#pragma once
// motion/locomotion_envelope.h — amplitude/frequency envelope with phase
// integration (baseline review #11 semantics, user-confirmed "效果非常好"):
//   desiredAngle(t) = ampEnv(t) * sin(phase(t))
//   amp env: exponential approach, tau from profile (0.15s / 0.015s to-idle)
//   freq env: exponential approach, tau 0.20s
//   phase:   phase += 2π * freqEnv * dt; fmod 2π  — NEVER reset on gait change
#include <cmath>
#include "../common/time_utils.h"
#include "../config/config_types.h"

static constexpr float kLocomotionOnsetAttackTauSec = 0.10f;

// PLL phase tracking: convergence time constant (phase pulled toward the
// clip-phase reference; 95% convergence in ~3x tau) and the amplitude below
// which an initial hard snap is invisible (startup/character-switch align).
static constexpr double kPhaseLockTauSec = 0.5;
static constexpr float kPhaseSnapAmpThresholdRad = 0.02f;

// Shortest-path wrapped error to the reference in [-pi, pi).
static inline double WrapPhaseError(double phase, double ref) {
  double err = fmod(ref - phase, 2.0 * 3.14159265358979);
  if (err > 3.14159265358979) err -= 2.0 * 3.14159265358979;
  else if (err < -3.14159265358979) err += 2.0 * 3.14159265358979;
  return err;
}

// Selects an internal fast attack only for a newly accepted ground-locomotion
// epoch. State lives with ActiveCharacterRuntime so switch/reload resets it;
// this parameter is deliberately not part of the user preset/UI schema.
static float SelectLocomotionAttackTau(
    bool groundMoving, float ampNow, float downNow, float ampTarget,
    float downTarget, float normalAttackTau, bool &wasGroundMoving,
    bool &onsetActive) {
  if (!groundMoving) {
    wasGroundMoving = false;
    onsetActive = false;
    return normalAttackTau;
  }
  if (!wasGroundMoving) onsetActive = true;
  wasGroundMoving = true;
  if (onsetActive) {
    float ampTolerance = fmaxf(0.001f, fabsf(ampTarget) * 0.05f);
    float downTolerance = fmaxf(0.001f, fabsf(downTarget) * 0.05f);
    if (fabsf(ampTarget - ampNow) <= ampTolerance &&
        fabsf(downTarget - downNow) <= downTolerance)
      onsetActive = false;
  }
  return onsetActive ? kLocomotionOnsetAttackTauSec : normalAttackTau;
}

struct LocomotionEnvelope {
  float ampEnv = 0.0f;
  float downEnv = 0.0f;   // down-amplitude channel (asymmetric gait)
  float freqEnv = 1.5f;
  double phase = 0.0;
  double phaseRef = 0.0;          // PLL tracking target ([0,2pi))
  double phaseLockTauSec = 0.0;   // >0 -> bounded pull toward phaseRef
  FrameGate gate;  // frame-once advance (multi-instance safe)

  // PLL phase tracking: while valid, every Advance pulls the continuous
  // phase toward `ref` with an exponential time constant (shortest wrapped
  // path).  The phase is never snapped while tracking — only a smooth,
  // bounded convergence, so amplitude/frequency/phase all transition
  // smoothly between gaits.
  void SetPhaseTracking(bool valid, double ref) {
    phaseRef = ref;
    phaseLockTauSec = valid ? kPhaseLockTauSec : 0.0;
  }
  bool PhaseTrackingActive() const { return phaseLockTauSec > 0.0; }

  // Advance the envelope.  Returns true when a frame actually advanced.
  bool Advance(float ampTarget, float downTarget, float freqTarget,
               bool toIdle, float attackTauSec, float toIdleTauSec,
               float freqTauSec) {
    float dt = gate.Tick();
    if (dt <= 0.0) return false;
    float ampTau = toIdle ? toIdleTauSec : attackTauSec;
    float kA = 1.0f - expf(-dt / ampTau);
    float kF = 1.0f - expf(-dt / freqTauSec);
    ampEnv += (ampTarget - ampEnv) * kA;
    downEnv += (downTarget - downEnv) * kA;
    freqEnv += (freqTarget - freqEnv) * kF;
    phase += 2.0 * 3.14159265358979 * freqEnv * dt;
    if (phaseLockTauSec > 0.0)
      phase += WrapPhaseError(phase, phaseRef) *
               (1.0 - exp(-dt / phaseLockTauSec));
    phase = fmod(phase, 2.0 * 3.14159265358979);
    return true;
  }

  float Amplitude() const { return ampEnv; }
  float DownAmplitude() const { return downEnv; }
  float Frequency() const { return freqEnv; }
  float Phase() const { return (float)phase; }

  // Anchor the oscillator phase to an external reference (locomotion-clip
  // normalizedTime derived phase).  Caller must snap at ampEnv ~ 0 onset so
  // the phase jump is invisible.  Keeps the phase in [0, 2pi) invariant.
  void SetPhase(double ph) {
    phase = fmod(ph, 2.0 * 3.14159265358979);
    if (phase < 0.0) phase += 2.0 * 3.14159265358979;
  }

  void Reset() {
    ampEnv = 0.0f;
    downEnv = 0.0f;
    freqEnv = 1.5f;
    phase = 0.0;
    phaseRef = 0.0;
    phaseLockTauSec = 0.0;
    gate = FrameGate();
  }
};
