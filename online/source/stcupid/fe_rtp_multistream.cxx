// fe_rtp_multistream.cxx
// MIDAS frontend: UDP/RTP receiver -> multi-stream (SSRC) -> MIDAS banks
//
// Build idea:
//   - RX thread: recvfrom() UDP, parse RTP header + payload, push samples into per-SSRC queues
//   - Builder thread: periodically (or when enough data) packs latest samples into one "event buffer"
//   - MIDAS readout(): pops prepared event buffer and publishes it (banks)
//
// Assumptions matching your simulator/protocol:
//   - RTP header has CC>=1 and CSRC[0] encodes "freq_div"
//   - Payload is array of big-endian uint32 words
//   - word0 is metadata (bessel etc) => skipped
//   - samples extracted as: sample24 = (ntohl(word) >> 8) & 0x00FFFFFF
//
// References: MIDAS frontend examples and mtfe template.  :contentReference[oaicite:1]{index=1}

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "midas.h"
#include "mfe.h"
#include "msystem.h"

// ---------------- MIDAS globals ----------------
const char *frontend_name = "RTP Frontend";
const char *frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 1000; // ms

// We will use polled equipment; MIDAS calls readout periodically.
INT max_event_size = 5 * 1024 * 1024;
INT event_buffer_size = 10 * 1024 * 1024;

// ---------------- Configuration (defaults) ----------------
static int g_udp_port = 5004;
static int g_samples_per_event_per_ch = 256; // how many samples we pack per channel per MIDAS event
static int g_event_period_ms = 100;          // builder cadence
static int g_expected_channels = 20;

// SSRC mapping: choose stable mapping to bank names C000..C019.
// If you want fixed SSRC values (0x100..0x109 and 0x200..0x209), you can prefill here.
static std::vector<uint32_t> g_ssrc_list;

// ---------------- Data structures ----------------
struct ChannelQueue {
  std::mutex m;
  std::deque<uint32_t> samples; // store sample24 as DWORD (low 24 bits used)
  uint32_t last_seq = 0;
  uint32_t last_ts = 0;
  uint32_t csrc0 = 0;
};

static std::mutex g_map_mtx;
static std::map<uint32_t, ChannelQueue> g_channels; // key = SSRC

// Prepared MIDAS event content produced by builder thread.
struct BuiltEvent {
  // banks: one per channel index, each bank is vector<DWORD>
  // also store timestamp/seq meta if you want separate banks later.
  std::vector<std::vector<uint32_t>> bank_data; // size = Nch
};

static std::mutex g_evt_mtx;
static std::condition_variable g_evt_cv;
static std::queue<BuiltEvent> g_ready_events;
static const size_t g_max_ready_events = 10;

// Threads control
static std::atomic<bool> g_run{false};
static std::thread g_rx_thread;
static std::thread g_builder_thread;

// ---------------- Utilities ----------------
static inline uint32_t be32toh_u32(uint32_t x) { return ntohl(x); }

static int bank_index_for_ssrc(uint32_t ssrc)
{
  // If g_ssrc_list is set, index is its position. Otherwise, assign in discovery order.
  if (!g_ssrc_list.empty()) {
    for (size_t i = 0; i < g_ssrc_list.size(); i++)
      if (g_ssrc_list[i] == ssrc) return (int)i;
    return -1;
  }

  // discovery order with cap
  std::lock_guard<std::mutex> lk(g_map_mtx);
  // If already exists, count position in map iteration order is unstable; better keep a list.
  // For simplicity, we auto-fill g_ssrc_list on first encounter.
  for (size_t i = 0; i < g_ssrc_list.size(); i++)
    if (g_ssrc_list[i] == ssrc) return (int)i;

  if ((int)g_ssrc_list.size() >= g_expected_channels) return -1;
  g_ssrc_list.push_back(ssrc);
  return (int)g_ssrc_list.size() - 1;
}

static std::string bank_name_for_index(int idx)
{
  // "C000".."C019"
  char b[5];
  std::snprintf(b, sizeof(b), "C%03d", idx);
  return std::string(b);
}

// ---------------- UDP/RTP receiver thread ----------------
static void rx_loop()
{
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    cm_msg(MERROR, "rx_loop", "socket() failed");
    return;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)g_udp_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
    cm_msg(MERROR, "rx_loop", "bind() failed on UDP port %d", g_udp_port);
    ::close(fd);
    return;
  }

  std::vector<uint8_t> buf(65536);

  cm_msg(MINFO, "rx_loop", "Listening UDP on port %d", g_udp_port);

  while (g_run.load()) {
    ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
    if (n <= 0) continue;
    if (n < 12) continue;

    uint8_t b0 = buf[0];
    uint8_t cc = (uint8_t)(b0 & 0x0F);

    uint16_t seq = ntohs(*(uint16_t*)(&buf[2]));
    uint32_t ts  = ntohl(*(uint32_t*)(&buf[4]));
    uint32_t ssrc= ntohl(*(uint32_t*)(&buf[8]));

    size_t header_len = 12 + 4u * (size_t)cc;
    if ((size_t)n < header_len) continue;

    uint32_t csrc0 = 0;
    if (cc >= 1) csrc0 = ntohl(*(uint32_t*)(&buf[12]));

    const uint8_t* payload = buf.data() + header_len;
    size_t payload_len = (size_t)n - header_len;
    size_t nwords = payload_len / 4;
    if (nwords < 2) continue; // need meta + >=1 sample

    // Map SSRC to an index
    int idx = bank_index_for_ssrc(ssrc);
    if (idx < 0) continue;

    // Ensure channel exists
    {
      std::lock_guard<std::mutex> lk(g_map_mtx);
      (void)g_channels[ssrc]; // default construct if absent
    }

    // Parse payload words
    // word0 metadata skipped
    std::vector<uint32_t> local_samples;
    local_samples.reserve(nwords - 1);

    for (size_t i = 1; i < nwords; i++) {
      uint32_t w_be;
      std::memcpy(&w_be, payload + i*4, 4);
      uint32_t w = be32toh_u32(w_be);
      uint32_t s24 = (w >> 8) & 0x00FFFFFFu;
      local_samples.push_back(s24);
    }

    // Push into per-channel queue
    {
      std::lock_guard<std::mutex> lk(g_channels[ssrc].m);
      auto &cq = g_channels[ssrc];
      cq.csrc0 = csrc0;
      cq.last_seq = seq;
      cq.last_ts = ts;
      cq.samples.insert(cq.samples.end(), local_samples.begin(), local_samples.end());

      // Optional: bound memory (drop oldest)
      const size_t max_keep = (size_t)g_samples_per_event_per_ch * 50;
      if (cq.samples.size() > max_keep) {
        cq.samples.erase(cq.samples.begin(), cq.samples.begin() + (cq.samples.size() - max_keep));
      }
    }
  }
  static uint64_t cnt=0;
  cnt++;
  if ((cnt % 1000) == 0) cm_msg(MINFO, "rx_loop", "Received %llu packets", (unsigned long long)cnt);

  ::close(fd);
}

// ---------------- Builder thread ----------------
static void builder_loop()
{
  cm_msg(MINFO, "builder_loop", "Builder period %d ms, samples/event/ch %d",
         g_event_period_ms, g_samples_per_event_per_ch);

  while (g_run.load()) {
    auto t0 = std::chrono::steady_clock::now();

    BuiltEvent ev;
    ev.bank_data.resize((size_t)g_expected_channels);

    // Snapshot per channel
    for (int ch = 0; ch < g_expected_channels; ch++) {
      if ((int)g_ssrc_list.size() <= ch) continue; // not discovered yet
      uint32_t ssrc = g_ssrc_list[(size_t)ch];

      std::lock_guard<std::mutex> lk(g_channels[ssrc].m);
      auto &cq = g_channels[ssrc];

      // take up to N samples
      size_t take = std::min((size_t)g_samples_per_event_per_ch, cq.samples.size());
      if (take == 0) continue;

      ev.bank_data[(size_t)ch].assign(cq.samples.begin(), cq.samples.begin() + take);
      cq.samples.erase(cq.samples.begin(), cq.samples.begin() + take);
    }

    // Only enqueue if there is any data
    bool any = false;
    for (auto &v : ev.bank_data) if (!v.empty()) { any = true; break; }

    if (any) {
      std::unique_lock<std::mutex> lk(g_evt_mtx);
      if (g_ready_events.size() >= g_max_ready_events) {
        // drop oldest to avoid unbounded growth
        g_ready_events.pop();
      }
      g_ready_events.push(std::move(ev));
      lk.unlock();
      g_evt_cv.notify_one();
    }

    // Sleep to maintain cadence
    auto t1 = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    int sleep_ms = g_event_period_ms - (int)elapsed;
    if (sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }
}

// ---------------- MIDAS equipment callbacks ----------------
static INT readout_rtp_event(char *pevent, INT off)
{
  (void)off;
  BuiltEvent ev;

  // Non-blocking pop: if nothing ready, return 0 (no event)
  {
    std::lock_guard<std::mutex> lk(g_evt_mtx);
    if (g_ready_events.empty())
      return 0;
    ev = std::move(g_ready_events.front());
    g_ready_events.pop();
  }

  bk_init32(pevent);

  // Build banks
  for (int ch = 0; ch < (int)ev.bank_data.size(); ch++) {
    auto &data = ev.bank_data[(size_t)ch];
    if (data.empty()) continue;

    std::string bname = bank_name_for_index(ch);

    DWORD *pdata = nullptr;
    bk_create(pevent, bname.c_str(), TID_DWORD, (void**)&pdata);
    std::memcpy(pdata, data.data(), data.size() * sizeof(uint32_t));
    bk_close(pevent, pdata + data.size());
  }

  return bk_size(pevent);
}

// ---------------- Equipment list ----------------
EQUIPMENT equipment[] = {
  { "RTP",
    { 1, 0,              // event ID, trigger mask
      "SYSTEM",          // event buffer
      EQ_POLLED,         // equipment type
      0,                 // not used
      "MIDAS",           // format
      TRUE,              // enabled
      RO_ALWAYS,        // read only when running
      500,               // poll interval (ms)
      0, 0, 0, 0,        // readout info
      0, 0, 0 },         // <-- FIX QUI
    readout_rtp_event,   // readout routine
    nullptr, nullptr, nullptr
  },
  { "" }
};

// ---------------- Frontend lifecycle ----------------
INT frontend_init()
{
  // Optional: hardcode SSRC list to match your producer:
  // 10 @1kHz: 0x100..0x109 ; 10 @5kHz: 0x200..0x209
  // Comment this block if you prefer discovery.
  g_ssrc_list.clear();
  for (int i = 0; i < 10; i++) g_ssrc_list.push_back(0x100u + (uint32_t)i);
  for (int i = 0; i < 10; i++) g_ssrc_list.push_back(0x200u + (uint32_t)i);

  // ODB settings (minimal)
  HNDLE hDB; cm_get_experiment_database(&hDB, NULL);

  // Create /Equipment/RTP/Settings if missing
  db_create_record(hDB, 0, "/Equipment/RTP/Settings",
                   "UDP Port = INT : 5004\n"
                   "SamplesPerEventPerCh = INT : 256\n"
                   "EventPeriodMs = INT : 100\n"
                   "ExpectedChannels = INT : 20\n");

  int size;

  size = sizeof(g_udp_port);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/UDP Port", &g_udp_port, &size, TID_INT, TRUE);

  size = sizeof(g_samples_per_event_per_ch);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/SamplesPerEventPerCh", &g_samples_per_event_per_ch, &size, TID_INT, TRUE);

  size = sizeof(g_event_period_ms);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/EventPeriodMs", &g_event_period_ms, &size, TID_INT, TRUE);

  size = sizeof(g_expected_channels);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/ExpectedChannels", &g_expected_channels, &size, TID_INT, TRUE);

  cm_msg(MINFO, "frontend_init", "Config: port=%d samples/event/ch=%d period=%dms expected_ch=%d",
         g_udp_port, g_samples_per_event_per_ch, g_event_period_ms, g_expected_channels);

  return SUCCESS;
}

// ----------------------------------------------------------------------
// Required MIDAS frontend stubs (even if unused)
// ----------------------------------------------------------------------


// Fragmentation support (not used, but required)
INT max_event_size_frag = 0;

// Polled equipment support (required by mfe.cxx)
INT poll_event(INT source, INT count, BOOL test)
{
  (void)source;
  (void)count;
  (void)test;

  // test==TRUE: MIDAS chiede "c'è qualcosa pronto?"
  // test==FALSE: MIDAS può chiamare in loop; per un polled semplice va bene uguale.
  std::lock_guard<std::mutex> lk(g_evt_mtx);
  return g_ready_events.empty() ? 0 : 1;
}

// Interrupt support (required even if not used)
INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
  (void)cmd;
  (void)source;
  (void)adr;
  return SUCCESS;
}

// Optional overwrite hook (safe default)
BOOL equipment_common_overwrite = FALSE;


INT frontend_exit()
{
  return SUCCESS;
}

INT begin_of_run(INT run_number, char *error)
{
  (void)run_number; (void)error;

  g_run.store(true);

  // Clear old queues
  {
    std::lock_guard<std::mutex> lk(g_map_mtx);
    for (auto &kv : g_channels) {
      std::lock_guard<std::mutex> lk2(kv.second.m);
      kv.second.samples.clear();
    }
  }
  {
    std::lock_guard<std::mutex> lk(g_evt_mtx);
    while (!g_ready_events.empty()) g_ready_events.pop();
  }

  g_rx_thread = std::thread(rx_loop);
  g_builder_thread = std::thread(builder_loop);

  cm_msg(MINFO, "begin_of_run", "Started RX and builder threads");
  return SUCCESS;
}

INT end_of_run(INT run_number, char *error)
{
  (void)run_number; (void)error;

  g_run.store(false);

  if (g_rx_thread.joinable()) g_rx_thread.join();
  if (g_builder_thread.joinable()) g_builder_thread.join();

  cm_msg(MINFO, "end_of_run", "Stopped RX and builder threads");
  return SUCCESS;
}

INT pause_run(INT run_number, char *error)
{
  (void)run_number; (void)error;
  return SUCCESS;
}

INT resume_run(INT run_number, char *error)
{
  (void)run_number; (void)error;
  return SUCCESS;
}

INT frontend_loop()
{
  return SUCCESS;
}
