#pragma once
// Phase D: allocation-free MovementComponent signal provider.
// Metadata is resolved lazily once on the animation thread; continuous samples
// use only bounded raw reads from exact current-runtime instance fields.
#include <climits>
#include <cstdint>
#include <cstring>

#include "../il2cpp/il2cpp_api.h"
#include "movement_signal_types.h"

struct MovementExactField {
  int offset = -1;
};

enum class MovementExpectedType {
  Bool,
  Single,
  Vector3,
  Enum32,
  MovementComponent,
};

static inline bool MovementClassExact(void *klass, const char *expectedNs,
                                      const char *expectedName) {
  if (!klass || !il2cpp_class_get_name || !il2cpp_class_get_namespace)
    return false;
  const char *name = il2cpp_class_get_name(klass);
  const char *nameSpace = il2cpp_class_get_namespace(klass);
  return name && nameSpace && strcmp(name, expectedName) == 0 &&
         strcmp(nameSpace, expectedNs) == 0;
}

static inline bool MovementClassIsInt32Enum(void *klass) {
  if (!klass || !il2cpp_class_get_parent || !il2cpp_class_get_fields ||
      !il2cpp_field_get_name || !il2cpp_field_get_type ||
      !il2cpp_class_from_type)
    return false;
  bool derivesFromEnum = false;
  for (void *base = klass, *parent = nullptr; base; base = parent) {
    if (MovementClassExact(base, "System", "Enum")) {
      derivesFromEnum = true;
      break;
    }
    parent = il2cpp_class_get_parent(base);
  }
  if (!derivesFromEnum) return false;

  void *iterator = nullptr;
  void *field = nullptr;
  while ((field = il2cpp_class_get_fields(klass, &iterator))) {
    const char *name = il2cpp_field_get_name(field);
    if (!name || strcmp(name, "value__") != 0) continue;
    void *underlying = il2cpp_class_from_type(il2cpp_field_get_type(field));
    return MovementClassExact(underlying, "System", "Int32");
  }
  return false;
}

static inline bool MovementTypeMatches(void *fieldType,
                                       MovementExpectedType expected) {
  if (!fieldType || !il2cpp_class_from_type) return false;
  void *klass = il2cpp_class_from_type(fieldType);
  switch (expected) {
    case MovementExpectedType::Bool:
      return MovementClassExact(klass, "System", "Boolean");
    case MovementExpectedType::Single:
      return MovementClassExact(klass, "System", "Single");
    case MovementExpectedType::Vector3:
      return MovementClassExact(klass, "UnityEngine", "Vector3");
    case MovementExpectedType::Enum32:
      return MovementClassIsInt32Enum(klass);
    case MovementExpectedType::MovementComponent:
      return MovementClassExact(klass, "Beyond.Gameplay.Core",
                                "MovementComponent");
  }
  return false;
}

// Resolve exactly one matching field from the exact declaring class. Candidate
// names accommodate compiler backing fields without accepting ambiguous hits.
static inline bool MovementFindExactField(
    void *objectClass, const char *declaringNs, const char *declaringName,
    const char *const *candidateNames, size_t candidateCount,
    MovementExpectedType expectedType, MovementExactField &out) {
  out = MovementExactField();
  if (!objectClass || !candidateNames || candidateCount == 0 ||
      !il2cpp_class_get_parent || !il2cpp_class_get_fields ||
      !il2cpp_field_get_name || !il2cpp_field_get_type ||
      !il2cpp_field_get_flags || !il2cpp_field_get_offset)
    return false;

  for (void *klass = objectClass; klass;
       klass = il2cpp_class_get_parent(klass)) {
    if (!MovementClassExact(klass, declaringNs, declaringName)) continue;
    int matches = 0;
    int resolvedOffset = -1;
    void *iterator = nullptr;
    void *field = nullptr;
    while ((field = il2cpp_class_get_fields(klass, &iterator))) {
      const char *fieldName = il2cpp_field_get_name(field);
      if (!fieldName) continue;
      bool nameMatches = false;
      for (size_t i = 0; i < candidateCount; ++i) {
        if (candidateNames[i] && strcmp(fieldName, candidateNames[i]) == 0) {
          nameMatches = true;
          break;
        }
      }
      if (!nameMatches) continue;
      if ((il2cpp_field_get_flags(field) & 0x0010) != 0) return false;
      if (!MovementTypeMatches(il2cpp_field_get_type(field), expectedType))
        return false;
      const size_t rawOffset = il2cpp_field_get_offset(field);
      if (rawOffset < IL2CPP_BOXED_DATA || rawOffset > (size_t)INT_MAX)
        return false;
      ++matches;
      resolvedOffset = static_cast<int>(rawOffset);
    }
    if (matches != 1) return false;
    out.offset = resolvedOffset;
    return true;
  }
  return false;
}

class MovementSignalProbe {
 public:
  MovementSignalSample Sample(void *entity, bool identityValid) {
    MovementSignalSample out;
    out.entity = reinterpret_cast<uintptr_t>(entity);
    out.identityValid = identityValid;
    if (!entity || !identityValid) return out;

    void *movement = ResolveMovement(entity);
    out.movement = reinterpret_cast<uintptr_t>(movement);
    out.resolutionMask = resolutionMask_;
    if (!movement) return out;
    ResolveFields(movement);
    out.resolutionMask = resolutionMask_;

    __try {
      ReadEnum(movement, moveMode_, out.moveModeValid, out.moveMode);
      ReadEnum(movement, actualGait_, out.actualGaitValid, out.actualGait);
      ReadBool(movement, isMoving_, out.isMovingValid, out.isMoving);
      ReadBool(movement, isMovingOnGround_, out.isMovingOnGroundValid,
               out.isMovingOnGround);
      ReadBool(movement, isInAir_, out.isInAirValid, out.isInAir);
      ReadVec3(movement, velocity_, out.velocityValid, out.velocity);
      ReadVec3(movement, acceleration_, out.accelerationValid,
               out.acceleration);
      if (fallingSpeed_.offset >= 0) {
        out.fallingSpeed =
            *reinterpret_cast<const float *>(
                static_cast<const char *>(movement) + fallingSpeed_.offset);
        out.fallingSpeedValid = std::isfinite(out.fallingSpeed);
      }
      ReadBool(movement, teleported_, out.teleportedValid,
               out.teleportedThisFrame);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      out = MovementSignalSample();
      out.entity = reinterpret_cast<uintptr_t>(entity);
      out.movement = reinterpret_cast<uintptr_t>(movement);
      out.identityValid = identityValid;
      out.resolutionMask = resolutionMask_;
    }
    return out;
  }

  void Reset() {
    entityClass_ = nullptr;
    movementClass_ = nullptr;
    movementField_ = MovementExactField();
    ResetFields();
    resolutionMask_ = 0;
  }

 private:
  static void ReadBool(void *object, const MovementExactField &field,
                       bool &valid, bool &value) {
    if (field.offset < 0) return;
    value = *reinterpret_cast<const uint8_t *>(
                static_cast<const char *>(object) + field.offset) != 0;
    valid = true;
  }

  static void ReadEnum(void *object, const MovementExactField &field,
                       bool &valid, int &value) {
    if (field.offset < 0) return;
    value = *reinterpret_cast<const int32_t *>(
        static_cast<const char *>(object) + field.offset);
    valid = value >= -1 && value <= 255;
  }

  static void ReadVec3(void *object, const MovementExactField &field,
                       bool &valid, MovementVec3 &value) {
    if (field.offset < 0) return;
    value = *reinterpret_cast<const MovementVec3 *>(
        static_cast<const char *>(object) + field.offset);
    valid = MovementVec3Finite(value);
  }

  void *ResolveMovement(void *entity) {
    __try {
      void *klass = il2cpp_object_get_class
                        ? il2cpp_object_get_class(entity)
                        : nullptr;
      if (!klass) return nullptr;
      if (klass != entityClass_) {
        entityClass_ = klass;
        movementField_ = MovementExactField();
        resolutionMask_ = 0;
        const char *names[] = {"<movementComponent>k__BackingField"};
        if (MovementFindExactField(
                klass, "Beyond.Gameplay.Core", "Entity", names, 1,
                MovementExpectedType::MovementComponent, movementField_))
          resolutionMask_ |= MovementResolveEntityField;
      }
      if (movementField_.offset < 0) return nullptr;
      return *reinterpret_cast<void **>(
          static_cast<char *>(entity) + movementField_.offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return nullptr;
    }
  }

  void ResolveFields(void *movement) {
    __try {
      void *klass = il2cpp_object_get_class
                        ? il2cpp_object_get_class(movement)
                        : nullptr;
      if (!klass || klass == movementClass_) return;
      movementClass_ = klass;
      ResetFields();
      resolutionMask_ &= MovementResolveEntityField;
      if (!MovementClassExact(klass, "Beyond.Gameplay.Core",
                              "MovementComponent"))
        return;
      resolutionMask_ |= MovementResolveClass;

#define RESOLVE_FIELD(member, bit, type, ...)                                 \
      do {                                                                     \
        const char *names[] = {__VA_ARGS__};                                   \
        if (MovementFindExactField(                                            \
                klass, "Beyond.Gameplay.Core", "MovementComponent",          \
                names, sizeof(names) / sizeof(names[0]), type, member))        \
          resolutionMask_ |= bit;                                              \
      } while (0)
      RESOLVE_FIELD(moveMode_, MovementResolveMoveMode,
                    MovementExpectedType::Enum32,
                    "<moveMode>k__BackingField", "moveMode", "m_moveMode");
      RESOLVE_FIELD(actualGait_, MovementResolveActualGait,
                    MovementExpectedType::Enum32,
                    "<actualGait>k__BackingField", "actualGait", "m_actualGait");
      RESOLVE_FIELD(isMoving_, MovementResolveIsMoving,
                    MovementExpectedType::Bool,
                    "<isMoving>k__BackingField", "isMoving", "m_isMoving");
      RESOLVE_FIELD(isMovingOnGround_, MovementResolveIsMovingOnGround,
                    MovementExpectedType::Bool,
                    "<isMovingOnGround>k__BackingField", "isMovingOnGround",
                    "m_isMovingOnGround");
      RESOLVE_FIELD(isInAir_, MovementResolveIsInAir,
                    MovementExpectedType::Bool,
                    "<isInAir>k__BackingField", "isInAir", "m_isInAir");
      RESOLVE_FIELD(velocity_, MovementResolveVelocity,
                    MovementExpectedType::Vector3,
                    "<velocity>k__BackingField", "velocity", "m_velocity");
      RESOLVE_FIELD(acceleration_, MovementResolveAcceleration,
                    MovementExpectedType::Vector3,
                    "<acceleration>k__BackingField", "acceleration",
                    "m_acceleration");
      RESOLVE_FIELD(fallingSpeed_, MovementResolveFallingSpeed,
                    MovementExpectedType::Single,
                    "<fallingSpeed>k__BackingField", "fallingSpeed",
                    "m_fallingSpeed");
      RESOLVE_FIELD(teleported_, MovementResolveTeleported,
                    MovementExpectedType::Bool,
                    "<teleportedThisFrame>k__BackingField",
                    "teleportedThisFrame", "m_teleportedThisFrame");
#undef RESOLVE_FIELD
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      movementClass_ = nullptr;
      ResetFields();
      resolutionMask_ &= MovementResolveEntityField;
    }
  }

  void ResetFields() {
    moveMode_ = MovementExactField();
    actualGait_ = MovementExactField();
    isMoving_ = MovementExactField();
    isMovingOnGround_ = MovementExactField();
    isInAir_ = MovementExactField();
    velocity_ = MovementExactField();
    acceleration_ = MovementExactField();
    fallingSpeed_ = MovementExactField();
    teleported_ = MovementExactField();
  }

  void *entityClass_ = nullptr;
  void *movementClass_ = nullptr;
  MovementExactField movementField_;
  MovementExactField moveMode_;
  MovementExactField actualGait_;
  MovementExactField isMoving_;
  MovementExactField isMovingOnGround_;
  MovementExactField isInAir_;
  MovementExactField velocity_;
  MovementExactField acceleration_;
  MovementExactField fallingSpeed_;
  MovementExactField teleported_;
  uint32_t resolutionMask_ = 0;
};

static MovementSignalProbe g_movementSignalProbe;
