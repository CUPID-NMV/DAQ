// fe_rtp_multistream.cxx
// MIDAS frontend: UDP/RTP receiver -> multi-stream (SSRC) -> MIDAS banks
//
// Supports two 4-byte sample word layouts:
//   Layout A: [channel_id][MSB][MID][LSB]
//   Layout B: [MSB][MID][LSB][channel_id]   <-- matches your current rtp_noise_24bit.py
//
// Samples are 24-bit offset-binary (0..0xFFFFFF) stored in uint32_t (low 24 bits).
// Payload has extra trailing byte (typically 0x00): payload_len = N*4 + 1.
//
// Threads:
//   - RX thread: recvfrom UDP, parse RTP, push sample24 into per-SSRC deque
//   - Builder thread: every period, pulls up to N samples per channel, prepares MIDAS event
//   - MIDAS readout: pops prepared event and publishes banks C000..
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
static int g_samples_per_event_per_ch = 256;
static int g_event_period_ms = 100;
static int g_expected_channels = 20;

// Word layout: 0 = A ([ch][MSB][MID][LSB]), 1 = B ([MSB][MID][LSB][ch])
static int g_word_layout = 1; // default to B to match your current Python sender

// Optional sanity check: if enabled and layout B, warn if channel_id != (ssrc & 0xFF)
static int g_check_channel_id = 0;

// If non-empty, fixes SSRC->bank index mapping.
static std::vector<uint32_t> g_ssrc_list;

// ---------------- Data structures ----------------
struct ChannelQueue {
  std::mutex m;
  std::deque<uint32_t> samples; // sample24 in low 24 bits
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
  if (!g_ssrc_list.empty()) {
    for (size_t i = 0; i < g_ssrc_list.size(); i++)
      if (g_ssrc_list[i] == ssrc) return (int)i;
    return -1;
  }

  std::lock_guard<std::mutex> lk(g_map_mtx);

  for (size_t i = 0; i < g_ssrc_list.size(); i++)
    if (g_ssrc_list[i] == ssrc) return (int)i;

  if ((int)g_ssrc_list.size() >= g_expected_channels) return -1;
  g_ssrc_list.push_back(ssrc);
  return (int)g_ssrc_list.size() - 1;
}

static std::string bank_name_for_index(int idx)
{
  char b[5];
  std::snprintf(b, sizeof(b), "C%03d", idx);
  return std::string(b);
}

static ChannelQueue& get_or_create_channel(uint32_t ssrc)
{
  std::lock_guard<std::mutex> lk(g_map_mtx);
  return g_channels[ssrc];
}

// Decode one 4-byte record into (sample24, channel_id)
static inline void decode_record_4b(const uint8_t* r, uint32_t& sample24, uint8_t& channel_id)
{
  if (g_word_layout == 0) {
    // Layout A: [channel_id][MSB][MID][LSB]
    channel_id = r[0];
    sample24 = (uint32_t(r[1]) << 16) | (uint32_t(r[2]) << 8) | uint32_t(r[3]);
  } else {
    // Layout B: [MSB][MID][LSB][channel_id]
    sample24 = (uint32_t(r[0]) << 16) | (uint32_t(r[1]) << 8) | uint32_t(r[2]);
    channel_id = r[3];
  }
  sample24 &= 0x00FFFFFFu;
}

// ---------------- UDP/RTP receiver thread ----------------
static void rx_loop()
{
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    cm_msg(MERROR, "rx_loop", "socket() failed");
    return;
  }

  // Add a timeout so end_of_run can stop promptly even if traffic stops
  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 200000; // 200 ms
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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

  cm_msg(MINFO, "rx_loop", "Listening UDP on port %d (WordLayout=%d)", g_udp_port, g_word_layout);

  uint64_t rx_packets = 0;

  while (g_run.load()) {
    ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
    if (n <= 0) continue;               // timeout or error
    if (n < 12) continue;

    // RTP header fields
    uint8_t b0 = buf[0];
    uint8_t v = (uint8_t)(b0 >> 6);
    uint8_t cc = (uint8_t)(b0 & 0x0F);
    if (v != 2) continue;

    uint16_t seq = u16_be_to_host(&buf[2]);
    uint32_t ts  = u32_be_to_host(&buf[4]);
    uint32_t ssrc= u32_be_to_host(&buf[8]);

    size_t header_len = 12 + 4u * (size_t)cc;
    if ((size_t)n < header_len) continue;

    uint32_t csrc0 = 0;
    if (cc >= 1) csrc0 = u32_be_to_host(&buf[12]);

    const uint8_t* payload = buf.data() + header_len;
    size_t payload_len = (size_t)n - header_len;
    if (payload_len < 5) continue; // need at least 1 record (4) + trailing (1)

    // Handle trailing byte: payload_len = N*4 + 1 (trailing 0x00)
    // Compute nrecs safely:
    size_t nrecs = 0;
    if (payload_len >= 5) {
      // assume trailing exists; if not, this still works for aligned payloads:
      nrecs = (payload_len - 1) / 4;
    }
    if (nrecs < 1) continue;

    int idx = bank_index_for_ssrc(ssrc);
    if (idx < 0) continue;

    ChannelQueue& cq_ref = get_or_create_channel(ssrc);

    std::vector<uint32_t> local_samples;
    local_samples.reserve(nrecs);

    // Optional: sanity-check channel_id
    const uint8_t expected_ch = (uint8_t)(ssrc & 0xFF);

    for (size_t irec = 0; irec < nrecs; irec++) {
      const uint8_t* r = payload + irec * 4;

      uint32_t s24 = 0;
      uint8_t ch_id = 0;
      decode_record_4b(r, s24, ch_id);

      if (g_check_channel_id && g_word_layout == 1) {
        if (ch_id != expected_ch) {
          // rate-limit a bit (avoid flooding)
          if ((rx_packets % 1000) == 0) {
            cm_msg(MINFO, "rx_loop",
                   "channel_id mismatch: ssrc=0x%08X expected=0x%02X got=0x%02X (layout B)",
                   ssrc, expected_ch, ch_id);
          }
        }
      }

      local_samples.push_back(s24);
    }

    {
      std::lock_guard<std::mutex> lk(cq_ref.m);
      cq_ref.csrc0 = csrc0;
      cq_ref.last_seq = seq;
      cq_ref.last_ts = ts;
      cq_ref.packets++;
      cq_ref.samples.insert(cq_ref.samples.end(), local_samples.begin(), local_samples.end());

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

    for (int ch = 0; ch < g_expected_channels; ch++) {
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
        g_ready_events.pop();
      }
      g_ready_events.push(std::move(ev));
      lk.unlock();
      g_evt_cv.notify_one();
    }

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
EQUIPMENT equipment[] = {
  { "RTP",
    { 1, 0,
      "SYSTEM",
      EQ_POLLED,
      0,
      "MIDAS",
      TRUE,
      RO_ALWAYS,
      100,
      0, 0, 0, 0,
      0, 0, 0 },
    readout_rtp_event,
    nullptr, nullptr, nullptr
  },
  { "" }
};

// ---------------- Frontend lifecycle ----------------
INT frontend_init()
{
  g_ssrc_list.clear();

  HNDLE hDB; cm_get_experiment_database(&hDB, NULL);

  db_create_record(hDB, 0, "/Equipment/RTP/Settings",
                   "UDP Port = INT : 6666\n"
                   "SamplesPerEventPerCh = INT : 256\n"
                   "EventPeriodMs = INT : 100\n"
                   "ExpectedChannels = INT : 20\n"
                   "WordLayout = INT : 1\n"
                   "CheckChannelId = INT : 0\n");

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

  size = sizeof(g_word_layout);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/WordLayout",
               &g_word_layout, &size, TID_INT, TRUE);
  if (g_word_layout != 0 && g_word_layout != 1) g_word_layout = 1;

  size = sizeof(g_check_channel_id);
  db_get_value(hDB, 0, "/Equipment/RTP/Settings/CheckChannelId",
               &g_check_channel_id, &size, TID_INT, TRUE);

  cm_msg(MINFO, "frontend_init",
         "Config: port=%d samples/event/ch=%d period=%dms expected_ch=%d WordLayout=%d CheckChannelId=%d (SSRC mapping: %s)",
         g_udp_port, g_samples_per_event_per_ch, g_event_period_ms, g_expected_channels,
         g_word_layout, g_check_channel_id,
         g_ssrc_list.empty() ? "discovery" : "fixed");

  return SUCCESS;
}

INT max_event_size_frag = 0;

INT poll_event(INT source, INT count, BOOL test)
{
  (void)source; (void)count; (void)test;
  std::lock_guard<std::mutex> lk(g_evt_mtx);
  return g_ready_events.empty() ? 0 : 1;
}

INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
  (void)cmd; (void)source; (void)adr;
  return SUCCESS;
}

BOOL equipment_common_overwrite = FALSE;

INT frontend_exit()
{
  return SUCCESS;
}

INT begin_of_run(INT run_number, char *error)
{
  (void)run_number; (void)error;

  g_run.store(true);

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
