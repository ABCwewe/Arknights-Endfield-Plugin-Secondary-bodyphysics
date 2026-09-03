#pragma once
// motion/jump_live_signal.h -- latest validated 20Hz Jump evidence handed
// from GaitSampler to the per-frame inertial source. Plain native data only.
#include <cstdint>
#include <cstddef>

struct JumpLiveSignal {
  uint32_t serial = 0;
  uint32_t sampleMs = 0;
  uintptr_t entity = 0;
  bool identityValid = false;
  bool clipReadValid = false;
  bool fallingSpeedValid = false;
  float fallingSpeed = 0.0f;
  bool teleported = false;
  bool jumpStartActive = false;
  bool landingActive = false;
};
