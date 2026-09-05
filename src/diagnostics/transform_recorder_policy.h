#pragma once
// Pure recorder policy kept separate from Unity/Windows runtime access so the
// long-window and CSV contracts stay covered by the standalone verify suite.
#include <cstddef>
#include <cstdio>

static constexpr int kTransformRecorderMaxFrames = 7200;  // ~120s at 60fps
static constexpr const char *kTransformRecorderLatestFile =
    "developer\\breast_record.csv";
static constexpr const char *kTransformRecorderCsvHeader =
    "frame,time_ms,gait,target_valid,phase_rad,amp_env_deg,down_env_deg,"
    "freq_env_hz,synthetic_angle_deg,character_id,axis,axis_sign,"
    "preRx,preRy,preRz,preRw,preLx,preLy,preLz,preLw,"
    "targetRx,targetRy,targetRz,targetRw,targetLx,targetLy,targetLz,targetLw,"
    "actualRx,actualRy,actualRz,actualRw,actualLx,actualLy,actualLz,actualLw";

static inline bool TransformRecorderShouldAutoStop(int frames) {
  return frames >= kTransformRecorderMaxFrames;
}

static inline void BuildTransformRecorderFileName(
    char *out, size_t outSize, int year, int month, int day, int hour,
    int minute, int second, unsigned long sessionId) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize,
           "breast_record_%04d%02d%02d_%02d%02d%02d_%lu.csv", year, month,
           day, hour, minute, second, sessionId);
}