# Static regression checks for cross-language UI/serialization wiring.
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parent


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


for page, prefix in [
    ("Manager/Pages/MainPage.xaml", "Current"),
    ("Manager/Pages/CharactersPage.xaml", "Selected"),
]:
    source = text(page)
    row = re.findall(
        r'Grid.Row="4"\s+Grid.Column="([12])"[\s\S]{0,400}?Text="\{Binding '
        + prefix
        + r'\.(ZiplineAmp|ZiplineDown)',
        source,
    )
    mapping = {column: binding for column, binding in row}
    require(mapping.get("1") == "ZiplineAmp", page + ": Up must bind ZiplineAmp")
    require(mapping.get("2") == "ZiplineDown", page + ": Down must bind ZiplineDown")

serializer = text("Manager/Services/CharacterDatabaseService.cs")
require(
    'string[] gnames2 = { "idle", "walk", "run", "sprint", "zipline" };'
    in serializer,
    "Character DB serializer must include zipline defaults",
)
require("for (int i = 0; i < 5; i++)" in serializer, "DB gait writer must emit five gaits")
require(
    ("if (wroteGait) sb.Append(\",\");" in serializer and
     "sb.Append(\"\\r\\n\");" in serializer) or
    '.Append(i < 4 ? "," : "").Append(Environment.NewLine)' in serializer,
    "DB gait writer must keep comma/CRLF boundaries so the block stays valid JSON",
)

status = text("src/plugin/runtime_status.h")
require('case 4: return "zipline";' in status, "runtime_status gait must name Zipline")

# Callback stacking is prevented by frame dedup, so legacy party/callback
# compensation must not remain as a dormant configuration or write factor.
for rel in [
    "src/config/config_types.h",
    "src/config/config_loader.h",
    "src/motion/motion_engine.h",
    "src/motion/synthetic_motion.h",
    "verify_tests.cpp",
]:
    source = text(rel)
    for legacy in [
        "PartyCompensationConfig",
        "partyCompensation",
        "LegacyCallbackStackCompensation",
        "party_compensator.h",
        "squadFactor",
        "sqCalls_",
    ]:
        require(legacy not in source, rel + ": legacy compensation remains: " + legacy)
require(
    not (ROOT / "src/motion/party_compensator.h").exists(),
    "legacy party_compensator.h must be removed",
)

# Per-character gait amplitudes are authoritative; legacy uniform scale fields
# must not survive in Runtime, Manager, or working JSON.
scale_files = [
    "src/config/config_types.h",
    "src/config/config_loader.h",
    "src/config/config_validator.h",
    "src/character/active_character.h",
    "src/character/bone_resolver.h",
    "src/motion/gait_sampler.h",
    "src/motion/synthetic_motion.h",
    "src/motion/jump_controller.h",
    "Manager/Models/CharacterData.cs",
    "Manager/ViewModels/CharactersViewModel.cs",
    "Manager/ViewModels/DeveloperViewModel.cs",
    "Manager/Services/AppCtx.cs",
    "Manager/Services/PresetService.cs",
    "Manager/Services/CharacterDatabaseService.cs",
    "Manager/Pages/MainPage.xaml",
    "Manager/Pages/CharactersPage.xaml",
    "verify_tests.cpp",
]
for rel in scale_files:
    source = text(rel)
    for legacy in ["bone_scale", "amplitude_scale", "amplitudeScale", "AmpScale", "boneAmplitudeScale", "ampScale"]:
        require(legacy not in source, rel + ": legacy scale remains: " + legacy)
for rel in [
    "SecondaryMotion/data/characters.default.json",
    "SecondaryMotion/presets/Default.json",
    "SecondaryMotion/presets/Set1.json",
    "SecondaryMotion/presets/User.json",
]:
    source = text(rel)
    require("bone_scale" not in source, rel + ": bone_scale remains")
    require("amplitude_scale" not in source, rel + ": amplitude_scale remains")

loader = text("src/config/config_loader.h")
require('#include "config_validator.h"' in loader,
        "config loader must own final validation for startup and hot reload")
require("if (!ValidateSnapshot(snap))" in loader,
        "snapshot must be rejected by final validator before install")
plugin = text("src/plugin/plugin_main.h")
require("ConfigLastError()" in plugin,
        "rejected config reason must reach runtime_status error")
require("s_configRecorderStarted" in plugin,
        "diagnostics recorder must have a one-shot config start latch")
require('g_recFile || MarkerPresent("record_test.txt")' not in plugin,
        "diagnostics recorder must not require an already-open file or marker")
recorder = text("src/diagnostics/transform_recorder.h")
require('developer\\\\breast_record.csv' in recorder,
        "recorder output must use the runtime developer directory")
require('plugin/breast_record.csv' not in recorder,
        "recorder must not depend on process working directory")
require("kMarkerDir" not in plugin,
        "legacy marker directory must not be hard-coded")
require("RuntimePluginPath" in plugin,
        "legacy markers must resolve through the runtime-derived game plugin path")
config_types = text("src/config/config_types.h")
require("BoneFamilyDefaults" not in config_types,
        "unused legacy bone-family scale helper must be removed")

preset_service = text("Manager/Services/PresetService.cs")
require("IsValidPresetName(name)" in preset_service and
        "if (IsValidPresetName(name)) names.Add(name);" in preset_service,
        "preset list must filter package templates and Runtime-invalid names")
require("IsValidPresetName(name) && File.Exists(PathOf(name))" in preset_service,
        "preset selection must require a valid existing formal preset")

jump_migration = text("Manager/Services/JumpDefaultsMigration.cs")
require("PresetJumpNeedsOfficialDefaults" in jump_migration,
        "Jump migration must distinguish legacy defaults from customized Jump data")
require("if (!PresetJumpNeedsOfficialDefaults(targetEntry[\"jump\"], genericJump))" in jump_migration,
        "Jump migration must preserve an existing customized Jump block")

hotfix_path = ROOT / "Manager/Services/JumpDefaultsV301Hotfix.cs"
require(hotfix_path.exists(),
        "3.0.1 must carry the one-shot Default Jump correction")
hotfix = text("Manager/Services/JumpDefaultsV301Hotfix.cs")
require(".jump_defaults_v3_0_1_hotfix" in hotfix,
        "3.0.1 Default correction needs its own one-shot marker")
require("JsonNode.DeepEquals(currentJump, badJump)" in hotfix,
        "3.0.1 must replace only an exact 3.0.0 bad Jump object")
require("Default.v3.0.0-bad.template.json" in hotfix,
        "3.0.1 must compare against the frozen 3.0.0 bad table")
require("Default.v3.0.0-bad.template.json" in text("assemble_one.bat"),
        "release package must carry the frozen 3.0.0 comparison table")

config_service = text("Manager/Services/ConfigService.cs")
require('Write(NextRevision(), "Default", Enabled);' in config_service,
        "startup preset recovery must preserve enabled and advance revision")

appctx = text("Manager/Services/AppCtx.cs")
require("RecoverInvalidActivePreset(Presets)" in appctx,
        "Manager startup must repair stale invalid active presets")
require("if (!Presets.IsSelectablePreset(name))" in appctx,
        "SwitchPreset must reject invalid or missing preset names before writing config")

# Jump Phase A is a read-only observer at the existing 20 Hz gait seam.
reader = text("src/il2cpp/animator_clip_reader.h")
require("struct ClipReadStatus" in reader,
        "Phase A requires explicit read/reported/parsed/truncated status")
require("ClipReadStatus *status" in reader,
        "ReadLayer0 must expose optional status without adding a second read")
gait_sampler = text("src/motion/gait_sampler.h")
require('#include "../diagnostics/jump_phase_a_probe.h"' in gait_sampler,
        "Phase A probe must attach only at the existing gait sample seam")
require("JumpPhaseAObserveReadFailure" in gait_sampler,
        "Phase A must retain reader failures during an active capture")
require("JumpPhaseAObserveClips" in gait_sampler,
        "Phase A must receive the same successful clip set as the classifier")
require("active.jumpDetected, landing" in gait_sampler,
        "Phase A must consume the existing profile-aware Jump/Landing result")
require('#include "../diagnostics/movement_signal_probe.h"' in gait_sampler,
        "Phase D provider must attach only at the existing gait sample seam")
require("liveJumpSignalNeeded" in gait_sampler and
        "cfg.pluginEnabled && active.profile && active.profile->jump.enabled" in gait_sampler and
        'active.profile->jump.mode == "landing_damped"' in gait_sampler and
        "if (!cfg.pluginEnabled || liveJumpSignalNeeded)" in gait_sampler and
        "PhaseDEntityAnimatorPairValid(active)" in gait_sampler,
        "Movement sampling while production is enabled must require the exact live Jump gate")
plugin = text("src/plugin/plugin_main.h")
require("JumpPhaseAFlushIfSealed" in plugin,
        "Phase A sealed timeline must flush on the worker")
require("g_jumpPhaseATimeline.Initialize()" in plugin,
        "Phase A ring must allocate before the animation hook is installed")
require(plugin.index("g_jumpPhaseATimeline.Initialize()") <
        plugin.index("g_hookManager.InstallAll"),
        "Phase A ring allocation must precede animation hook installation")
require("developer\\\\jump_phase_d_timeline.csv" in plugin,
        "Phase D output must use the runtime developer directory")
for rel in [
    "src/motion/motion_engine.h",
    "src/diagnostics/axis_tester.h",
    "src/diagnostics/transform_recorder.h",
]:
    require("JumpPhaseA" not in text(rel),
            rel + ": Phase A must not alter motion/writer architecture")
probe = text("src/diagnostics/jump_phase_a_probe.h")
require("JumpPhaseAFrame frames_[" not in probe,
        "Phase A ring must not inflate the DLL static data section")
observe_body = probe.split("bool Observe(", 1)[1].split("void MarkFlushed", 1)[0]
for forbidden in ["fopen", "fprintf", "fflush", "fclose", "MoveFileEx", "Sleep("]:
    require(forbidden not in observe_body,
            "Phase A producer hot path contains forbidden call: " + forbidden)
movement_probe = text("src/diagnostics/movement_signal_probe.h")
for forbidden in ["il2cpp_runtime_invoke", "Invoke(", "il2cpp_type_get_name",
                  "fopen", "fprintf", "Sleep("]:
    require(forbidden not in movement_probe,
            "Phase D continuous provider contains forbidden call: " + forbidden)
require("MovementFindExactField" in movement_probe and
        "il2cpp_class_from_type" in movement_probe,
        "Phase D fields require current-runtime exact metadata type validation")

# Phase E inertial intent must not add a parallel Unity write path or
# callback-side I/O/allocation. All visual tuning is per-character config;
# numerical/lifecycle safety gates remain internal and non-editable.
jump_source = text("src/motion/jump_inertial_source.h")
for forbidden in ["SafeSetLocalRotation", "SafeGetLocalRotation", "fopen",
                  "fprintf", "Sleep(", "new ", "malloc(", "Invoke("]:
    require(forbidden not in jump_source,
            "Phase E source contains forbidden hot-path operation: " + forbidden)
JUMP_JSON_KEYS = [
    "enabled", "mode", "amplitude_deg", "damping_tau_sec", "frequency_hz",
    "max_duration_sec", "takeoff_delay_sec", "rising_target_deg",
    "apex_falling_target_deg", "acceleration_response",
    "acceleration_filter_tau_sec", "natural_frequency_hz", "damping_ratio",
    "landing_impulse_gain",
]
JUMP_MODEL_FIELDS = [
    "JumpEnabled", "JumpAmplitude", "JumpDampingTau", "JumpFrequency",
    "JumpMaxDuration", "JumpTakeoffDelay", "JumpRisingTarget",
    "JumpApexFallingTarget", "JumpAccelerationResponse",
    "JumpAccelerationFilterTau", "JumpNaturalFrequency", "JumpDampingRatio",
    "JumpLandingImpulseGain",
]

config_types = text("src/config/config_types.h")
for field in [
    "takeoffDelaySec", "risingTargetDeg", "apexFallingTargetDeg",
    "accelerationResponse", "accelerationFilterTauSec", "naturalFrequencyHz",
    "dampingRatio", "landingImpulseGain",
]:
    require(field in config_types, "JumpConfig missing field: " + field)
for key in JUMP_JSON_KEYS[2:]:
    require('"' + key + '"' in loader, "config loader missing Jump key: " + key)

require("const JumpConfig &tuning" in jump_source and
        "tuning.takeoffDelaySec" in jump_source and
        "tuning.risingTargetDeg" in jump_source and
        "tuning.apexFallingTargetDeg" in jump_source and
        "tuning.accelerationResponse" in jump_source and
        "tuning.accelerationFilterTauSec" in jump_source and
        "t.naturalFrequencyHz" in jump_source and
        "tuning.dampingRatio" in jump_source and
        "tuning.landingImpulseGain" in jump_source and
        "t.amplitudeDeg" in jump_source and
        "t.frequencyHz" in jump_source and
        "t.dampingTauSec" in jump_source and
        "t.maxDurationSec" in jump_source and
        "kTakeoffImpulseDegSec" not in jump_source and
        "kRisingTargetDeg" not in jump_source and
        "kApexFallingTargetDeg" not in jump_source and
        "kLandingShakeAmplitudeDeg" not in jump_source and
        "kMaxAngleDeg" not in jump_source and
        "kMaxAngularVelocityDegSec" not in jump_source and
        "kLandingImpulseLimitDegSec" not in jump_source and
        "kSignalStaleMs = 200" in jump_source and
        "kSubstepSec = 1.0f / 240.0f" in jump_source,
        "Phase E diagnostic constants/clamp removal or safety gates regressed")
motion_engine = text("src/motion/motion_engine.h")
require("jumpInertial_.Tick" in motion_engine and
        "active_.profile->jump" in motion_engine and
        "SyntheticMotion::ComposeTargets" in motion_engine and
        "active_.synthetic.targetValid = false" in motion_engine,
        "Phase E intent must reuse MotionEngine Compose and fail-closed target gate")
require("PublishJumpLiveSignal" in gait_sampler and
        'strstr(clips[i].name, "jump_start")' in gait_sampler,
        "Phase E start ticket must come from exact accepted jump_start evidence")

# Manager/UI/serialization: Jump tuning appears only on Main, while all fields
# round-trip through both preset and technical-DB writers. Developer onboarding
# copies the full Aurora template instead of only the enable bit.
model = text("Manager/Models/CharacterData.cs")
wrapper = text("Manager/ViewModels/CharactersViewModel.cs")
main_xaml = text("Manager/Pages/MainPage.xaml")
characters_xaml = text("Manager/Pages/CharactersPage.xaml")
db_service = text("Manager/Services/CharacterDatabaseService.cs")
developer_vm = text("Manager/ViewModels/DeveloperViewModel.cs")
converters = text("Manager/ViewModels/Converters.cs")
json_mini = text("Manager/Services/JsonMini.cs")
VISIBLE_JUMP_BINDINGS = [
    "JumpEnabled", "JumpAmplitude", "JumpApexFallingTarget",
    "JumpAccelerationResponse", "JumpNaturalFrequency", "JumpDampingRatio",
    "JumpLandingImpulseGain",
]
HIDDEN_JUMP_BINDINGS = [
    "JumpDampingTau", "JumpFrequency", "JumpMaxDuration", "JumpTakeoffDelay",
    "JumpAccelerationFilterTau", "JumpRisingTarget",
]
for field in JUMP_MODEL_FIELDS:
    require(field in model or field == "JumpEnabled", "CharacterData missing " + field)
    require(field in wrapper, "CharacterItem missing " + field)
    require(("Selected." + field) not in characters_xaml,
            "Jump controls must not appear on Characters page: " + field)
for field in VISIBLE_JUMP_BINDINGS:
    require("Current." + field in main_xaml,
            "Main Jump panel missing visible binding " + field)
for field in HIDDEN_JUMP_BINDINGS:
    require(("Current." + field) not in main_xaml,
            "Main Jump panel must hide direct binding " + field)
require("Current.JumpRisingMagnitude" in main_xaml and
        "Data.JumpRisingTarget = -Math.Abs(value)" in wrapper,
        "Main Rising target must keep a non-editable negative direction")
require("Current.JumpShakePresetIndex" in main_xaml and
        "IsSnapToTickEnabled=\"True\"" in main_xaml and
        all(group in wrapper for group in [
            "(0.30, 2.50, 1.60)", "(0.55, 2.00, 3.00)",
            "(0.50, 2.50, 2.80)", "(0.57, 2.25, 3.11)",
            "(0.70, 2.50, 4.40)",
        ]),
        "Main landing shake slider must map the five approved parameter groups")
require("C_jump_legacy_landing_damped" not in characters_xaml,
        "Characters page legacy Jump checkbox must stay removed")
for key in JUMP_JSON_KEYS:
    require('"' + key + '"' in db_service,
            "shared Jump round-trip missing key: " + key)
require("CharacterDatabaseService.ApplyJump(jump, c)" in preset_service and
        "CharacterDatabaseService.JumpEntryJson(c)" in preset_service,
        "Preset service must use shared Jump load/write round-trip")
require("double.IsFinite(d)" in converters,
        "numeric TextBox converter must reject NaN/Infinity")
require("double.IsFinite(v)" in json_mini,
        "JSON number writer must reject NaN/Infinity")
require('ToString("R", CultureInfo.InvariantCulture)' in converters and
        'ToString("R", CultureInfo.InvariantCulture)' in json_mini,
        "Manager numeric display/writer must preserve round-trip precision")
for field in JUMP_MODEL_FIELDS:
    require("data." + field + " = aurora." + field in developer_vm or
            field == "JumpEnabled" and "data.JumpEnabled = aurora.JumpEnabled" in developer_vm,
            "Developer new-character template missing " + field)

for rel in ["SecondaryMotion/data/characters.default.json",
            "SecondaryMotion/presets/Default.json",
            "SecondaryMotion/presets/Set1.json",
            "SecondaryMotion/presets/User.json"]:
    root = json.loads(text(rel))
    chars = root.get("characters", {})
    if not chars:
        continue
    for cid, node in chars.items():
        jump = node["defaults"]["jump"] if "defaults" in node else node["jump"]
        require(set(JUMP_JSON_KEYS).issubset(jump.keys()),
                rel + ": incomplete Jump block for " + cid)
        require(jump["enabled"] is True and jump["mode"] == "landing_damped",
                rel + ": Jump not enabled for " + cid)

print("PASS wiring/removal/config-validation/recorder/preset/Phase-E checks")
