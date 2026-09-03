# Jump Phase A — existing clip timeline

Date: 2026-08-24
Scope: `D:\Project\EndfieldBreastMotion_test` only

## Purpose

Record the existing 20 Hz Layer-0 Animator clip observations needed to decide what the current Jump signal can and cannot prove before adding `MovementComponent`, a Jump FSM, an oscillator, or any bone output.

This Phase A build does not implement Jump motion.

## Runtime boundaries

- Reuses the existing `AnimatorClipReader::ReadLayer0()` call; no second Animator read is added.
- Adds no hook and does not change callback order, frame dedup, Compose, `SafeSetLocalRotation`, replay, AxisTester, or the legacy transform recorder.
- Adds no `MovementComponent` lookup or field access.
- The animation callback only copies the already-read clip result into a fixed-capacity memory timeline. Ring storage is allocated once during plugin startup, before hook installation.
- CSV I/O occurs on the existing worker and only after the timeline is sealed.
- Capture can start only while the global plugin switch is disabled.
- AxisTester, transform recorder, and developer commands must also remain disabled during the test. Phase A does not redesign their existing ownership.

## Automatic capture

There is no marker or start/stop state machine.

1. Start the game with the corrected test DLL and move to the test location normally.
2. Confirm all developer diagnostics are disabled; when ready to begin, disable the Manager global switch.
3. After the global switch is disabled, the first successfully sampled accepted Jump clip starts one capture. A successful truncated sample also starts capture, so a Jump hidden beyond the eight sampled clips cannot fail silently.
4. The triggering sample is retained.
5. Capture continues for 60 seconds at the existing nominal 20 Hz sample cadence.
6. Re-enabling the plugin or changing the active character seals the current capture early.
7. The worker writes the sealed timeline once. A second capture requires a game restart.

Output:

```text
<game root>\SecondaryMotion\developer\jump_phase_a_timeline.csv
```

## Recorded fields

Each sample/clip row contains:

- serial, absolute and elapsed milliseconds;
- character id and animator pointer;
- `read_ok` and exact read failure code;
- reported count, parsed count, and truncation;
- failure streak and age of the last successful read;
- cached gait, transition-to-idle, cached Jump and landing flags;
- Jump and landing evidence supplied by the existing profile-aware gait classification pass;
- clip index, name, weight, finite-weight flag, and duration.

The existing reader samples at most eight clips per poll. If Unity reports more than eight, the row records `truncated=1` and `evidence_complete=0`. Positive evidence among the sampled clips remains observable, but absence of Jump/Landing evidence is inconclusive. Phase A does not claim that a truncated set is complete and does not add a second Animator read.

A failed read is a distinct row and cannot create a fresh Jump edge. NaN/Inf weights are retained as invalid rather than silently treated as normal zero-weight clips.

## First game sequence

After entering the world:

1. Stand still for 2 seconds.
2. Perform one ordinary Jump to trigger capture.
3. Perform three standing Jumps.
4. Perform three running Jumps.
5. Perform three sprinting Jumps.
6. Without pressing Jump, walk or run off a small ledge once.
7. Stand still and leave the game running until at least 60 seconds after the trigger Jump.

The direct ledge drop is a negative control: the existing clip timeline may show landing or other transition clips, but it must not be interpreted as verified active takeoff evidence without a sampled accepted Jump start.

## Verification

Current local result:

```text
verify.bat: 145 passed, 0 failed
build.bat: Runtime and both proxy DLLs built successfully
```

Deployment and user testing remain blocked until the independent read-only review passes.
