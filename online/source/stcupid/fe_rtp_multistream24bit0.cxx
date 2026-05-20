// fe_rtp_multistream.cxx
// MIDAS frontend: UDP/RTP receiver -> multi-stream (SSRC) -> MIDAS banks
//
// Adapted to receive "producer_24bit" payload:
//   - RTP header: CC>=1 typically, CSRC[0] present (stored as metadata but not required)
//   - Payload format per sample record (4 bytes):
//       [channel_id][MSB][MID][LSB]
//       sample24 = (MSB<<16)|(MID<<8)|LSB  (0..0xFFFFFF, offset-binary)
//   - Payload has an extra trailing byte 0x00 (so payload_len = N*4 + 1).
//     We parse floor(payload_len/4) records and ignore the remainder.
//   - No "meta word" at payload[0] (unlike previous producer).
//
// Threads:
//   - RX thread: recvfrom UDP, parse RTP, push sample24 into per-SSRC deque
//   - Builder thread: every period, pulls up to N samples per channel, prepares MIDAS event
//   - MIDAS readout: pops prepared event and publishes banks C000..
//
// Notes:
//   - Uses discovery mapping SSRC->bank index by default (supports variable channel count).
//   - Avoids unaligned pointer casts by using memcpy().
//
// Requires MIDAS headers/libraries.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <algorithm>

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

INT max_event_size = 5 * 1024 * 1024;
INT event_buffer_size = 10 * 1024 * 1024;

// ---------------- Configuration (defaults) ----------------
static int g_udp_port = 6666;
static int g_samples_per_event_per_ch = 256; // how many samples we pack per channel per MIDAS event
static int g_event_period_ms = 100;          // builder cadence
static int g_expected_channels = 20;

// If non-empty, this fixes SSRC->bank index mapping (index = position in list).
// If empty, we use discovery order (first time a SSRC arrives, it gets next free index).
static std::vector<uint32_t> g_ssrc_list;

// ---------------- Data structures ----------------
struct ChannelQueue {
  std::mutex m;
  std::deque<uint32_t> samples; // store sample24 as DWORD (low 24 bits used)
  uint32_t last_seq = 0;
  uint32_t last_ts = 0;
  uint32_t csrc0 = 0;
  uint64_t packets = 0;
};

static std::mutex g_map_mtx;
static std::map<uint32_t, ChannelQueue> g_channels; // key = SSRC

struct BuiltEvent {
  std::vector<std::vector<uint32_t>> bank_data; // size = expected channels
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
static inline uint32_t u32_be_to_host(const uint8_t* p) {
  uint32_t x_be;
  std::memcpy(&x_be, p, 4);
  return ntohl(x_be);
}
static inline uint16_t u16_be_to_host(const uint8_t* p) {
  uint16_t x_be;
  std::memcpy(&x_be, p, 2);
  return ntohs(x_be);
}

static int bank_index_for_ssrc(uint32_t ssrc)
{
  // If g_ssrc_list is fixed, index is its position.
  if (!g_ssrc_list.empty()) {
    for (size_t i = 0; i < g_ssrc_list.size(); i++)
      if (g_ssrc_list[i] == ssrc) return (int)i;
    return -1;
  }

  // Discovery mapping.
  std::lock_guard<std::mutex> lk(g_map_mtx);

  // If already discovered
  for (size_t i = 0; i < g_ssrc_list.size(); i++)
    if (g_ssrc_list[i] == ssrc) return (int)i;

  // Assign new index if capacity allows
  if ((int)g_ssrc_list.size() >= g_expected_channels) return -1;
  g_ssrc_list.push_back(ssrc);
  return (int)g_ssrc_list.size() - 1;
}

static std::string bank_name_for_index(int idx)
{
  // "C000".."C999"
  char b[5];
  std::snprintf(b, sizeof(b), "C%03d", idx);
  return std::string(b);
}

static ChannelQueue& get_or_create_channel(uint32_t ssrc)
{
  std::lock_guard<std::mutex> lk(g_map_mtx);
  return g_channels[ssrc]; // create if absent
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

  uint64_t rx_packets = 0;

  while (g_run.load()) {
    ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
    if (n <= 0) continue;
    if (n < 12) continue;

    // RTP fixed header fields
    uint8_t b0 = buf[0];
    uint8_t v = (uint8_t)(b0 >> 6);
    uint8_t cc = (uint8_t)(b0 & 0x0F);
    if (v != 2) continue; // RTP v2 only

    uint16_t seq = u16_be_to_host(&buf[2]);
    uint32_t ts  = u32_be_to_host(&buf[4]);
    uint32_t ssrc= u32_be_to_host(&buf[8]);

    size_t header_len = 12 + 4u * (size_t)cc;
    if ((size_t)n < header_len) continue;

    uint32_t csrc0 = 0;
    if (cc >= 1) csrc0 = u32_be_to_host(&buf[12]);

    const uint8_t* payload = buf.data() + header_len;
    size_t payload_len = (size_t)n - header_len;
    if (payload_len < 4) continue;

    // Payload from my producer: N records of 4 bytes + 1 trailing 0x00.
    // Parse floor(payload_len/4) records, ignore remainder.
    size_t nrecs = payload_len / 4;
    if (nrecs < 1) continue;

    // Map SSRC to bank index; drop if too many channels
    int idx = bank_index_for_ssrc(ssrc);
    if (idx < 0) continue;

    ChannelQueue& cq_ref = get_or_create_channel(ssrc);

    std::vector<uint32_t> local_samples;
    local_samples.reserve(nrecs);

    for (size_t irec = 0; irec < nrecs; irec++) {
      const uint8_t* r = payload + irec * 4;

      // record: [channel_id][MSB][MID][LSB]
      // channel_id currently optional; we don't enforce it, but you can sanity-check:
      // uint8_t channel_id = r[0];

      uint32_t s24 = (uint32_t(r[1]) << 16) | (uint32_t(r[2]) << 8) | uint32_t(r[3]);
      local_samples.push_back(s24);
    }

    {
      std::lock_guard<std::mutex> lk(cq_ref.m);
      cq_ref.csrc0 = csrc0;
      cq_ref.last_seq = seq;
      cq_ref.last_ts = ts;
      cq_ref.packets++;
      cq_ref.samples.insert(cq_ref.samples.end(), local_samples.begin(), local_samples.end());

      // Bound memory (drop oldest)
      const size_t max_keep = (size_t)g_samples_per_event_per_ch * 50;
      if (cq_ref.samples.size() > max_keep) {
        cq_ref.samples.erase(cq_ref.samples.begin(),
                             cq_ref.samples.begin() + (cq_ref.samples.size() - max_keep));
      }
    }

    rx_packets++;
    if ((rx_packets % 5000) == 0) {
      cm_msg(MINFO, "rx_loop", "Received %llu RTP packets (total)", (unsigned long long)rx_packets);
    }
  }

  ::close(fd);
  cm_msg(MINFO, "rx_loop", "RX thread exiting");
}

// ---------------- Builder thread ----------------
static void builder_loop()
{
  cm_msg(MINFO, "builder_loop", "Builder period %d ms, samples/event/ch %d, expected_ch %d",
         g_event_period_ms, g_samples_per_event_per_ch, g_expected_channels);

  while (g_run.load()) {
    auto t0 = std::chrono::steady_clock::now();

    BuiltEvent ev;
    ev.bank_data.resize((size_t)g_expected_channels);

    // Snapshot per channel index
    for (int ch = 0; ch < g_expected_channels; ch++) {
      // Not discovered yet
      if ((int)g_ssrc_list.size() <= ch) continue;

      uint32_t ssrc = g_ssrc_list[(size_t)ch];

      ChannelQueue& cq = get_or_create_channel(ssrc);
      std::lock_guard<std::mutex> lk(cq.m);

      size_t take = std::min((size_t)g_samples_per_event_per_ch, cq.samples.size());
      if (take == 0) continue;

      ev.bank_data[(size_t)ch].assign(cq.samples.begin(), cq.samples.begin() + take);
      cq.samples.erase(cq.samples.begin(), cq.samples.begin() + take);
    }

    bool any = false;
    for (auto &v : ev.bank_data) {
      if (!v.empty()) { any = true; break; }
    }

    if (any) {
      std::unique_lock<std::mutex> lk(g_evt_mtx);
      if (g_ready_events.size() >= g_max_ready_events) {
        g_ready_events.pop(); // drop oldest
      }
      g_ready_events.push(std::move(ev));
      lk.unlock();
      g_evt_cv.notify_one();
    }

    // cadence
    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    int sleep_ms = g_event_period_ms - (int)elapsed_ms;
    if (sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  cm_msg(MINFO, "builder_loop", "Builder thread exiting");
}

// ---------------- MIDAS equipment callbacks ----------------
static INT readout_rtp_event(char *pevent, INT off)
{
  (void)off;
  BuiltEvent ev;

  // Non-blocking pop
  {
    std::lock_guard<std::mutex> lk(g_evt_mtx);
    if (g_ready_events.empty())
      return 0;
    ev = std::move(g_ready_events.front());
    g_ready_events.pop();
  }

  bk_init32(pevent);

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
//
// NOTE: The exact EQUIPMENT_INFO initializer fields can vary slightly by MIDAS version.
// This matches common "mtfe" style and is usually accepted as-is.
EQUIPMENT equipment[] = {
  { "RTP",
    { 1, 0,              // event ID, trigger mask
      "SYSTEM",          // event buffer
      EQ_POLLED,         // equipment type
      0,                 // source
      "MIDAS",           // format
      TRUE,              // enabled
      RO_ALWAYS,         // read only when running
      100,               // poll interval (ms)
      0, 0, 0, 0,        // event limit & readout info (unused)
      0, 0, 0 },         // unused
    readout_rtp_event,
    nullptr, nullptr, nullptr
  },
  { "" }
};

// ---------------- Frontend lifecycle ----------------
INT frontend_init()
{
  // By default: discovery mapping (supports variable channel count).
  // If you want a fixed mapping for my producer default SSRC base 0xBDAC0001..:
  //   g_ssrc_list.clear();
  //   for (int i=0; i<g_expected_channels; i++) g_ssrc_list.push_back(0xBDAC0000u + uint32_t(i+1));
  g_ssrc_list.clear();

  // ODB settings (minimal)
  HNDLE hDB; cm_get_experiment_database(&hDB, NULL);

  db_create_record(hDB, 0, "/Equipment/RTP/Settings",
                   "UDP Port = INT : 6666\n"
                   "SamplesPerEventPerCh = INT : 256\n"
                   "EventPeriodMs = INT : 100\n"
                   "ExpectedChannels = INT : 20\n");

  int size;

  size = sizeof(g_udp_port);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/UDP Port", &g_udp_port, &size, TID_INT, TRUE);

  size = sizeof(g_samples_per_event_per_ch);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/SamplesPerEventPerCh",
               &g_samples_per_event_per_ch, &size, TID_INT, TRUE);

  size = sizeof(g_event_period_ms);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/EventPeriodMs",
               &g_event_period_ms, &size, TID_INT, TRUE);

  size = sizeof(g_expected_channels);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/ExpectedChannels",
               &g_expected_channels, &size, TID_INT, TRUE);

  cm_msg(MINFO, "frontend_init",
         "Config: port=%d samples/event/ch=%d period=%dms expected_ch=%d (SSRC mapping: %s)",
         g_udp_port, g_samples_per_event_per_ch, g_event_period_ms, g_expected_channels,
         g_ssrc_list.empty() ? "discovery" : "fixed");

  return SUCCESS;
}

// Fragmentation support (not used, but required)
INT max_event_size_frag = 0;

// Polled equipment support
INT poll_event(INT source, INT count, BOOL test)
{
  (void)source; (void)count; (void)test;
  std::lock_guard<std::mutex> lk(g_evt_mtx);
  return g_ready_events.empty() ? 0 : 1;
}

// Interrupt support (required even if not used)
INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
  (void)cmd; (void)source; (void)adr;
  return SUCCESS;
}

// Optional overwrite hook
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
      kv.second.packets = 0;
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

  // recvfrom() is blocking; to ensure prompt exit you can send a dummy packet locally,
  // but typically end_of_run is fine if traffic continues. If needed, add a socket timeout.
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
