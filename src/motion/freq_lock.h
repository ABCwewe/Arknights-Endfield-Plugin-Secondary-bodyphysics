#pragma once
// motion/freq_lock.h — auto-frequency alignment (pure logic, no Unity/IL2CPP
// deps so verify_tests.cpp can unit-test it directly).
//
// Semantics (user task): start from the CONFIG frequency; correct toward the
// measured animation-loop frequency only when the deviation stays above the
// per-gait threshold for several consecutive 20Hz samples.  Hysteresis:
// correction triggers at `threshold`, freezes below threshold/2.
// Physical frequency = kLocomotionPhaseCyclesPerLoop x loop frequency (same
// semantics as config frequency_hz).
#include <cmath>
#include <cstring>
#include "../config/config_types.h"

static constexpr float kFreqEmaAlpha = 0.3f;         // measurement EMA
static constexpr float kFreqCorrectionStep = 0.2f;   // per-sample pull toward meas
static constexpr int   kFreqDevSamples = 5;          // samples before correction (~0.25s)
static constexpr float kFreqMeasMaxDeltaNorm = 0.5f; // reject > half-loop per 50ms
static constexpr double kFreqLockCyclesPerLoop = 2.0;  // 1:2 loop->oscillator mapping

struct FreqLockState {
  float useHz = 0.0f;       // current oscillator frequency (cfg or corrected)
  float cfgHz = 0.0f;       // config frequency of the last sample (change -> reset)
  float measHz = 0.0f;      // EMA-smoothed measured physical frequency
  bool measValid = false;   // measurement published
  bool correcting = false;  // currently pulling useHz toward measHz
  int devCount = 0;         // consecutive over-threshold samples
  // measurement scratch (20Hz, reset on clip switch)
  float lastNorm = -1.0f;
  DWORD lastNormMs = 0;
  bool havePrev = false;
  char lastClip[128] = {0};
};

// One 20Hz sample.  `g` is the per-gait GaitParam of the current clip;
// `clipName` change restarts measurement + correction from the config value.
static inline void FreqLockTick(FreqLockState &f, const GaitParam &g,
                                const char *clipName, float norm, DWORD now) {
  const float cfgHz = g.frequencyHz;

  // clip switch -> restart measurement + correction from the config value
  if (strcmp(f.lastClip, clipName) != 0) {
    snprintf(f.lastClip, sizeof(f.lastClip), "%s", clipName);
    f.havePrev = false;
    f.lastNorm = -1.0f;
    f.lastNormMs = 0;
    f.cfgHz = cfgHz;
    f.useHz = cfgHz;
    f.measValid = false;
    f.correcting = false;
    f.devCount = 0;
    return;  // first sample of a clip: no delta yet
  }

  // config frequency is primary: a config change (hot reload / per-gait
  // switch) restarts from the new config value
  if (cfgHz != f.cfgHz) {
    f.cfgHz = cfgHz;
    f.useHz = cfgHz;
    f.correcting = false;
    f.devCount = 0;
  }

  // measurement: loop frequency from the normalizedTime delta (already
  // includes state-speed multipliers that Animator.speed misses)
  if (norm >= 0.0f && f.havePrev && f.lastNormMs) {
    float dNorm = norm - f.lastNorm;
    if (dNorm < 0.0f) dNorm += 1.0f;  // wrapped a full loop
    float dt = (float)(now - f.lastNormMs) / 1000.0f;
    if (dt > 0.01f && dNorm > 0.0f && dNorm < kFreqMeasMaxDeltaNorm) {
      const float fMeas =
          (float)(kFreqLockCyclesPerLoop * (dNorm / dt));
      if (!f.measValid) {
        f.measHz = fMeas;
        f.measValid = true;
      } else {
        f.measHz += kFreqEmaAlpha * (fMeas - f.measHz);
      }
    }
  }
  f.havePrev = true;
  f.lastNorm = norm;
  f.lastNormMs = now;

  // deviation-triggered correction (hysteresis: trigger at the threshold,
  // keep correcting through the band until the deviation drops below
  // threshold/2, then freeze)
  if (!g.autoFrequency || !f.measValid || f.measHz < 0.1f) return;
  const float th = g.freqDevThreshold;
  const float dev = fabsf(f.useHz - f.measHz) / f.measHz;
  if (dev > th) {
    f.devCount++;
    if (f.devCount >= kFreqDevSamples && !f.correcting) f.correcting = true;
  } else {
    f.devCount = 0;
  }
  // The correction step is gated by the correcting flag alone (not by
  // dev > th): once engaged it converges through the hysteresis band down to
  // threshold/2 so the final frequency MATCHES the measurement instead of
  // freezing at the trigger edge (4.7% for a 5% threshold).
  if (f.correcting) {
    f.useHz += (f.measHz - f.useHz) * kFreqCorrectionStep;
    if (fabsf(f.useHz - f.measHz) / f.measHz < th * 0.5f) f.correcting = false;
  }
}
