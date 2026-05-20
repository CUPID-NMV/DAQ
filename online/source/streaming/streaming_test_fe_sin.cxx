/********************************************************************\
  Streaming sine frontend: 8 channels, 5 MS/s per channel, 80 MB/s total

  - Generates 8 sine waves with different amplitudes and frequencies
  - Sampling rate = 5 MHz
  - Output type = TID_WORD (16-bit)
  - Total throughput = 8 * 5e6 samples/s * 2 bytes = 80 MB/s
  - Uses EQ_PERIODIC (time-sliced events)
\********************************************************************/

#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <random>

#include "midas.h"

/* MIDAS globals */
char *frontend_name      = (char*)"stream80_sine_fe";
char *frontend_file_name = (char*)__FILE__;
BOOL frontend_call_loop  = FALSE;
INT  display_period      = 3000;

/* Size tuning */
INT max_event_size       = 2 * 1024 * 1024;     // ~0.8 MB/event at 10 ms slicing is typical
INT max_event_size_frag  = 2 * 1024 * 1024;
INT event_buffer_size    = 64 * 1024 * 1024;

/* Forward declarations */
INT frontend_init();
INT frontend_exit();
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop();

INT read_stream_event(char *pevent, INT off);

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

/* --- signal parameters --- */
static constexpr int    kNch      = 8;
static constexpr double kFs       = 5.0e6;                 // 5 MHz sampling
static constexpr int    kPeriodMs = 10;                    // time slice

// Clamp to avoid pathological spikes if scheduling stalls.
static constexpr uint32_t kMinSamplesPerSlice = 1024;
static constexpr uint32_t kMaxSamplesPerSlice = 250000;    // 50 ms worth @ 5MHz

// Sine lookup table (fast generation)
static constexpr int kLutSize = 4096;
static double g_lut[kLutSize];

// Per-channel phase accumulators (LUT index in double)
static double g_phase[kNch];

// Per-channel frequency (Hz) and amplitude (ADC counts)
static double   g_freq_hz[kNch];
static uint16_t g_amp_counts[kNch];

static std::chrono::steady_clock::time_point g_last;
static uint64_t g_seq = 0;

static inline uint16_t clip_u16(int v)
{
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return (uint16_t)v;
}


/* MIDAS transitions */
INT frontend_init()
{
  // Build LUT
  for (int i = 0; i < kLutSize; i++) {
    g_lut[i] = std::sin(2.0 * M_PI * (double)i / (double)kLutSize);
  }

  // Example frequencies + amplitudes (customize as needed)
  // Choose any set you like; keep below Nyquist (2.5 MHz).
  g_freq_hz[0] =  50e3;  g_amp_counts[0] =  2000;
  g_freq_hz[1] = 100e3;  g_amp_counts[1] =  4000;
  g_freq_hz[2] = 200e3;  g_amp_counts[2] =  8000;
  g_freq_hz[3] = 300e3;  g_amp_counts[3] = 12000;
  g_freq_hz[4] = 500e3;  g_amp_counts[4] = 16000;
  g_freq_hz[5] = 800e3;  g_amp_counts[5] = 10000;
  g_freq_hz[6] = 1.2e6;  g_amp_counts[6] =  6000;
  g_freq_hz[7] = 2.0e6;  g_amp_counts[7] =  3000;

  for (int ch = 0; ch < kNch; ch++) {
    g_phase[ch] = 0.0;
  }

  g_last = std::chrono::steady_clock::now();
  g_seq  = 0;

  cm_msg(MINFO, frontend_name,
         "Initialized sine streaming frontend: %d ch, fs=%.1f MHz, total throughput=%.1f MB/s.",
         kNch, kFs/1e6, (kNch * kFs * 2.0)/1e6);

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
  for (int ch = 0; ch < kNch; ch++) g_phase[ch] = 0.0;
  cm_msg(MINFO, frontend_name, "Begin of run: reset phases and counters.");
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

    // Phase increment per sample (in LUT indices)
    const double phase_inc = (g_freq_hz[ch] / kFs) * (double)kLutSize;

    // Synthesize samples
    const int32_t mid = 32768;
    const int32_t amp = (int32_t)g_amp_counts[ch];

    double ph = g_phase[ch];
    for (uint32_t i = 0; i < nsamp; i++) {
      int idx = (int)ph;
      // ensure idx in [0, kLutSize)
      idx &= (kLutSize - 1); // kLutSize is power-of-two (4096)
      const double s = g_lut[idx];

      const int32_t v = mid + (int32_t)llround((double)amp * s);

      *pdata++ = (WORD)clip_u16(v);

      ph += phase_inc;
      if (ph >= (double)kLutSize) ph -= (double)kLutSize;
      if (ph < 0.0) ph += (double)kLutSize;
    }
    g_phase[ch] = ph;

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
/* Required by mfe.cxx even if not used by EQ_PERIODIC */
BOOL equipment_common_overwrite = TRUE;

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

