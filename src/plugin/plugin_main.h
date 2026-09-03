#pragma once
// plugin/plugin_main.h — startup state machine + worker loop (spec §18.1).
//
//   STARTING -> CONFIG_LOADED -> SYMBOLS_RESOLVED -> HOOKS_INSTALLED -> READY
//   any required failure -> DISABLED_SAFE (NO WRITE)
//   optional hook missing  -> DEGRADED (PreLateTick continues)
//
// Worker responsibilities (low frequency only):
//   - marker state cache (legacy compatibility, spec §23)
//   - runtime_status.json updates
//   - diagnostics gates (bone scan / clip inspect / recorder / axis test)
//   - hook health summary
#include "../character/active_character.h"
#include "../common/logger.h"
#include "../config/config_loader.h"
#include "../config/config_validator.h"
#include "../diagnostics/axis_tester.h"
#include "../diagnostics/bone_scanner.h"
#include "../diagnostics/clip_inspector.h"
#include "../diagnostics/diagnostics_manager.h"
#include "../diagnostics/hook_health.h"
#include "../diagnostics/transform_recorder.h"
#include "../hooks/hook_manager.h"
#include "../il2cpp/animator_clip_reader.h"
#include "../il2cpp/metadata_resolver.h"
#include "../motion/gait_sampler.h"
#include "../motion/motion_engine.h"
#include "../runtime/dev_command.h"
#include "runtime_status.h"

// Legacy marker compatibility. The directory is derived from the loaded
// Runtime root instead of a machine-specific game installation path.
static bool MarkerPresent(const char *name) {
  char path[512];
  if (!RuntimePluginPath(path, sizeof(path), name)) return false;
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void JumpPhaseAFlushIfSealed() {
  static JumpPhaseAFlushSchedule s_flushSchedule;
  DWORD now = GetTickCount();
  if (!s_flushSchedule.Due(now, g_jumpPhaseATimeline.State())) return;
  char path[512];
  RuntimePath(path, sizeof(path),
              "developer\\jump_phase_d_timeline.csv");
  if (JumpPhaseAWriteCsvAtomic(path, g_jumpPhaseATimeline)) {
    g_jumpPhaseATimeline.MarkFlushed();
    s_flushSchedule.Reset();
    ProbeLog("[JUMP-A] timeline flushed rows=%zu path=%s\n",
             g_jumpPhaseATimeline.Count(), path);
  } else {
    s_flushSchedule.OnFailure(now);
    static DWORD s_jumpPhaseAFlushLogT = 0;
    if (RateLimit(s_jumpPhaseAFlushLogT, 2000))
      ProbeLog("[JUMP-A] timeline flush failed; sealed data retained\n");
  }
}

// ---- global service objects (single TU) ----
static RuntimeSymbols g_symbols;
static ActiveCharacterRuntime g_active;
static MarkerState g_markerState;
static AnimatorClipReader g_clipReader;
static GaitSampler g_sampler(g_clipReader);
static MotionEngine g_engine(g_sampler, g_active, g_markerState);
static HookManager g_hookManager;
static AxisTester g_axisTester;
static PluginState g_state = PluginState::STARTING;
static char g_lastError[256] = {0};
// shutdown_detach_v1 experiment: worker's IL2CPP attach handle, detached
// after startup completes (H1: permanent attach blocks game exit).
static void *g_attachedThread = nullptr;

// ---- startup ----
static bool PluginStartup() {
  Log("[PLUGIN] === secondary motion v1 startup ===");

  if (!ResolveIl2Cpp()) {
    Log("[PLUGIN] FAIL: IL2CPP API resolve -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "il2cpp resolve failed");
    return false;
  }
  Log("[OK] IL2CPP API resolved");
  // shutdown_detach_v1: detach symbol must resolve, else experiment INVALID.
  ProbeLog("[SHUTDOWN-DIAG] il2cpp_thread_detach resolved %s\n",
           il2cpp_thread_detach ? "PASS" : "FAIL");

  void *domain = il2cpp_domain_get();
  g_attachedThread = il2cpp_thread_attach(domain);
  ProbeLog("[SHUTDOWN-DIAG] worker attached ptr=%p\n",
           (void *)g_attachedThread);

  if (!ResolveCoreUnitySymbols()) {
    Log("[PLUGIN] FAIL: core Unity symbols -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "core symbols failed");
    return false;
  }

  // MinHook init (single thread at startup — baseline rule)
  MH_Initialize();

  // V2 data dirs + initial config install (data/characters.default.json +
  // presets/<active>.json + runtime/config.json revision).
  if (!RuntimePathsInit()) {
    Log("[PLUGIN] FAIL: SecondaryMotion dir unresolvable -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "runtime paths failed");
    return false;
  }
  RuntimeDirsEnsure();
  if (!g_jumpPhaseATimeline.Initialize()) {
    Log("[PLUGIN] FAIL: Jump Phase A ring allocation -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "Jump Phase A ring allocation failed");
    return false;
  }
  if (!ConfigInstallInitial(0)) {
    Log("[PLUGIN] FAIL: initial config invalid -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "%s", ConfigLastError());
    return false;
  }
  const ConfigSnapshot *cfg0 = ConfigAcquire();
  g_state = PluginState::CONFIG_LOADED;
  LoadDiagnosticsConfig();
  KnownCharactersInit();

  // Player-controller capture (optional; improves entity refresh)
  ResolvePlayerChainOffsets();
  InstallPlayerControllerHook();

  if (!ResolveRuntimeSymbols(g_symbols)) {
    Log("[PLUGIN] FAIL: required symbol PreLateTick unresolved -> "
        "DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "PreLateTick unresolved");
    return false;
  }
  g_state = PluginState::SYMBOLS_RESOLVED;

  // STRICT (V2 §36): the resolved field offset is the only production path.
  // ResolveRuntimeSymbols already fails the plugin when reflection fails.
  g_animatorFieldOffset = g_symbols.animatorFieldOffset;

  if (!g_hookManager.InstallAll(g_symbols, g_engine)) {
    Log("[PLUGIN] FAIL: hook install -> DISABLED_SAFE");
    snprintf(g_lastError, sizeof(g_lastError), "hook install failed");
    return false;
  }
  g_state = g_hookManager.Health().lateTickInstalled ||
                    g_hookManager.Health().syncCalcInstalled
                ? PluginState::READY
                : PluginState::DEGRADED;
  Log("[PLUGIN] %s", g_state == PluginState::READY ? "READY" : "DEGRADED");

  // Diagnostics main-thread hooks (no-op while unarmed/disabled).
  g_engine.diag.axisTest = [](const ActiveCharacterRuntime &c) { g_axisTester.Tick(c); };
  g_engine.diag.recorder = TransformRecorderFrame;
  g_engine.diag.boneScan = [](void *root) { BoneScannerRun(root, "breast", g_active.characterId); };
  if (g_diagCfg.clipInspector)
    g_engine.diag.clipInspect = [](void *animator) {
      ClipInspectorRun(g_clipReader, animator);
    };
  return true;
}

// ---- worker loop ----
static DWORD WINAPI PluginWorker(LPVOID) {
  while (!GetModuleHandleW(L"GameAssembly.dll")) Sleep(500);
  Sleep(3000);  // baseline: let the game settle before touching IL2CPP

  LogInit();
  ProbeLogInit();
  ProbeLog("[PROBE] secondary motion v1 started\n");

  bool startupOk = PluginStartup();
  ProbeLog("[SHUTDOWN-DIAG] startup complete ok=%d\n", startupOk ? 1 : 0);

  // ===== shutdown_detach_v1 experiment (single change) =====
  // Startup is done; the worker loop below only uses Win32/CRT/plain logic
  // (static audit passed: no il2cpp_* / Unity calls post-startup).  Detach
  // the worker from the IL2CPP runtime to test H1 (permanent attach blocks
  // game exit).
  if (g_attachedThread && il2cpp_thread_detach) {
    il2cpp_thread_detach(g_attachedThread);
    g_attachedThread = nullptr;
    ProbeLog("[SHUTDOWN-DIAG] worker IL2CPP DETACHED\n");
  } else {
    ProbeLog("[SHUTDOWN-DIAG] detach skipped (attached=%p detachFn=%p)\n",
             (void *)g_attachedThread, (void *)il2cpp_thread_detach);
  }
  // =========================================================

  if (!startupOk) {
    g_state = PluginState::DISABLED_SAFE;
    WriteRuntimeStatus(g_state, 0, g_active, g_lastError);
    ProbeLog("[SHUTDOWN-DIAG] worker entering service loop detached (disabled)\n");
    while (true) Sleep(5000);  // stay dormant, never write
  }

  ProbeLog("[SHUTDOWN-DIAG] worker entering service loop detached\n");
  static DWORD s_statusT = 0;
  while (true) {
    // ---- V2 developer command channel (Phase 5) ----
    // Manager writes runtime/developer_command.json; we parse it here (low
    // frequency) and only poke main-thread flags / tester state.
    DevCommandPoll();
    if (g_devCmd.active && strcmp(g_devCmd.type, "axis_test") == 0) {
      if (!g_axisTester.armed) g_axisTester.Arm();
    } else if (!g_devCmd.active) {
      // command cleared (none) — stop any manual test burst
      g_axisTester.armed = false;
    }
    if (DevCommandConsumeOneShot("bone_scan")) {
      g_engine.diag.boneScanPending = true;
      // ALWAYS override with the full-dump scanner (writes the bone_dumps
      // file); the default keyword scanner only logs and would never
      // produce a file the Manager can read.
      g_engine.diag.boneScan = [](void *root) {
        BoneScannerRun(root, nullptr, g_active.characterId);
      };
    }
    if (DevCommandConsumeOneShot("clip_inspect")) {
      g_engine.diag.clipInspect = [](void *animator) {
        ClipInspectorRun(g_clipReader, animator);
      };
    }
    if (DevCommandConsumeOneShot("record")) {
      if (g_devCmd.recordStart) {
        TransformRecorderEnsureInit(g_active);
        ProbeLog("[DEVCMD] recorder started\n");
      } else {
        TransformRecorderStop();
        ProbeLog("[DEVCMD] recorder stopped\n");
      }
    }

    // ---- V2 hot config poll (Phase 1) ----
    // Read runtime/config.json revision; on change, rebuild + swap the
    // snapshot.  Manager writes atomically (tmp + rename).
    {
      RuntimeConfigFile rc;
      if (LoadRuntimeConfigFile(rc)) {
        const ConfigSnapshot *cur = ConfigAcquire();
        if (!cur || cur->revision != rc.revision) {
          if (ConfigReloadIfChanged(rc.revision)) {
            g_lastError[0] = 0;
            Log("[CFG] hot apply revision=%d -> ACK", rc.revision);
          } else {
            snprintf(g_lastError, sizeof(g_lastError), "%s", ConfigLastError());
            Log("[CFG] hot apply revision=%d rejected (invalid): %s",
                rc.revision, ConfigLastError());
          }
        } else if (ConfigLastError()[0]) {
          // A transient malformed write was repaired without changing the
          // already-applied revision.
          ConfigClearLastError();
          g_lastError[0] = 0;
          Log("[CFG] runtime config recovered at revision=%d", rc.revision);
        }
      } else {
        snprintf(g_lastError, sizeof(g_lastError), "%s", ConfigLastError());
        Log("[CFG] runtime config poll rejected: %s", ConfigLastError());
      }
    }

    // marker cache refresh (debug compatibility only — V2 default off)
    g_markerState.spring = MarkerPresent("spring_test.txt");
    g_markerState.amplify = MarkerPresent("amplify_test.txt");

    static DWORD s_markerDiagT = 0;
    if (RateLimit(s_markerDiagT, 1000))
      ProbeLog("[MARKER] spring=%d amplify=%d\n",
               g_markerState.spring ? 1 : 0, g_markerState.amplify ? 1 : 0);

    // Diagnostics: arm main-thread hooks (the actual Unity API work runs on
    // PreLateTick via MotionEngine.diag — never on this worker thread).
    static bool s_boneScanArmed = false;
    static bool s_configRecorderStarted = false;
    static DWORD s_configRecorderRetryT = 0;
    if (g_diagCfg.enabled) {
      if (g_diagCfg.boneScanner && !s_boneScanArmed) {
        g_engine.diag.boneScanPending = true;
        s_boneScanArmed = true;
      }
      if (g_diagCfg.transformRecorder && !s_configRecorderStarted &&
          RateLimit(s_configRecorderRetryT, 2000)) {
        if (TransformRecorderEnsureInit(g_active)) {
          s_configRecorderStarted = true;
          ProbeLog("[DIAG] recorder started from diagnostics config\n");
        }
      }
      if (g_diagCfg.axisTester && !g_axisTester.armed) g_axisTester.Arm();
      if (g_diagCfg.hookHealth) HookHealthLog();
    } else {
      // legacy one-shot diagnostics markers (dev workflow compatibility)
      if (MarkerPresent("bone_scan_test.txt") && !s_boneScanArmed) {
        g_engine.diag.boneScanPending = true;
        s_boneScanArmed = true;
      }
      if (MarkerPresent("record_test.txt"))
        TransformRecorderEnsureInit(g_active);
      else
        TransformRecorderStop();
      if (MarkerPresent("axis_test.txt") && !g_axisTester.armed)
        g_axisTester.Arm();
      if (MarkerPresent("clip_inspect_test.txt"))
        g_engine.diag.clipInspect = [](void *animator) {
          ClipInspectorRun(g_clipReader, animator);
        };  // rate-limited inside
    }

    // Phase A ring is immutable after Sealed; only the worker persists it.
    JumpPhaseAFlushIfSealed();

    // runtime_status.json (~1s) + known-character collector flush (~2s)
    if (RateLimit(s_statusT, 1000))
      WriteRuntimeStatus(g_state, 0, g_active, g_lastError);
    static DWORD s_knownT = 0;
    if (RateLimit(s_knownT, 2000))
      KnownCharactersFlush();

    Sleep(250);
  }
  return 0;
}

static void PluginStart() {
  CreateThread(NULL, 0, PluginWorker, NULL, 0, NULL);
}
