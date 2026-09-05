#pragma once
// diagnostics/transform_recorder.h — long-window CSV recorder for locating
// native/synthetic composition, self-feedback, and post-write mismatch.
// Worker-triggered; Unity Transform reads remain on the animation callback.
#include <cstdio>
#include "../character/active_character.h"
#include "../common/logger.h"
#include "../common/safe_unity.h"
#include "../runtime/runtime_paths.h"
#include "transform_recorder_policy.h"

static FILE *g_recFile = nullptr;
static int g_recFrames = 0;
static char g_recSessionPath[512] = {0};

static void TransformRecorderClose(const char *reason) {
  if (!g_recFile) return;
  fflush(g_recFile);
  fclose(g_recFile);
  g_recFile = nullptr;

  char latestPath[512] = {0};
  RuntimePath(latestPath, sizeof(latestPath), kTransformRecorderLatestFile);
  BOOL copied = CopyFileA(g_recSessionPath, latestPath, FALSE);
  ProbeLog("[REC] %s frames=%d session=%s latest=%s copy=%d\n",
           reason ? reason : "stopped", g_recFrames, g_recSessionPath,
           latestPath, copied ? 1 : 0);
  g_recSessionPath[0] = 0;
}

static bool TransformRecorderEnsureInit(const ActiveCharacterRuntime &c) {
  (void)c;
  if (g_recFile) return true;
  if (!RuntimePathsInit()) {
    ProbeLog("[REC] start failed: runtime root unavailable\n");
    return false;
  }

  char dir[512] = {0};
  RuntimePath(dir, sizeof(dir), "developer");
  CreateDirectoryA(dir, nullptr);

  SYSTEMTIME st = {};
  GetLocalTime(&st);
  char fileName[128] = {0};
  BuildTransformRecorderFileName(
      fileName, sizeof(fileName), st.wYear, st.wMonth, st.wDay, st.wHour,
      st.wMinute, st.wSecond, static_cast<unsigned long>(GetTickCount()));
  snprintf(g_recSessionPath, sizeof(g_recSessionPath), "%s\\%s", dir,
           fileName);

  g_recFile = fopen(g_recSessionPath, "w");
  if (g_recFile) {
    fprintf(g_recFile, "%s\n", kTransformRecorderCsvHeader);
    g_recFrames = 0;
    ProbeLog("[REC] session=%s max_frames=%d\n", g_recSessionPath,
             kTransformRecorderMaxFrames);
    return true;
  }

  ProbeLog("[REC] start failed: cannot open %s\n", g_recSessionPath);
  g_recSessionPath[0] = 0;
  return false;
}

static void TransformRecorderFrame(const ActiveCharacterRuntime &c) {
  if (!g_recFile || !c.bones.breastR || !c.bones.breastL) return;
  if (TransformRecorderShouldAutoStop(g_recFrames)) {
    TransformRecorderClose("auto-stop");
    return;
  }

  const SyntheticRuntime &s = c.synthetic;
  Quat actualR = SafeGetLocalRotation(c.bones.breastR);
  Quat actualL = SafeGetLocalRotation(c.bones.breastL);
  fprintf(
      g_recFile,
      "%d,%lu,%d,%d,%.9f,%.6f,%.6f,%.6f,%.6f,%s,%d,%.3f,"
      "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
      "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
      "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
      g_recFrames, NowMs(), c.currentGait, s.targetValid ? 1 : 0, s.phase,
      RadToDeg(s.ampEnv), RadToDeg(s.downEnv), s.freqEnv,
      RadToDeg(s.outAngleRad), c.characterId, static_cast<int>(c.axis),
      c.axisSign,
      s.lastNativeR.x, s.lastNativeR.y, s.lastNativeR.z, s.lastNativeR.w,
      s.lastNativeL.x, s.lastNativeL.y, s.lastNativeL.z, s.lastNativeL.w,
      s.targetR.x, s.targetR.y, s.targetR.z, s.targetR.w,
      s.targetL.x, s.targetL.y, s.targetL.z, s.targetL.w,
      actualR.x, actualR.y, actualR.z, actualR.w,
      actualL.x, actualL.y, actualL.z, actualL.w);
  g_recFrames++;
  if ((g_recFrames % 300) == 0) {
    fflush(g_recFile);
    ProbeLog("[REC] frames=%d\n", g_recFrames);
  }
}

static void TransformRecorderStop() {
  TransformRecorderClose("marker-stop");
}
