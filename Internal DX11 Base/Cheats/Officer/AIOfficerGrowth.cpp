#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../System/MonthCapture.h"
#include "OfficerRosterResolve.h"
#include "AIOfficerGrowth.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace DX11Base {
namespace {

constexpr uintptr_t kOfficerStride = 0x3D0;
constexpr uint16_t kGrowthOfficerIdMax = 1800;

constexpr std::array<uintptr_t, 5> kCurrentExpOffsets = {
    0xB0, 0xB2, 0xB4, 0xB6, 0xB8};
constexpr std::array<uintptr_t, 5> kCumulativeExpOffsets = {
    0xBA, 0xBC, 0xBE, 0xC0, 0xC2};
constexpr std::array<uintptr_t, 7> kAptitudeOffsets = {
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA};
constexpr std::array<uintptr_t, 7> kTacticBaseOffsets = {
    0x139, 0x13E, 0x143, 0x148, 0x14D, 0x152, 0x157};

// GrowthM 0x180172A43의 19-byte category table을 사람이 읽을 수 있게 풀어쓴 값.
// stat/trait index는 원본과 동일한 1-base index를 유지한다.
struct CategoryRule {
  uint8_t statA;
  uint8_t statB;
  uint8_t traitA;
  uint8_t traitB;
  std::array<uint8_t, 3> normalStatReq;
  std::array<uint8_t, 3> normalTraitReq;
  std::array<uint8_t, 3> finalSumReq;
  std::array<uint8_t, 3> finalStatReq;
  std::array<uint8_t, 3> finalTraitReq;
};

constexpr std::array<CategoryRule, 7> kRules = {{
    {2, 1, 13, 18, {25, 50, 75}, {0, 0, 1}, {7, 9, 12}, {75, 85, 95}, {0, 1, 3}},
    {2, 1, 14, 18, {25, 50, 75}, {0, 0, 1}, {7, 9, 12}, {75, 85, 95}, {0, 1, 3}},
    {2, 1, 15, 18, {25, 50, 75}, {0, 0, 1}, {7, 9, 12}, {75, 85, 95}, {0, 1, 3}},
    {2, 1, 16, 18, {25, 50, 75}, {0, 0, 1}, {7, 9, 12}, {75, 85, 95}, {0, 1, 3}},
    {3, 1, 18, 12, {25, 50, 75}, {0, 0, 0}, {7, 9, 12}, {75, 85, 95}, {0, 0, 1}},
    {4, 5, 18, 12, {25, 50, 75}, {0, 0, 1}, {7, 9, 12}, {75, 85, 95}, {0, 0, 1}},
    {3, 5, 18, 12, {25, 50, 75}, {0, 0, 0}, {7, 9, 12}, {75, 85, 95}, {0, 0, 1}},
}};

constexpr std::array<int, 3> kNormalTacticCosts = {7, 15, 25};
constexpr std::array<int, 3> kFinalTacticCosts = {15, 25, 45};

int ClampSpeed(int speed) {
  if (speed < 1) return 1;
  if (speed > 3) return 3;
  return speed;
}

int GrowthFactorFromSpeed(int speed) {
  switch (ClampSpeed(speed)) {
  case 1: return 3;
  case 3: return 7;
  default: return 5;
  }
}

int CeilDiv10(uint16_t value) {
  return (static_cast<int>(value) + 9) / 10;
}

uint32_t NextRandom(uint32_t &state) {
  // Preview가 프레임마다 흔들리지 않도록 결정론적 xorshift32 사용.
  // 실제 적용 단계에서는 호출 시점 seed만 바꾸면 동일한 가중치 랜덤 로직을 재사용할 수 있다.
  if (state == 0)
    state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

int WeightedPick(
    const std::array<int, 5> &candidateIndex,
    const std::array<int, 5> &candidateWeight,
    int count,
    uint32_t &rng) {
  int total = 0;
  for (int i = 0; i < count; ++i)
    total += candidateWeight[i];
  if (total <= 0)
    return -1;

  int roll = static_cast<int>(NextRandom(rng) % static_cast<uint32_t>(total));
  for (int i = 0; i < count; ++i) {
    if (roll < candidateWeight[i])
      return candidateIndex[i];
    roll -= candidateWeight[i];
  }
  return candidateIndex[count - 1];
}

int StatValue(const std::array<uint8_t, 5> &stats, uint8_t oneBasedIndex) {
  if (oneBasedIndex < 1 || oneBasedIndex > stats.size())
    return 0;
  return stats[oneBasedIndex - 1];
}

int TraitValue(const std::array<uint8_t, 24> &traits, uint8_t oneBasedIndex) {
  if (oneBasedIndex < 1 || oneBasedIndex > traits.size())
    return 0;
  return traits[oneBasedIndex - 1];
}

int SumTactics(const std::array<uint8_t, 5> &levels) {
  int sum = 0;
  for (uint8_t v : levels)
    sum += (std::min<int>)(v, 3);
  return sum;
}

bool IsTacticEligible(
    const CategoryRule &rule,
    const std::array<uint8_t, 5> &stats,
    const std::array<uint8_t, 24> &traits,
    const std::array<uint8_t, 5> &levels,
    int tacticIndex) {
  if (tacticIndex < 0 || tacticIndex >= 5)
    return false;

  const int level = (std::min<int>)(levels[tacticIndex], 3);
  if (level >= 3)
    return false;

  const int maxStat =
      (std::max)(StatValue(stats, rule.statA), StatValue(stats, rule.statB));
  const int maxTrait =
      (std::max)(TraitValue(traits, rule.traitA), TraitValue(traits, rule.traitB));

  if (tacticIndex < 4) {
    return maxStat >= rule.normalStatReq[level] &&
           maxTrait >= rule.normalTraitReq[level];
  }

  int normalSum = 0;
  for (int i = 0; i < 4; ++i)
    normalSum += (std::min<int>)(levels[i], 3);

  return normalSum >= rule.finalSumReq[level] &&
         maxStat >= rule.finalStatReq[level] &&
         maxTrait >= rule.finalTraitReq[level];
}

int CalculateCategoryBase(
    int category,
    const std::array<int, 5> &cumUnit,
    const std::array<uint8_t, 24> &traits) {
  const int lead = cumUnit[0];
  const int war = cumUnit[1];
  const int intel = cumUnit[2];
  const int politics = cumUnit[3];
  const int charm = cumUnit[4];

  switch (category) {
  case 0: // 보병
  case 1: // 기병
  case 2: // 궁병
  case 3: { // 함선
    const int specialty =
        TraitValue(traits, kRules[category].traitA);
    return ((lead + war) * (specialty + 2)) / 10;
  }
  case 4: // 군략
    return (lead + intel * 2) / 3;
  case 5: // 보조: 실제 GrowthM DLL은 무력+지력+정치+매력 / 4
    return (war + intel + politics + charm) / 4;
  case 6: // 둔갑
    return (intel + politics + charm) / 3;
  default:
    return 0;
  }
}

int ChooseResidualStatIndex(
    int category,
    const std::array<uint8_t, 5> &stats) {
  // S8RPKCheats 규칙:
  // 병과 4종 -> 통솔/무력, 군략 -> 통솔/지력,
  // 보조 -> 정치/매력, 둔갑 -> 지력/매력.
  // 둘 중 낮은 순수 능력치에 EXP를 주고 동률이면 앞쪽 능력치를 선택한다.
  int a = 0;
  int b = 1;
  switch (category) {
  case 0:
  case 1:
  case 2:
  case 3:
    a = 0; b = 1; break;
  case 4:
    a = 0; b = 2; break;
  case 5:
    a = 3; b = 4; break;
  case 6:
    a = 2; b = 4; break;
  default:
    return 0;
  }
  return stats[a] <= stats[b] ? a : b;
}

void AddCurrentExp(
    std::array<uint16_t, 5> &currentExp,
    int statIndex,
    int amount) {
  if (statIndex < 0 || statIndex >= 5 || amount <= 0)
    return;
  const int value =
      (std::min)(65535, static_cast<int>(currentExp[statIndex]) + amount);
  currentExp[statIndex] = static_cast<uint16_t>(value);
}

bool IsHeroOfficer(uintptr_t officerBase, uint16_t officerId) {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;

  __try {
    if (!IsValidPtr(gameBase + 0xE0, sizeof(uintptr_t)))
      return false;
    const uintptr_t hero = *reinterpret_cast<uintptr_t *>(gameBase + 0xE0);
    if (hero <= 0x10000)
      return false;
    if (hero == officerBase)
      return true;
    if (!IsValidPtr(hero + 0x08, sizeof(uint16_t)))
      return false;
    return *reinterpret_cast<uint16_t *>(hero + 0x08) == officerId;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

} // namespace

const char *GetAIOfficerGrowthCategoryName(std::size_t index) {
  static constexpr std::array<const char *, 7> kNames = {
      u8"보병", u8"기병", u8"궁병", u8"함선", u8"군략", u8"보조", u8"둔갑"};
  return index < kNames.size() ? kNames[index] : u8"알 수 없음";
}

bool BuildAIOfficerGrowthPreview(
    uintptr_t officerBase,
    int speed,
    AIOfficerGrowthPreview *out,
    uint32_t randomSeed) {
  if (!out)
    return false;

  *out = {};
  out->speed = ClampSpeed(speed);
  out->growthFactor = GrowthFactorFromSpeed(out->speed);

  if (officerBase <= 0x10000 || !IsValidPtr(officerBase, kOfficerStride))
    return false;

  __try {
    const uint16_t id = *reinterpret_cast<uint16_t *>(officerBase + 0x08);
    out->officerId = id;
    if (id < 1 || id > kGrowthOfficerIdMax) {
      out->valid = true;
      return true;
    }

    const uintptr_t forcePtr =
        *reinterpret_cast<uintptr_t *>(officerBase + 0x18);
    if (forcePtr <= 0x10000 || IsHeroOfficer(officerBase, id)) {
      out->valid = true;
      return true;
    }

    std::array<uint8_t, 5> stats = {
        *reinterpret_cast<uint8_t *>(officerBase + 0xAA),
        *reinterpret_cast<uint8_t *>(officerBase + 0xAB),
        *reinterpret_cast<uint8_t *>(officerBase + 0xAC),
        *reinterpret_cast<uint8_t *>(officerBase + 0xAD),
        *reinterpret_cast<uint8_t *>(officerBase + 0xAE)};

    std::array<uint8_t, 24> traits{};
    for (int i = 0; i < 24; ++i)
      traits[i] =
          static_cast<uint8_t>((std::min<int>)(
              *reinterpret_cast<uint8_t *>(officerBase + 0x1D0 + i), 3));

    for (int i = 0; i < 5; ++i) {
      out->currentExpBefore[i] =
          *reinterpret_cast<uint16_t *>(officerBase + kCurrentExpOffsets[i]);
      out->currentExpAfter[i] = out->currentExpBefore[i];
      out->cumulativeExpBefore[i] =
          *reinterpret_cast<uint16_t *>(officerBase + kCumulativeExpOffsets[i]);
    }

    std::array<int, 5> cumUnit{};
    for (int i = 0; i < 5; ++i)
      cumUnit[i] = CeilDiv10(out->cumulativeExpBefore[i]);

    for (int category = 0; category < 7; ++category) {
      out->aptitudeBefore[category] =
          *reinterpret_cast<uint8_t *>(officerBase + kAptitudeOffsets[category]);

      for (int i = 0; i < 5; ++i) {
        const uint8_t level =
            static_cast<uint8_t>((std::min<int>)(
                *reinterpret_cast<uint8_t *>(
                    officerBase + kTacticBaseOffsets[category] + i), 3));
        out->tacticsBefore[category][i] = level;
        out->tacticsAfter[category][i] = level;
      }

      const int base = CalculateCategoryBase(category, cumUnit, traits);
      const int gain =
          (base * out->growthFactor + 6) / 7; // GrowthM: ceil(x/7)
      out->aptitudeGain[category] = gain;

      int aptitude =
          static_cast<int>(out->aptitudeBefore[category]) + gain;
      std::array<uint8_t, 5> &levels = out->tacticsAfter[category];
      int tacticSum = SumTactics(levels);

      uint32_t rng = randomSeed;
      if (rng == 0) {
        rng = 0xA511E9B3u ^
              (static_cast<uint32_t>(id) * 0x45D9F3Bu) ^
              (static_cast<uint32_t>(category + 1) * 0x9E3779B9u);
        for (uint16_t v : out->cumulativeExpBefore)
          rng = (rng * 1664525u) + static_cast<uint32_t>(v) + 1013904223u;
      } else {
        rng ^= static_cast<uint32_t>(category + 1) * 0x9E3779B9u;
      }

      if (tacticSum < 15) {
        for (int attempt = 0; attempt < 15 && aptitude >= 45 && tacticSum < 15;
             ++attempt) {
          std::array<int, 5> candidateIndex{};
          std::array<int, 5> candidateWeight{};
          int candidateCount = 0;

          for (int tactic = 0; tactic < 5; ++tactic) {
            if (!IsTacticEligible(
                    kRules[category], stats, traits, levels, tactic))
              continue;

            const int level = (std::min<int>)(levels[tactic], 3);
            candidateIndex[candidateCount] = tactic;
            if (tactic == 4)
              candidateWeight[candidateCount] = (level == 0) ? 3 : 1;
            else
              candidateWeight[candidateCount] = (level == 0) ? 2 : 1;
            ++candidateCount;
          }

          if (candidateCount == 0)
            break;

          const int picked =
              WeightedPick(candidateIndex, candidateWeight, candidateCount, rng);
          if (picked < 0)
            break;

          const int currentLevel = (std::min<int>)(levels[picked], 3);
          const int cost =
              (picked == 4)
                  ? kFinalTacticCosts[currentLevel]
                  : kNormalTacticCosts[currentLevel];

          if (aptitude < cost)
            break;

          aptitude -= cost;
          ++levels[picked];
          ++out->tacticLevelUps[category];
          ++tacticSum;
        }
      }

      const int residualStat = ChooseResidualStatIndex(category, stats);
      if (tacticSum >= 15) {
        AddCurrentExp(out->currentExpAfter, residualStat, aptitude);
        aptitude = 0;
      } else if (aptitude > 200) {
        AddCurrentExp(out->currentExpAfter, residualStat, aptitude - 200);
        aptitude = 200;
      }

      aptitude = (std::max)(0, (std::min)(255, aptitude));
      out->aptitudeAfter[category] = static_cast<uint8_t>(aptitude);
    }

    out->eligible = true;
    out->valid = true;
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = {};
    return false;
  }
}


namespace {

bool ApplyAIOfficerGrowthResult(
    uintptr_t officerBase,
    const AIOfficerGrowthPreview &preview) {
  if (!preview.valid || !preview.eligible)
    return false;

  // 소양/EXP/전법/누적EXP가 모두 들어 있는 레코드 범위만 잠시 쓰기 허용.
  DWORD oldProtect = 0;
  if (!VirtualProtect(
          reinterpret_cast<LPVOID>(officerBase + 0xB0),
          0xAC,
          PAGE_READWRITE,
          &oldProtect)) {
    return false;
  }

  bool ok = true;
  __try {
    for (int i = 0; i < 5; ++i) {
      *reinterpret_cast<uint16_t *>(
          officerBase + kCurrentExpOffsets[i]) =
          preview.currentExpAfter[i];

      // GrowthM 원본처럼 이번 성장 계산에 사용한 누적 EXP는 소비합니다.
      *reinterpret_cast<uint16_t *>(
          officerBase + kCumulativeExpOffsets[i]) = 0;
    }

    for (int category = 0; category < 7; ++category) {
      *reinterpret_cast<uint8_t *>(
          officerBase + kAptitudeOffsets[category]) =
          preview.aptitudeAfter[category];

      for (int i = 0; i < 5; ++i) {
        *reinterpret_cast<uint8_t *>(
            officerBase + kTacticBaseOffsets[category] + i) =
            preview.tacticsAfter[category][i];
      }
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD dummy = 0;
  VirtualProtect(
      reinterpret_cast<LPVOID>(officerBase + 0xB0),
      0xAC,
      oldProtect,
      &dummy);

  return ok;
}

bool IsCouncilState() {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;

  __try {
    if (!IsValidPtr(gameBase + 0xD0, 1))
      return false;
    return *reinterpret_cast<uint8_t *>(gameBase + 0xD0) == 0x05;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void RunAnnualAIOfficerGrowth(unsigned short year) {
  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
  uintptr_t rosterBase = 0;
  if (!exeBase ||
      !TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    AddLog(u8"[AI성장] %u년 자동성장 실패: 무장 배열을 찾지 못했습니다.",
           (unsigned)year);
    return;
  }

  const int speed = ClampSpeed(iAIOfficerGrowthSpeed);
  int eligibleCount = 0;
  int changedOfficerCount = 0;
  int totalTacticUps = 0;
  int totalAptitudeGain = 0;
  int consumedCumulativeCount = 0;

  std::array<bool, kGrowthOfficerIdMax + 1> seenIds{};

  // GrowthM 원본 범위: ID 1~1800.
  for (int slot = 0; slot < kGrowthOfficerIdMax; ++slot) {
    const uintptr_t officerBase =
        rosterBase + static_cast<uintptr_t>(slot) * kOfficerStride;

    if (!IsValidPtr(officerBase, kOfficerStride))
      continue;

    uint16_t id = 0;
    __try {
      id = *reinterpret_cast<uint16_t *>(officerBase + 0x08);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      continue;
    }

    if (id < 1 || id > kGrowthOfficerIdMax || seenIds[id])
      continue;
    seenIds[id] = true;

    AIOfficerGrowthPreview preview{};
    const uint32_t seed =
        (static_cast<uint32_t>(year) << 16) ^
        (static_cast<uint32_t>(id) * 0x45D9F3Bu) ^
        0xA17E5D31u;

    if (!BuildAIOfficerGrowthPreview(
            officerBase, speed, &preview, seed) ||
        !preview.valid ||
        !preview.eligible) {
      continue;
    }

    ++eligibleCount;

    bool hasCumulative = false;
    bool changed = false;
    for (int i = 0; i < 5; ++i) {
      if (preview.cumulativeExpBefore[i] != 0)
        hasCumulative = true;
      if (preview.currentExpAfter[i] != preview.currentExpBefore[i])
        changed = true;
    }

    for (int category = 0; category < 7; ++category) {
      totalAptitudeGain += preview.aptitudeGain[category];
      totalTacticUps += preview.tacticLevelUps[category];

      if (preview.aptitudeAfter[category] !=
          preview.aptitudeBefore[category]) {
        changed = true;
      }

      for (int i = 0; i < 5; ++i) {
        if (preview.tacticsAfter[category][i] !=
            preview.tacticsBefore[category][i]) {
          changed = true;
        }
      }
    }

    // 누적 EXP가 하나라도 있으면 결과가 동일해 보여도 원본처럼 소비해야 합니다.
    if (!hasCumulative)
      continue;

    if (ApplyAIOfficerGrowthResult(officerBase, preview)) {
      ++consumedCumulativeCount;
      if (changed)
        ++changedOfficerCount;
    }
  }

  AddLog(
      u8"[AI성장] %u년 자동성장 완료: 대상 %d명 / 누적EXP 소비 %d명 / 변화 %d명 / 전법 상승 %dLv / 소양 획득 합계 %d / 속도=%s",
      (unsigned)year,
      eligibleCount,
      consumedCumulativeCount,
      changedOfficerCount,
      totalTacticUps,
      totalAptitudeGain,
      speed == 1 ? u8"느림" : (speed == 3 ? u8"빠름" : u8"보통"));
}

} // namespace

void TickAIOfficerAutoGrowth() {
  static bool s_dateInitialized = false;
  static unsigned short s_lastYear = 0;
  static uint8_t s_lastMonth = 0;
  static bool s_pendingJanuaryCouncil = false;
  static unsigned short s_pendingYear = 0;
  static unsigned short s_lastAppliedYear = 0;

  unsigned short year = 0;
  uint8_t month = 0;
  if (!ReadScenarioDate(&year, &month) ||
      month < 1 || month > 12) {
    return;
  }

  if (!s_dateInitialized) {
    s_dateInitialized = true;
    s_lastYear = year;
    s_lastMonth = month;
    return;
  }

  // 기능이 꺼져 있는 동안에는 날짜 기준점만 따라가고 예약은 만들지 않습니다.
  if (!bAIOfficerAutoGrowth) {
    s_pendingJanuaryCouncil = false;
    s_pendingYear = 0;
    s_lastYear = year;
    s_lastMonth = month;
    return;
  }

  // 실제 12월 -> 다음 해 1월 전환을 본 경우에만 예약합니다.
  if (s_lastMonth == 12 &&
      month == 1 &&
      year >= s_lastYear &&
      year != s_lastAppliedYear) {
    s_pendingJanuaryCouncil = true;
    s_pendingYear = year;
    AddLog(u8"[AI성장] %u년 1월 전환 감지: 평정 진입 대기",
           (unsigned)year);
  }

  s_lastYear = year;
  s_lastMonth = month;

  if (!s_pendingJanuaryCouncil ||
      s_pendingYear == 0 ||
      s_pendingYear == s_lastAppliedYear) {
    return;
  }

  if (!IsCouncilState())
    return;

  const unsigned short applyYear = s_pendingYear;
  s_pendingJanuaryCouncil = false;
  s_pendingYear = 0;
  s_lastAppliedYear = applyYear;

  RunAnnualAIOfficerGrowth(applyYear);
}

} // namespace DX11Base
