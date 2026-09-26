#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace DX11Base::TraitEffectOwnerBaseline {

constexpr std::size_t kEffectCount = 6;
constexpr int kTraitCount = 254;

struct State {
  std::array<std::array<uint16_t, kEffectCount>, kTraitCount> effectTypes{};
  uintptr_t sourceTableBase = 0;
  bool ready = false;
};

inline State &GetState() {
  static State state;
  return state;
}

inline void Reset() {
  GetState() = {};
}

inline bool Matches(uint16_t requestedTraitId,
                    const std::array<uint16_t, kEffectCount> &customTypes) {
  const State &state = GetState();
  if (!state.ready || requestedTraitId < 1 || requestedTraitId > kTraitCount)
    return false;

  const auto &requestedTypes =
      state.effectTypes[static_cast<std::size_t>(requestedTraitId - 1)];

  for (uint16_t customType : customTypes) {
    if (customType == 0)
      continue;
    for (uint16_t requestedType : requestedTypes) {
      if (requestedType != 0 && customType == requestedType)
        return true;
    }
  }
  return false;
}

} // namespace DX11Base::TraitEffectOwnerBaseline
