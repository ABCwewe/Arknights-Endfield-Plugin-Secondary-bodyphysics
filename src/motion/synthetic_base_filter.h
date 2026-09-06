#pragma once
// motion/synthetic_base_filter.h — synthetic-only stable composition base.
// The live native pose is low-pass filtered before the independent oscillator
// is composed, preventing near-frequency native twist from beating against it.
#include <cmath>
#include "../common/quat.h"
#include "../common/time_utils.h"

static constexpr float kSyntheticBaseFollowTauSec = 0.30f;

struct SyntheticBaseFilter {
  Quat baseR = QuatIdentity();
  Quat baseL = QuatIdentity();
  bool initialized = false;
  FrameGate gate;

  void Reset() {
    baseR = QuatIdentity();
    baseL = QuatIdentity();
    initialized = false;
    gate = FrameGate();
  }

  void Compose(Quat currentR, Quat currentL, int axis, float angleRad,
               float dt, Quat &outR, Quat &outL) {
    if (!initialized) {
      baseR = currentR;
      baseL = currentL;
      initialized = true;
    } else if (dt > 0.0f) {
      if (dt > 0.05f) dt = 0.05f;
      float follow = 1.0f - expf(-dt / kSyntheticBaseFollowTauSec);
      baseR = QuatSlerp(baseR, currentR, follow);
      baseL = QuatSlerp(baseL, currentL, follow);
    }

    Quat dq = QuatAxisAngle(axis, angleRad);
    outR = QuatMul(baseR, dq);
    outL = QuatMul(baseL, dq);
  }

  void ComposeLive(Quat currentR, Quat currentL, int axis, float angleRad,
                   Quat &outR, Quat &outL) {
    Compose(currentR, currentL, axis, angleRad, (float)gate.Tick(),
            outR, outL);
  }

  void ComposeDirect(Quat currentR, Quat currentL, int axis, float angleRad,
                     Quat &outR, Quat &outL) {
    Reset();
    Quat dq = QuatAxisAngle(axis, angleRad);
    outR = QuatMul(currentR, dq);
    outL = QuatMul(currentL, dq);
  }
};
