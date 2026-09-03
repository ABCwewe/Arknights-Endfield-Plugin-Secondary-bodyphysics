#pragma once
// Pure Phase-D signal types shared by Runtime and unit tests.
#include <cmath>
#include <cstdint>

enum MovementResolveBits : uint32_t {
  MovementResolveEntityField = 1u << 0,
  MovementResolveClass = 1u << 1,
  MovementResolveMoveMode = 1u << 2,
  MovementResolveActualGait = 1u << 3,
  MovementResolveIsMoving = 1u << 4,
  MovementResolveIsMovingOnGround = 1u << 5,
  MovementResolveIsInAir = 1u << 6,
  MovementResolveVelocity = 1u << 7,
  MovementResolveAcceleration = 1u << 8,
  MovementResolveFallingSpeed = 1u << 9,
  MovementResolveTeleported = 1u << 10,
};

struct MovementVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct MovementSignalSample {
  bool identityValid = false;
  uint32_t resolutionMask = 0;
  uintptr_t entity = 0;
  uintptr_t movement = 0;

  bool moveModeValid = false;
  int moveMode = -1;
  bool actualGaitValid = false;
  int actualGait = -1;
  bool isMovingValid = false;
  bool isMoving = false;
  bool isMovingOnGroundValid = false;
  bool isMovingOnGround = false;
  bool isInAirValid = false;
  bool isInAir = false;
  bool velocityValid = false;
  MovementVec3 velocity;
  bool accelerationValid = false;
  MovementVec3 acceleration;
  bool fallingSpeedValid = false;
  float fallingSpeed = 0.0f;
  bool teleportedValid = false;
  bool teleportedThisFrame = false;
};

static inline bool MovementVec3Finite(const MovementVec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static inline bool MovementSignalContinuousValid(
    const MovementSignalSample &sample) {
  return sample.identityValid && sample.movement != 0 &&
         sample.velocityValid && MovementVec3Finite(sample.velocity) &&
         sample.fallingSpeedValid && std::isfinite(sample.fallingSpeed);
}
