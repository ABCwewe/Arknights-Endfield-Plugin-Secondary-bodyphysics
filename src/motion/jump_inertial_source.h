#pragma once
// motion/jump_inertial_source.h -- Phase E live Jump FSM.
//
// Pure native math: no Unity calls, allocation, file I/O, locks or sleeps.
// The caller supplies the newest validated 20Hz evidence and calls Tick once
// per Unity frame. The oscillator advances per frame with bounded substeps.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../config/config_types.h"
#include "jump_live_signal.h"

#ifndef DegToRad
static inline float JumpDegToRad(float degrees) {
  return degrees * 0.01745329251994329577f;
}
#else
static inline float JumpDegToRad(float degrees) { return DegToRad(degrees); }
#endif

// Character axes are calibrated with axis.sign=+1. Normalize Jump's visual
// up/down semantics without changing the sign used by ground locomotion.
static inline float AutomaticJumpDirection(Axis axis) {
  return axis == Axis::Z ? -1.0f : 1.0f;
}

enum class JumpInertialState {
  NativeOnly = 0,
  Armed = 1,
  Rising = 2,
  Apex = 3,
  Falling = 4,
  LandingTail = 5,
};

struct JumpInertialOutput {
  JumpInertialState state = JumpInertialState::NativeOnly;
  bool valid = false;
  float angleRad = 0.0f;
  float angularVelocityDegSec = 0.0f;
  float landingShakeAngleDeg = 0.0f;
  uint32_t eventEpoch = 0;
  bool acceptedStart = false;
  bool acceptedLanding = false;
  bool directLanding = false;
};

class JumpInertialSource {
public:
  JumpInertialOutput Tick(uint32_t nowMs, const JumpLiveSignal &signal,
                          const JumpConfig &tuning, bool enabled) {
    JumpInertialOutput out;
    if (!enabled || !TuningFiniteAndSafe(tuning) ||
        !SignalFiniteAndFresh(nowMs, signal)) {
      SyncInvalidSample(signal);
      return MakeOutput(tuning);
    }

    if (entity_ != 0 && entity_ != signal.entity) {
      // A character/entity epoch may not inherit a prior ticket or energy.
      SyncInvalidSample(signal);
      entity_ = signal.entity;
      return MakeOutput(tuning);
    }
    if (entity_ == 0) entity_ = signal.entity;

    const bool newSample = signal.serial != lastSerial_;
    if (newSample) UpdateContinuousSignal(signal, tuning);

    // Advance the state that existed over the preceding frame. Current sample
    // edges are processed afterwards, so impulses cannot rewrite the past.
    float dt = 0.0f;
    if (lastTickMs_ != 0) {
      dt = (float)(uint32_t)(nowMs - lastTickMs_) * 0.001f;
      if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
      if (dt > kMaxFrameDtSec) dt = kMaxFrameDtSec;
    }
    lastTickMs_ = nowMs;

    float targetDeg = 0.0f;
    if ((state_ == JumpInertialState::Armed ||
         state_ == JumpInertialState::Rising) &&
        stateElapsedSec_ >= tuning.takeoffDelaySec) {
      targetDeg = tuning.risingTargetDeg;
    } else if (state_ == JumpInertialState::Apex ||
               state_ == JumpInertialState::Falling) {
      targetDeg = tuning.apexFallingTargetDeg -
                  tuning.accelerationResponse * filteredAcceleration_;
    }
    const float omega = Omega(tuning);
    const float forcing = omega * omega * targetDeg;
    Integrate(dt, forcing, tuning);
    stateElapsedSec_ += dt;

    if (state_ == JumpInertialState::LandingTail) {
      const bool settled =
          stateElapsedSec_ >= tuning.maxDurationSec &&
          stateElapsedSec_ >= kTailMinSec &&
          std::fabs(angleDeg_) <= kSettleAngleDeg &&
          std::fabs(angularVelocityDegSec_) <= kSettleVelocityDegSec;
      if (settled || stateElapsedSec_ >= TailMaxSec(tuning)) CancelEpoch();
    } else if (state_ == JumpInertialState::Armed &&
               stateElapsedSec_ >= kArmTimeoutSec) {
      CancelEpoch();
    } else if ((state_ == JumpInertialState::Rising ||
                state_ == JumpInertialState::Apex ||
                state_ == JumpInertialState::Falling) &&
               stateElapsedSec_ >= kAirPhaseMaxSec) {
      // A jump whose ending clip never produced a landing edge (e.g. an air
      // attack that resolves in a clip whose name has no "land", such as
      // battle_air_atk_*) would otherwise hang the FSM forever and freeze the
      // chest at the converged rising/apex angle. Force-release the epoch.
      CancelEpoch();
    }

    if (newSample) {
      const bool startEdge = signal.jumpStartActive && !prevStartActive_;
      const bool landingEdge = signal.landingActive && !prevLandingActive_;
      prevStartActive_ = signal.jumpStartActive;
      prevLandingActive_ = signal.landingActive;
      lastSerial_ = signal.serial;

      if (startEdge) {
        ++eventEpoch_;
        out.acceptedStart = true;
        state_ = JumpInertialState::Armed;
        stateElapsedSec_ = 0.0f;
        angleDeg_ = 0.0f;
        angularVelocityDegSec_ = 0.0f;
      }

      if (state_ == JumpInertialState::Armed &&
          signal.fallingSpeed > kRisingEnterSpeed) {
        state_ = JumpInertialState::Rising;
        stateElapsedSec_ = 0.0f;
      } else if (state_ == JumpInertialState::Rising &&
                 signal.fallingSpeed <= kApexEnterSpeed) {
        state_ = JumpInertialState::Apex;
        stateElapsedSec_ = 0.0f;
      } else if (state_ == JumpInertialState::Apex &&
                 signal.fallingSpeed < kFallingEnterSpeed) {
        state_ = JumpInertialState::Falling;
        stateElapsedSec_ = 0.0f;
      }

      if (landingEdge) {
        if (state_ != JumpInertialState::NativeOnly) {
          out.acceptedLanding = true;
          float impact =
              tuning.landingImpulseGain * std::fabs(signal.fallingSpeed);
          if (!std::isfinite(impact)) {
            CancelEpoch();
            return MakeOutput(tuning);
          }
          angularVelocityDegSec_ -= impact;
          if (!std::isfinite(angularVelocityDegSec_)) {
            CancelEpoch();
            return MakeOutput(tuning);
          }
          state_ = JumpInertialState::LandingTail;
          stateElapsedSec_ = 0.0f;
        } else {
          out.directLanding = true;
        }
      }
    }

    out = MakeOutput(tuning, out.acceptedStart, out.acceptedLanding,
                     out.directLanding);
    return out;
  }

  void Reset() {
    state_ = JumpInertialState::NativeOnly;
    angleDeg_ = 0.0f;
    angularVelocityDegSec_ = 0.0f;
    filteredAcceleration_ = 0.0f;
    previousSpeed_ = 0.0f;
    havePreviousSpeed_ = false;
    stateElapsedSec_ = 0.0f;
    lastTickMs_ = 0;
    lastSignalMs_ = 0;
    lastSerial_ = 0;
    entity_ = 0;
    prevStartActive_ = false;
    prevLandingActive_ = false;
    eventEpoch_ = 0;
  }

  bool Active() const { return state_ != JumpInertialState::NativeOnly; }

private:
  // Lifecycle and numerical safety gates are deliberately not user-editable.
  static constexpr float kMaxFrameDtSec = 0.05f;
  static constexpr float kSubstepSec = 1.0f / 240.0f;
  static constexpr uint32_t kSignalStaleMs = 200;
  static constexpr float kRisingEnterSpeed = 0.5f;
  static constexpr float kApexEnterSpeed = 0.25f;
  static constexpr float kFallingEnterSpeed = -0.25f;
  static constexpr float kArmTimeoutSec = 0.30f;
  static constexpr float kAirPhaseMaxSec = 3.0f;  // upper bound on Rising/Apex/Falling
  static constexpr float kTailMinSec = 0.20f;
  static constexpr float kAbsoluteTailMaxSec = 5.0f;
  static constexpr float kSettleAngleDeg = 0.05f;
  static constexpr float kSettleVelocityDegSec = 0.20f;

  static bool TuningFiniteAndSafe(const JumpConfig &t) {
    return std::isfinite(t.amplitudeDeg) &&
           std::isfinite(t.dampingTauSec) && t.dampingTauSec > 0.0f &&
           std::isfinite(t.frequencyHz) && t.frequencyHz >= 0.0f &&
           std::isfinite(t.maxDurationSec) && t.maxDurationSec >= 0.0f &&
           std::isfinite(t.takeoffDelaySec) && t.takeoffDelaySec >= 0.0f &&
           std::isfinite(t.risingTargetDeg) &&
           std::isfinite(t.apexFallingTargetDeg) &&
           std::isfinite(t.accelerationResponse) &&
           std::isfinite(t.accelerationFilterTauSec) &&
           t.accelerationFilterTauSec > 0.0f &&
           std::isfinite(t.naturalFrequencyHz) &&
           t.naturalFrequencyHz > 0.0f && std::isfinite(t.dampingRatio) &&
           t.dampingRatio >= 0.0f && std::isfinite(t.landingImpulseGain) &&
           t.landingImpulseGain >= 0.0f;
  }

  static float Omega(const JumpConfig &t) {
    return 6.283185307179586f * t.naturalFrequencyHz;
  }

  static float TailMaxSec(const JumpConfig &t) {
    float seconds = t.maxDurationSec + 0.30f;
    if (seconds < kTailMinSec) seconds = kTailMinSec;
    if (seconds > kAbsoluteTailMaxSec) seconds = kAbsoluteTailMaxSec;
    return seconds;
  }

  float LandingShakeDeg(const JumpConfig &t) const {
    if (state_ != JumpInertialState::LandingTail ||
        stateElapsedSec_ < 0.0f || stateElapsedSec_ > t.maxDurationSec)
      return 0.0f;
    return t.amplitudeDeg *
           std::exp(-stateElapsedSec_ / t.dampingTauSec) *
           std::sin(6.283185307179586f * t.frequencyHz * stateElapsedSec_);
  }

  bool SignalFiniteAndFresh(uint32_t nowMs,
                            const JumpLiveSignal &s) const {
    if (!s.identityValid || !s.clipReadValid || !s.fallingSpeedValid ||
        s.teleported || s.entity == 0 || s.serial == 0 ||
        !std::isfinite(s.fallingSpeed))
      return false;
    return (uint32_t)(nowMs - s.sampleMs) <= kSignalStaleMs;
  }

  void UpdateContinuousSignal(const JumpLiveSignal &s,
                              const JumpConfig &tuning) {
    if (havePreviousSpeed_) {
      float sampleDt = (float)(uint32_t)(s.sampleMs - lastSignalMs_) * 0.001f;
      if (sampleDt > 0.0f && sampleDt <= 0.20f) {
        float raw = (s.fallingSpeed - previousSpeed_) / sampleDt;
        float alpha =
            1.0f - std::exp(-sampleDt / tuning.accelerationFilterTauSec);
        float next = filteredAcceleration_ +
                     alpha * (raw - filteredAcceleration_);
        filteredAcceleration_ = std::isfinite(raw) && std::isfinite(next)
                                    ? next
                                    : 0.0f;
      } else {
        filteredAcceleration_ = 0.0f;
      }
    } else {
      filteredAcceleration_ = 0.0f;
      havePreviousSpeed_ = true;
    }
    previousSpeed_ = s.fallingSpeed;
    lastSignalMs_ = s.sampleMs;
  }

  void Integrate(float dt, float forcing, const JumpConfig &tuning) {
    const float omega = Omega(tuning);
    float remain = dt;
    while (remain > 0.0f) {
      const float h = remain < kSubstepSec ? remain : kSubstepSec;
      const float acceleration =
          -2.0f * tuning.dampingRatio * omega * angularVelocityDegSec_ -
          omega * omega * angleDeg_ + forcing;
      angularVelocityDegSec_ += acceleration * h;
      angleDeg_ += angularVelocityDegSec_ * h;
      if (!std::isfinite(angleDeg_) ||
          !std::isfinite(angularVelocityDegSec_)) {
        CancelEpoch();
        return;
      }
      remain -= h;
    }
  }

  void CancelEpoch() {
    state_ = JumpInertialState::NativeOnly;
    angleDeg_ = 0.0f;
    angularVelocityDegSec_ = 0.0f;
    filteredAcceleration_ = 0.0f;
    stateElapsedSec_ = 0.0f;
  }

  void SyncInvalidSample(const JumpLiveSignal &s) {
    CancelEpoch();
    lastTickMs_ = 0;
    havePreviousSpeed_ = false;
    lastSignalMs_ = s.sampleMs;
    lastSerial_ = s.serial;
    entity_ = s.identityValid ? s.entity : 0;
    prevStartActive_ = s.jumpStartActive;
    prevLandingActive_ = s.landingActive;
  }

  JumpInertialOutput MakeOutput(const JumpConfig &tuning,
                                bool acceptedStart = false,
                                bool acceptedLanding = false,
                                bool directLanding = false) const {
    JumpInertialOutput out;
    out.state = state_;
    out.valid = state_ != JumpInertialState::NativeOnly;
    out.landingShakeAngleDeg = out.valid ? LandingShakeDeg(tuning) : 0.0f;
    float totalAngleDeg = angleDeg_ + out.landingShakeAngleDeg;
    if (!std::isfinite(totalAngleDeg) ||
        !std::isfinite(out.landingShakeAngleDeg)) {
      out.state = JumpInertialState::NativeOnly;
      out.valid = false;
      out.landingShakeAngleDeg = 0.0f;
      totalAngleDeg = 0.0f;
    }
    out.angleRad = out.valid ? JumpDegToRad(totalAngleDeg) : 0.0f;
    out.angularVelocityDegSec =
        out.valid ? angularVelocityDegSec_ : 0.0f;
    out.eventEpoch = eventEpoch_;
    out.acceptedStart = acceptedStart;
    out.acceptedLanding = acceptedLanding;
    out.directLanding = directLanding;
    return out;
  }

  JumpInertialState state_ = JumpInertialState::NativeOnly;
  float angleDeg_ = 0.0f;
  float angularVelocityDegSec_ = 0.0f;
  float filteredAcceleration_ = 0.0f;
  float previousSpeed_ = 0.0f;
  bool havePreviousSpeed_ = false;
  float stateElapsedSec_ = 0.0f;
  uint32_t lastTickMs_ = 0;
  uint32_t lastSignalMs_ = 0;
  uint32_t lastSerial_ = 0;
  uintptr_t entity_ = 0;
  bool prevStartActive_ = false;
  bool prevLandingActive_ = false;
  uint32_t eventEpoch_ = 0;
};
