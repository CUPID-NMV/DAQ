/********************************************************************\
  Streaming RANDOM frontend: 8 channels, 5 MS/s per channel, 80 MB/s total

  - Generates 8 channels of random samples (uniform noise) around mid-scale
  - Sampling rate = 5 MHz
  - Output type = TID_WORD (16-bit)
  - Total throughput = 8 * 5e6 samples/s * 2 bytes = 80 MB/s
  - Uses EQ_PERIODIC (time-sliced events, default 10 ms)
  - Random amplitude is limited to 10% of full 16-bit range (±~6553 counts)
\********************************************************************/

#include <cstdint>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cmath>

#include "midas.h"

/* MIDAS globals */
char *frontend_name      = (char*)"stream80_random_fe";
char *frontend_file_name = (char*)__FILE__;
BOOL frontend_call_loop  = FALSE;
INT  display_period      = 3000;

/* Size tuning */
INT max_event_size       = 2 * 1024 * 1024;     // ~0.8 MB/event at 10 ms slicing
INT max_event_size_frag  = 2 * 1024 * 1024;
INT event_buffer_size    = 64 * 1024 * 1024;

/* Required by mfe.cxx even if not used by EQ_PERIODIC */
BOOL equipment_common_overwrite = TRUE;

/* Forward declarations */
INT frontend_init();
INT frontend_exit();
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop();

INT read_stream_event(char *pevent, INT off);

INT poll_event(INT source, INT count, BOOL test);
INT interrupt_configure(INT cmd, INT source, POINTER_T adr);

/* Equipment list */
EQUIPMENT equipment[] = {
  {
    "Streamer",
    {
      1, 0,
      "SYSTEM",
      EQ_PERIODIC,
      0,
      "MIDAS",
      TRUE,
      RO_RUNNING,
      10,   /* period in ms (10 ms) */
      0,
      0,
      0,
      "", "", "",
    },
    read_stream_event,
  },
  { "" }
};

/* --- random signal parameters --- */
static constexpr int      kNch      = 8;
static constexpr double   kFs       = 5.0e6;        // 5 MHz sampling
static constexpr int      kPeriodMs = 10;

// Clamp to avoid pathological spikes if scheduling stalls.
static constexpr uint32_t kMinSamplesPerSlice = 1024;
static constexpr uint32_t kMaxSamplesPerSlice = 250000;    // 50 ms worth @ 5MHz

// 16-bit ADC model
static constexpr uint16_t kAdcMid       = 32768;
static constexpr int32_t  kAdcFullRange = 65535;

// Max random amplitude = 10% full range (peak around mid)
static constexpr int32_t  kAmpMax = (int32_t)(0.10 * (double)kAdcFullRange); // ~6553

static std::chrono::steady_clock::time_point g_last;
static uint64_t g_seq = 0;

/* Fast PRNG per channel: xorshift64* */
static uint64_t g_rng_state[kNch];

static inline uint64_t xorshift64star(uint64_t& x)
{
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  return x * 2685821657736338717ULL;
}

static inline uint16_t clip_u16(int32_t v)
{
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return (uint16_t)v;
}

static inline uint32_t urand_u32(uint64_t& state, uint32_t max_inclusive)
{
  return (uint32_t)(xorshift64star(state) % (uint64_t)(max_inclusive + 1u));
}

/* MIDAS transitions */
INT frontend_init()
{
  // Seed per-channel PRNG states
  uint64_t seed0 = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
  for (int ch = 0; ch < kNch; ch++) {
    g_rng_state[ch] = seed0 ^ (0x9E3779B97F4A7C15ULL + (uint64_t)ch * 0xBF58476D1CE4E5B9ULL);
    if (g_rng_state[ch] == 0) g_rng_state[ch] = 1ULL + (uint64_t)ch;
  }

  g_last = std::chrono::steady_clock::now();
  g_seq  = 0;

  cm_msg(MINFO, frontend_name,
         "Initialized RANDOM streaming frontend: %d ch, fs=%.1f MHz, total throughput=%.1f MB/s, amp<=10%% (±%d counts).",
         kNch, kFs/1e6, (kNch * kFs * 2.0)/1e6, (int)kAmpMax);

  return SUCCESS;
}

INT frontend_exit()
{
  cm_msg(MINFO, frontend_name, "Exiting.");
  return SUCCESS;
}

INT begin_of_run(INT run_number, char *error)
{
  (void)run_number; (void)error;
  g_last = std::chrono::steady_clock::now();
  g_seq  = 0;
  cm_msg(MINFO, frontend_name, "Begin of run: reset counters.");
  return SUCCESS;
}

INT end_of_run(INT run_number, char *error)
{
  (void)run_number; (void)error;
  cm_msg(MINFO, frontend_name, "End of run.");
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
  g_last = std::chrono::steady_clock::now();
  return SUCCESS;
}

INT frontend_loop()
{
  return SUCCESS;
}

/* Readout: generate one time-sliced event */
INT read_stream_event(char *pevent, INT off)
{
  (void)off;

  // Use elapsed time to decide how many samples to generate
  auto now = std::chrono::steady_clock::now();
  double dt = std::chrono::duration<double>(now - g_last).count();
  g_last = now;

  // samples per channel for this slice
  uint32_t nsamp = (uint32_t)llround(kFs * dt);
  nsamp = std::clamp(nsamp, kMinSamplesPerSlice, kMaxSamplesPerSlice);

  // Total throughput is intrinsically:
  // bytes = nsamp * kNch * sizeof(WORD)
  const uint32_t bytes_total = nsamp * kNch * (uint32_t)sizeof(WORD);

  bk_init32(pevent);

  // Generate banks CH00..CH07, each is TID_WORD with nsamp samples
  for (int ch = 0; ch < kNch; ch++) {
    char bank_name[5];
    std::snprintf(bank_name, sizeof(bank_name), "CH%02d", ch);

    WORD *pdata = nullptr;
    bk_create(pevent, bank_name, TID_WORD, (void**)&pdata);

    uint64_t& st = g_rng_state[ch];

    // Uniform in [mid-amp, mid+amp]
    for (uint32_t i = 0; i < nsamp; i++) {
      int32_t r = (int32_t)urand_u32(st, (uint32_t)(2 * kAmpMax)) - kAmpMax;
      int32_t v = (int32_t)kAdcMid + r;
      *pdata++ = (WORD)clip_u16(v);
    }

    bk_close(pevent, pdata);
  }

  // Small header (sequence, nsamp, bytes)
  {
    DWORD *h = nullptr;
    bk_create(pevent, "HDR0", TID_DWORD, (void**)&h);
    *h++ = (DWORD)(g_seq & 0xFFFFFFFFu);
    *h++ = (DWORD)((g_seq >> 32) & 0xFFFFFFFFu);
    *h++ = (DWORD)nsamp;
    *h++ = (DWORD)bytes_total;
    bk_close(pevent, h);
  }

  g_seq++;
  return bk_size(pevent);
}

/* Polling routine: not used for EQ_PERIODIC, but mfe links against it */
INT poll_event(INT source, INT count, BOOL test)
{
  (void)source; (void)count; (void)test;
  return 0; // FALSE: no polled event
}

/* Interrupt configuration: not used here, but mfe expects it */
INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
  (void)cmd; (void)source; (void)adr;
  return SUCCESS;
}
