#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace DX11Base {

constexpr std::size_t kAIOfficerGrowthCategoryCount = 7;

struct AIOfficerGrowthPreview {
  bool valid = false;
  bool eligible = false;
  uint16_t officerId = 0;
  int speed = 2;        // 1=느림, 2=보통, 3=빠름
  int growthFactor = 5; // GrowthM 원본 3/5/7

  std::array<uint8_t, kAIOfficerGrowthCategoryCount> aptitudeBefore{};
  std::array<int, kAIOfficerGrowthCategoryCount> aptitudeGain{};
  std::array<uint8_t, kAIOfficerGrowthCategoryCount> aptitudeAfter{};
  std::array<int, kAIOfficerGrowthCategoryCount> tacticLevelUps{};

  std::array<uint16_t, 5> currentExpBefore{};
  std::array<uint16_t, 5> currentExpAfter{};
  std::array<uint16_t, 5> cumulativeExpBefore{};

  std::array<std::array<uint8_t, 5>, kAIOfficerGrowthCategoryCount> tacticsBefore{};
  std::array<std::array<uint8_t, 5>, kAIOfficerGrowthCategoryCount> tacticsAfter{};
};

const char *GetAIOfficerGrowthCategoryName(std::size_t index);

// GrowthM 원본 로직을 기준으로 현재 무장 1명의 성장 결과를 메모리에 쓰지 않고 계산합니다.
// 남는 소양의 능력 EXP 전환 대상만 S8RPKCheats 규칙을 사용합니다.
bool BuildAIOfficerGrowthPreview(
    uintptr_t officerBase,
    int speed,
    AIOfficerGrowthPreview *out,
    uint32_t randomSeed = 0);

} // namespace DX11Base
