# Gait Whitelist Architecture Upgrade

Date: 2026-08-22  
Status: implemented and smoke-tested in game  
Release packaging: not performed

## Purpose

This change replaces the permissive animation-name fallback with a fail-closed locomotion whitelist. Its main purpose is architectural: prevent unrelated animations from entering synthetic breast motion while preserving a controlled extension point for future character-specific movement clips.

## Default locomotion whitelist

The generic classifier accepts:

- Walk
- Run
- Sprint
- Zipline
- Normal Jump, using the existing Run classification and Jump configuration gate
- Explicit locomotion transitions such as `walk_start`, `run_stop`, `run_to_sprint`, and equivalent recognized transitions

The generic classifier rejects:

- Idle and Relax
- Generic Skill, Battle, Attack, story, emote, and unknown clips
- Special and Dash clips by default, even when their names also contain `run` or `sprint`
- Generic `start`, `stop`, and `_to_` clips that are not associated with recognized locomotion

A rejected clip becomes `GaitNone`. Its envelope may continue decaying internally, but the Runtime does not compose, write, or replay a synthetic bone target.

## Character-specific extension rules

Special movement clips can be enabled per stable `chr_id` in the technical character database:

```json
"animation_rules": {
  "real_special_movement_clip_fragment": "run",
  "real_special_dash_clip_fragment": "sprint"
}
```

Supported target gaits are:

```text
walk
run
sprint
zipline
```

Rules are loaded into the immutable configuration snapshot during database load or hot reload. Runtime animation callbacks perform only bounded in-memory string comparisons; they do not read files or parse JSON.

Rules have the following semantics:

- Character-specific rules run before the generic classifier.
- A rule can therefore opt a Special or Dash clip back into locomotion.
- If multiple fragments match, the longest fragment wins.
- A character may have at most 16 rules.
- An oversized rule set is rejected as a group and recorded in the Runtime configuration log instead of being silently truncated.
- No guessed rules were added for Lossi or any other character. Real clip names must be observed before adding a rule.

The Manager preserves `animation_rules` when rescanning a character or rewriting the character database.

## Jump-disabled write boundary

Normal Jump classification remains compatible with the previous Jump fix. When a Jump clip is detected and Jump is disabled:

- envelope state can still decay toward zero;
- no target is composed;
- no `SetLocalRotation` is performed;
- cached synthetic targets are invalidated and are not replayed by later callbacks in the same frame.

## Multi-clip policy

The Runtime intentionally keeps the existing low-latency policy:

> If any currently sampled clip is recognized as locomotion, locomotion remains active.

This avoids depending on the currently unreliable Animator weight values and avoids delaying normal Idle-to-Run crossfades.

Accepted limitation: during a Skill or story animation blend, a low-weight residual Run clip can briefly keep synthetic locomotion active until that Run clip disappears. Strictly rejecting every mixed set containing an unknown clip would also delay ordinary locomotion transitions, so that conservative policy was not adopted.

## Performance and scope

The update does not change:

- Animator sampling frequency (approximately 20 Hz);
- hook installation or callback scheduling;
- Quaternion composition mathematics;
- envelope equations or time constants;
- main-thread/write ownership;
- Runtime file-I/O boundaries.

The hot path adds at most a bounded set of `strstr` comparisons over already-loaded strings. It adds no file I/O, JSON parsing, sleeps, debounce windows, or per-frame allocations.

## Verification

Automated verification after review:

```text
90 PASS, 0 FAIL
Runtime build: PASS
Manager build: PASS (0 errors)
Manager DB rewrite preserves animation_rules: PASS
```

Manual in-game smoke test:

- Existing functionality showed no observed regression.
- Walk, Run, Sprint, Zipline, and existing motion behavior remained operational.
- The architectural extension cannot be fully exercised until a future character-specific Special or Dash clip is captured and configured.

## Files involved

```text
src/motion/gait_classifier.h
src/motion/gait_sampler.h
src/motion/motion_engine.h
src/config/config_types.h
src/config/config_loader.h
Manager/Services/CharacterDatabaseService.cs
verify_tests.cpp
```

The pre-existing local changes in `SecondaryMotion/presets/Default.json` are unrelated to this whitelist upgrade.
