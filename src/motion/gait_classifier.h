#pragma once
// motion/gait_classifier.h — pure clip-name gait classification.
// V1 keeps the EXACT baseline classification semantics (architecture spec
// §9.3): priority jump > _to_ (by target) > start > stop > main loops.
#include <cstring>
#include "../config/config_types.h"

struct GaitClassification {
  int gait = GaitNone;          // Gait enum
  bool transitionToIdle = false;
  bool jumpDetected = false;
  bool landingDetected = false;
};

static bool IsLocomotionGait(int gait) {
  return gait >= GaitWalk && gait <= GaitZipline;
}

// Fast onset is limited to accepted ground locomotion. Zipline keeps its
// existing attack smoothing; stop/to-idle and Jump never arm onset.
static bool IsGroundLocomotionOnsetEligible(int gait, bool transitionToIdle,
                                             bool jumpDetected) {
  return gait >= GaitWalk && gait <= GaitSprint && !transitionToIdle &&
         !jumpDetected;
}

static bool CanWriteSynthetic(int gait, bool jumpDetected,
                              bool jumpEnabled) {
  if (!IsLocomotionGait(gait)) return false;
  if (jumpDetected && !jumpEnabled) return false;
  return true;
}

// Global locomotion whitelist.  Unknown clips fail closed (GaitNone); there
// is deliberately no permanent blacklist for "special", "dash", etc. so
// character-specific animation rules can opt those clips in later.
static int ClassifyGlobalLocomotionClip(const char *name) {
  if (!name) return GaitNone;
  // Default policy: special/dash clips are not generic locomotion. They are
  // still opt-in capable because character rules run before this function.
  if (strstr(name, "special") || strstr(name, "dash")) return GaitNone;
  if (strstr(name, "zipline")) return GaitZipline;
  if (strstr(name, "jump")) return GaitRun;  // existing Jump gate semantics

  if (strstr(name, "_to_")) {
    const char *target = strstr(name, "_to_") + 4;
    if (strstr(target, "sprint")) return GaitSprint;
    if (strstr(target, "run")) return GaitRun;
    if (strstr(target, "walk") || strstr(target, "move")) return GaitWalk;
    if (strstr(target, "idle")) {
      // Only a locomotion -> idle transition is eligible for fast release.
      if (strstr(name, "sprint") || strstr(name, "run") ||
          strstr(name, "walk") || strstr(name, "move"))
        return GaitWalk;
    }
    return GaitNone;
  }

  if (strstr(name, "start")) {
    if (strstr(name, "sprint")) return GaitSprint;
    if (strstr(name, "run")) return GaitRun;
    if (strstr(name, "walk") || strstr(name, "move")) return GaitWalk;
    return GaitNone;
  }

  if (strstr(name, "stop")) {
    if (strstr(name, "sprint") || strstr(name, "run") ||
        strstr(name, "walk") || strstr(name, "move"))
      return GaitWalk;
    return GaitNone;
  }

  if (strstr(name, "sprint")) return GaitSprint;
  if (strstr(name, "run")) return GaitRun;
  if (strstr(name, "walk") || strstr(name, "move")) return GaitWalk;
  return GaitNone;
}

static int ClassifyClipName(const char *name,
                            const CharacterProfile *profile = nullptr) {
  if (!name) return GaitNone;
  if (profile) {
    const AnimationRule *bestRule = nullptr;
    size_t bestLength = 0;
    for (const AnimationRule &rule : profile->animationRules) {
      if (!rule.contains.empty() && rule.contains.size() > bestLength &&
          strstr(name, rule.contains.c_str())) {
        bestRule = &rule;
        bestLength = rule.contains.size();
      }
    }
    if (bestRule) return bestRule->gait;
  }
  return ClassifyGlobalLocomotionClip(name);
}

static GaitClassification ClassifyClipNameFull(
    const char *name, const CharacterProfile *profile = nullptr) {
  GaitClassification out;
  out.gait = ClassifyClipName(name, profile);
  if (!name) return out;
  // Fast release is only meaningful for an accepted locomotion transition.
  if (out.gait != GaitNone &&
      (strstr(name, "stop") || strstr(name, "_to_idle")))
    out.transitionToIdle = true;
  if (out.gait != GaitNone && strstr(name, "jump")) {
    out.jumpDetected = true;
    // landing-only simplified jump (baseline: only the LAND clip drives it)
    if (strstr(name, "land")) out.landingDetected = true;
  }
  return out;
}
