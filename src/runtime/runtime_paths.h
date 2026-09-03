#pragma once
// runtime/runtime_paths.h — resolve the SecondaryMotion data directory from
// the DLL's own module path (independent of the process working directory):
//   <game root>/plugin/sbm.dll  ->  <game root>/SecondaryMotion/
// V2 layout (architecture §1):
//   SecondaryMotion/
//     data/characters.default.json
//     presets/<name>.json
//     runtime/config.json, runtime_status.json, developer_command.json
//     developer/...
//     logs/ (reserved data directory; current Runtime logs remain in plugin/)
#include <windows.h>
#include <cstdio>
#include <cstring>

static char g_runtimeRoot[512] = {0};  // e.g. E:\...\Endfield Game\SecondaryMotion
static HMODULE g_hEiemModule = nullptr;  // set by DllMain (DLL_PROCESS_ATTACH)

// Test-only override (verify_tests.exe runs outside the plugin layout).
static void RuntimePathsSetRootForTest(const char *root) {
  snprintf(g_runtimeRoot, sizeof(g_runtimeRoot), "%s", root);
}

// Derive the SecondaryMotion root once from the EIEM DLL's own path.
// Returns false if the module path is unusable (plugin dir layout
// unexpected).
static bool RuntimePathsInit() {
  if (g_runtimeRoot[0]) return true;
  if (!g_hEiemModule) return false;  // DllMain must set it first
  char modulePath[512] = {0};
  DWORD n = GetModuleFileNameA(g_hEiemModule, modulePath,
                               sizeof(modulePath) - 64);
  if (n == 0 || n >= sizeof(modulePath) - 64) return false;
  // modulePath points at ...\plugin\sbm.dll -> strip file + "plugin\" dir
  char *slash = strrchr(modulePath, '\\');
  if (!slash) return false;
  *slash = 0;  // ...\plugin
  slash = strrchr(modulePath, '\\');
  if (!slash) return false;
  *slash = 0;  // <game root>
  snprintf(g_runtimeRoot, sizeof(g_runtimeRoot), "%s\\SecondaryMotion",
           modulePath);
  return g_runtimeRoot[0] != 0;
}

static void RuntimePath(char *out, size_t sz, const char *rel) {
  snprintf(out, sz, "%s\\%s", g_runtimeRoot, rel);
}

// Resolve a file in <game root>\\plugin from the already-derived
// <game root>\\SecondaryMotion root. Used only by legacy marker compatibility.
static bool RuntimePluginPath(char *out, size_t sz, const char *fileName) {
  if (!out || sz == 0 || !fileName || !RuntimePathsInit()) return false;
  char gameRoot[512];
  snprintf(gameRoot, sizeof(gameRoot), "%s", g_runtimeRoot);
  char *back = strrchr(gameRoot, '\\');
  char *forward = strrchr(gameRoot, '/');
  char *slash = back;
  if (forward && (!slash || forward > slash)) slash = forward;
  if (!slash) return false;
  *slash = 0;
  int written =
      snprintf(out, sz, "%s\\plugin\\%s", gameRoot, fileName);
  return written > 0 && (size_t)written < sz;
}

// Create the SecondaryMotion subdirectories (idempotent, startup only).
static void RuntimeDirsEnsure() {
  static const char *kDirs[] = {"", "\\data", "\\presets", "\\runtime",
                                "\\developer", "\\logs"};
  for (const char *d : kDirs) {
    char p[512];
    snprintf(p, sizeof(p), "%s%s", g_runtimeRoot, d);
    CreateDirectoryA(p, NULL);
  }
}
