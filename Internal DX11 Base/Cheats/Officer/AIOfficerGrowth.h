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

// 12월 미리보기와 다음 1월 실제 적용에서 동일한 전법 선택 순서를 사용합니다.
uint32_t MakeAIOfficerGrowthAnnualSeed(
    unsigned short applyYear,
    uint16_t officerId);

// 해당 연도 1월 평정에서 AI 자동성장이 실제 적용되었는지 확인합니다.
// 1월 세이브를 단순 로드한 경우에는 false입니다.
bool WasAIOfficerGrowthAppliedForYear(unsigned short year);

// GrowthM 원본 로직을 기준으로 현재 무장 1명의 성장 결과를 메모리에 쓰지 않고 계산합니다.
// 남는 소양의 능력 EXP 전환 대상만 S8RPKCheats 규칙을 사용합니다.
bool BuildAIOfficerGrowthPreview(
    uintptr_t officerBase,
    int speed,
    AIOfficerGrowthPreview *out,
    uint32_t randomSeed = 0);

// 매년 12월 -> 1월 전환을 감지한 뒤 평정 상태에서 AI 성장 1회를 적용합니다.
// 1월 세이브를 단순히 불러온 경우에는 즉시 실행하지 않습니다.
void TickAIOfficerAutoGrowth();

} // namespace DX11Base
