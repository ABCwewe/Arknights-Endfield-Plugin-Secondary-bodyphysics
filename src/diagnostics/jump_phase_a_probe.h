#pragma once
// Phase A: read-only Jump clip timeline.
// Producer contract: called only from the existing 20 Hz gait sample point.
// Observe() does not read Unity objects, allocate, block, log, or write files.
// Ring storage is allocated once by PluginStartup before hooks are installed.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "movement_signal_types.h"

static constexpr size_t kJumpPhaseAMaxClips = 8;
static constexpr size_t kJumpPhaseAMaxFrames = 1536;  // >60 s at 20 Hz
static constexpr DWORD kJumpPhaseACaptureMs = 60000;

struct JumpPhaseAClip {
  char name[128] = {0};
  float weight = 0.0f;
  float durationSec = 0.0f;
};

struct JumpPhaseAObservation {
  DWORD sampleMs = 0;
  bool pluginEnabled = true;
  bool phaseDActive = false;
  bool readOk = false;
  uint32_t readFailure = 0;
  int reportedCount = -1;
  size_t parsedCount = 0;
  bool truncated = false;
  char characterId[128] = {0};
  void *animator = nullptr;
  int cachedGait = GaitNone;
  bool cachedTransitionToIdle = false;
  bool cachedJump = false;
  bool cachedLanding = false;
  bool sampleJumpEvidence = false;
  bool sampleLandingEvidence = false;
  MovementSignalSample movement;
  JumpPhaseAClip clips[kJumpPhaseAMaxClips];
};

enum class JumpPhaseAState : uint32_t {
  Idle = 0,
  Capturing = 1,
  Sealed = 2,
  Flushed = 3,
};

class JumpPhaseAFlushSchedule {
public:
  bool Due(DWORD now, JumpPhaseAState state) const {
    if (state != JumpPhaseAState::Sealed) return false;
    if (attempts_ == 0) return true;
    return static_cast<int32_t>(now - nextAttemptMs_) >= 0;
  }

  void OnFailure(DWORD now) {
    nextAttemptMs_ = now + backoffMs_;
    if (backoffMs_ < 30000) {
      DWORD doubled = backoffMs_ * 2;
      backoffMs_ = doubled < 30000 ? doubled : 30000;
    }
    ++attempts_;
  }

  void Reset() {
    attempts_ = 0;
    nextAttemptMs_ = 0;
    backoffMs_ = 1000;
  }

private:
  uint32_t attempts_ = 0;
  DWORD nextAttemptMs_ = 0;
  DWORD backoffMs_ = 1000;
};

struct JumpPhaseAFrame {
  uint32_t serial = 0;
  DWORD sampleMs = 0;
  DWORD elapsedMs = 0;
  bool phaseDActive = false;
  bool readOk = false;
  uint32_t readFailure = 0;
  int reportedCount = -1;
  size_t parsedCount = 0;
  bool truncated = false;
  bool evidenceComplete = false;
  uint32_t failureStreak = 0;
  DWORD sampleAgeMs = UINT32_MAX;
  char characterId[128] = {0};
  void *animator = nullptr;
  int cachedGait = GaitNone;
  bool cachedTransitionToIdle = false;
  bool cachedJump = false;
  bool cachedLanding = false;
  bool sampleJump = false;
  bool sampleLanding = false;
  MovementSignalSample movement;
  bool hasNonFiniteWeight = false;
  size_t clipCount = 0;
  JumpPhaseAClip clips[kJumpPhaseAMaxClips];
};

class JumpPhaseATimeline {
public:
  bool Initialize() {
    if (frames_) return true;
    frames_ = static_cast<JumpPhaseAFrame *>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(JumpPhaseAFrame) * kJumpPhaseAMaxFrames));
    return frames_ != nullptr;
  }

  JumpPhaseAState State() const {
    return state_.load(std::memory_order_acquire);
  }

  size_t Count() const { return count_; }
  const JumpPhaseAFrame *Frames() const { return frames_; }

  // Returns true only when this observation was appended to the ring.
  bool Observe(const JumpPhaseAObservation &observation) {
    if (!frames_) return false;
    JumpPhaseAFrame frame;
    BuildFrame(observation, frame);

    JumpPhaseAState state = State();
    if (state == JumpPhaseAState::Idle) {
      const bool phaseDTrigger =
          observation.phaseDActive && frame.movement.identityValid &&
          frame.movement.entity != 0;
      if (observation.pluginEnabled || !observation.readOk ||
          (!frame.sampleJump && frame.evidenceComplete &&
           !phaseDTrigger) ||
          !observation.animator ||
          !observation.characterId[0])
        return false;
      Start(observation);
      state = JumpPhaseAState::Capturing;
    }

    if (state != JumpPhaseAState::Capturing) return false;
    if (observation.pluginEnabled || observation.animator != animator_ ||
        (entity_ && observation.movement.entity !=
                        reinterpret_cast<uintptr_t>(entity_)) ||
        strcmp(observation.characterId, characterId_) != 0) {
      Seal();
      return false;
    }
    if (count_ >= kJumpPhaseAMaxFrames) {
      Seal();
      return false;
    }

    frame.serial = static_cast<uint32_t>(count_ + 1);
    frame.elapsedMs = observation.sampleMs - startMs_;
    frame.failureStreak = observation.readOk ? 0 : failureStreak_ + 1;
    if (observation.readOk) {
      failureStreak_ = 0;
      lastReadOkMs_ = observation.sampleMs;
      hasLastReadOk_ = true;
      frame.sampleAgeMs = 0;
    } else {
      failureStreak_ = frame.failureStreak;
      frame.sampleAgeMs = hasLastReadOk_
                              ? observation.sampleMs - lastReadOkMs_
                              : UINT32_MAX;
    }
    frames_[count_++] = frame;

    if (frame.elapsedMs >= kJumpPhaseACaptureMs ||
        count_ >= kJumpPhaseAMaxFrames)
      Seal();
    return true;
  }

  void MarkFlushed() {
    JumpPhaseAState expected = JumpPhaseAState::Sealed;
    state_.compare_exchange_strong(expected, JumpPhaseAState::Flushed,
                                   std::memory_order_acq_rel);
  }

  void ResetForTest() {
    count_ = 0;
    startMs_ = 0;
    lastReadOkMs_ = 0;
    hasLastReadOk_ = false;
    failureStreak_ = 0;
    animator_ = nullptr;
    entity_ = nullptr;
    characterId_[0] = 0;
    state_.store(JumpPhaseAState::Idle, std::memory_order_release);
  }

private:
  static void BuildFrame(const JumpPhaseAObservation &observation,
                         JumpPhaseAFrame &frame) {
    frame.sampleMs = observation.sampleMs;
    frame.phaseDActive = observation.phaseDActive;
    frame.readOk = observation.readOk;
    frame.readFailure = observation.readFailure;
    frame.reportedCount = observation.reportedCount;
    frame.parsedCount = observation.parsedCount;
    frame.truncated = observation.truncated;
    frame.evidenceComplete =
        observation.readOk && !observation.truncated &&
        observation.reportedCount >= 0 &&
        observation.parsedCount ==
            static_cast<size_t>(observation.reportedCount);
    snprintf(frame.characterId, sizeof(frame.characterId), "%s",
             observation.characterId);
    frame.animator = observation.animator;
    frame.cachedGait = observation.cachedGait;
    frame.cachedTransitionToIdle = observation.cachedTransitionToIdle;
    frame.cachedJump = observation.cachedJump;
    frame.cachedLanding = observation.cachedLanding;

    if (!observation.readOk) return;
    frame.clipCount = observation.parsedCount < kJumpPhaseAMaxClips
                          ? observation.parsedCount
                          : kJumpPhaseAMaxClips;
    for (size_t i = 0; i < frame.clipCount; ++i) {
      frame.clips[i] = observation.clips[i];
      if (!std::isfinite(observation.clips[i].weight))
        frame.hasNonFiniteWeight = true;
    }
    frame.sampleJump = observation.sampleJumpEvidence;
    frame.sampleLanding = observation.sampleLandingEvidence;
    frame.movement = observation.movement;
  }

  void Start(const JumpPhaseAObservation &observation) {
    count_ = 0;
    startMs_ = observation.sampleMs;
    lastReadOkMs_ = 0;
    hasLastReadOk_ = false;
    failureStreak_ = 0;
    animator_ = observation.animator;
    entity_ = reinterpret_cast<void *>(observation.movement.entity);
    snprintf(characterId_, sizeof(characterId_), "%s",
             observation.characterId);
    state_.store(JumpPhaseAState::Capturing, std::memory_order_release);
  }

  void Seal() {
    JumpPhaseAState expected = JumpPhaseAState::Capturing;
    state_.compare_exchange_strong(expected, JumpPhaseAState::Sealed,
                                   std::memory_order_release,
                                   std::memory_order_relaxed);
  }

  std::atomic<JumpPhaseAState> state_{JumpPhaseAState::Idle};
  size_t count_ = 0;
  DWORD startMs_ = 0;
  DWORD lastReadOkMs_ = 0;
  bool hasLastReadOk_ = false;
  uint32_t failureStreak_ = 0;
  void *animator_ = nullptr;
  void *entity_ = nullptr;
  char characterId_[128] = {0};
  JumpPhaseAFrame *frames_ = nullptr;
};

// Single-TU Runtime/test instance. The animation callback is the sole producer;
// the service worker only reads it after State()==Sealed.
static JumpPhaseATimeline g_jumpPhaseATimeline;

static bool JumpPhaseACsvString(FILE *file, const char *value) {
  if (!file || fputc('"', file) == EOF) return false;
  const char *p = value ? value : "";
  while (*p) {
    if (*p == '\r' || *p == '\n') {
      if (fputc('\\', file) == EOF ||
          fputc(*p == '\r' ? 'r' : 'n', file) == EOF)
        return false;
      ++p;
      continue;
    }
    if (*p == '"' && fputc('"', file) == EOF) return false;
    if (fputc(*p++, file) == EOF) return false;
  }
  return fputc('"', file) != EOF;
}

static bool JumpPhaseACsvFloat(FILE *file, bool valid, float value) {
  if (valid && std::isfinite(value)) return fprintf(file, "%.9g", value) >= 0;
  return true;
}

static bool JumpPhaseAWriteMovementCsv(
    FILE *file, const MovementSignalSample &movement) {
  if (fprintf(file, ",%d,%u,%p,%p,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,",
              movement.identityValid ? 1 : 0, movement.resolutionMask,
              reinterpret_cast<void *>(movement.entity),
              reinterpret_cast<void *>(movement.movement),
              movement.moveModeValid ? 1 : 0, movement.moveMode,
              movement.actualGaitValid ? 1 : 0, movement.actualGait,
              movement.isMovingValid ? 1 : 0, movement.isMoving ? 1 : 0,
              movement.isMovingOnGroundValid ? 1 : 0,
              movement.isMovingOnGround ? 1 : 0,
              movement.isInAirValid ? 1 : 0, movement.isInAir ? 1 : 0,
              movement.velocityValid ? 1 : 0) < 0)
    return false;
  if (!JumpPhaseACsvFloat(file, movement.velocityValid, movement.velocity.x) ||
      fputc(',', file) == EOF ||
      !JumpPhaseACsvFloat(file, movement.velocityValid, movement.velocity.y) ||
      fputc(',', file) == EOF ||
      !JumpPhaseACsvFloat(file, movement.velocityValid, movement.velocity.z))
    return false;
  if (fprintf(file, ",%d,", movement.accelerationValid ? 1 : 0) < 0)
    return false;
  if (!JumpPhaseACsvFloat(file, movement.accelerationValid,
                          movement.acceleration.x) ||
      fputc(',', file) == EOF ||
      !JumpPhaseACsvFloat(file, movement.accelerationValid,
                          movement.acceleration.y) ||
      fputc(',', file) == EOF ||
      !JumpPhaseACsvFloat(file, movement.accelerationValid,
                          movement.acceleration.z))
    return false;
  if (fprintf(file, ",%d,", movement.fallingSpeedValid ? 1 : 0) < 0 ||
      !JumpPhaseACsvFloat(file, movement.fallingSpeedValid,
                          movement.fallingSpeed) ||
      fprintf(file, ",%d,%d\r\n", movement.teleportedValid ? 1 : 0,
              movement.teleportedThisFrame ? 1 : 0) < 0)
    return false;
  return true;
}

static bool JumpPhaseAWriteCsvRow(FILE *file, const JumpPhaseAFrame &frame,
                                  int clipIndex) {
  if (fprintf(file, "%u,%lu,%lu,", frame.serial,
              static_cast<unsigned long>(frame.elapsedMs),
              static_cast<unsigned long>(frame.sampleMs)) < 0)
    return false;
  if (!JumpPhaseACsvString(file, frame.characterId)) return false;
  if (fprintf(file, ",%p,%d,%d,%u,%d,%zu,%d,%d,%u,", frame.animator,
              frame.phaseDActive ? 1 : 0, frame.readOk ? 1 : 0,
              frame.readFailure,
              frame.reportedCount, frame.parsedCount,
              frame.truncated ? 1 : 0, frame.evidenceComplete ? 1 : 0,
              frame.failureStreak) < 0)
    return false;
  if (frame.sampleAgeMs == UINT32_MAX) {
    if (fputc(',', file) == EOF) return false;
  } else if (fprintf(file, "%lu,",
                     static_cast<unsigned long>(frame.sampleAgeMs)) < 0) {
    return false;
  }
  if (fprintf(file, "%d,%d,%d,%d,%d,%d,%d,", frame.cachedGait,
              frame.cachedTransitionToIdle ? 1 : 0,
              frame.cachedJump ? 1 : 0, frame.cachedLanding ? 1 : 0,
              frame.sampleJump ? 1 : 0, frame.sampleLanding ? 1 : 0,
              clipIndex) < 0)
    return false;

  if (clipIndex < 0) {
    if (fprintf(file, "\"\",,0,") < 0) return false;
    return JumpPhaseAWriteMovementCsv(file, frame.movement);
  }
  const JumpPhaseAClip &clip = frame.clips[clipIndex];
  if (!JumpPhaseACsvString(file, clip.name) || fputc(',', file) == EOF)
    return false;
  bool finiteWeight = std::isfinite(clip.weight);
  if (finiteWeight && fprintf(file, "%.9g", clip.weight) < 0) return false;
  if (fprintf(file, ",%d,", finiteWeight ? 1 : 0) < 0) return false;
  if (std::isfinite(clip.durationSec) &&
      fprintf(file, "%.9g", clip.durationSec) < 0)
    return false;
  return JumpPhaseAWriteMovementCsv(file, frame.movement);
}

// Worker-only persistence. The producer never calls this function.
static bool JumpPhaseAWriteCsvAtomic(const char *finalPath,
                                     const JumpPhaseATimeline &timeline) {
  if (!finalPath || !finalPath[0] ||
      timeline.State() != JumpPhaseAState::Sealed)
    return false;

  char tempPath[768];
  int pathLen = snprintf(tempPath, sizeof(tempPath), "%s.tmp", finalPath);
  if (pathLen <= 0 || static_cast<size_t>(pathLen) >= sizeof(tempPath))
    return false;

  FILE *file = fopen(tempPath, "wb");
  if (!file) return false;
  bool ok = fprintf(
                file,
                "serial,elapsed_ms,sample_ms,character_id,animator,phase_d_active,read_ok,"
                "read_failure,reported_count,parsed_count,truncated,"
                "evidence_complete,failure_streak,"
                "sample_age_ms,cached_gait,cached_transition_to_idle,"
                "cached_jump,cached_landing,sample_jump,sample_landing,"
                "clip_index,clip_name,clip_weight,clip_weight_finite,"
                "clip_duration_sec,movement_identity_valid,resolution_mask,"
                "entity,movement,move_mode_valid,move_mode,actual_gait_valid,"
                "actual_gait,is_moving_valid,is_moving,"
                "is_moving_on_ground_valid,is_moving_on_ground,"
                "is_in_air_valid,is_in_air,velocity_valid,velocity_x,"
                "velocity_y,velocity_z,acceleration_valid,acceleration_x,"
                "acceleration_y,acceleration_z,falling_speed_valid,"
                "falling_speed,teleported_valid,teleported_this_frame\r\n") >= 0;
  for (size_t i = 0; ok && i < timeline.Count(); ++i) {
    const JumpPhaseAFrame &frame = timeline.Frames()[i];
    if (frame.clipCount == 0) {
      ok = JumpPhaseAWriteCsvRow(file, frame, -1);
    } else {
      for (size_t clip = 0; ok && clip < frame.clipCount; ++clip)
        ok = JumpPhaseAWriteCsvRow(file, frame, static_cast<int>(clip));
    }
  }
  if (ferror(file)) ok = false;
  if (ok && fflush(file) != 0) ok = false;
  if (fclose(file) != 0) ok = false;
  if (!ok) return false;

  return MoveFileExA(tempPath, finalPath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
