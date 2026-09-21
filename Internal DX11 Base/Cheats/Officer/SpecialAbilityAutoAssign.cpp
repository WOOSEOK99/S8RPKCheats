#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "../System/SkillCountManager.h"
#include "OfficerRosterResolve.h"
#include "OfficerData.h"
#include "SpecialAbilityAutoAssign.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace DX11Base {
namespace {

constexpr int kOfficerCount = 5102;
constexpr uintptr_t kOfficerStride = 0x3D0;

constexpr std::array<uintptr_t, 10> kAbilityOffsets = {
    0x1000, // 군악대
    0x1001, // 무쌍 보병
    0x1002, // 불꽃 기병
    0x1003, // 원격 궁병
    0x1004, // 등갑군
    0x1005, // 총사령관
    0x1006, // 기습부대
    0x1007, // 대군사
    0x1008, // 무신
    0x1009  // 함선 병기화
};

constexpr std::array<const char *, 10> kAbilityNames = {
    u8"군악대", u8"무쌍 보병", u8"불꽃 기병", u8"원격 궁병", u8"등갑군",
    u8"총사령관", u8"기습부대", u8"대군사", u8"무신", u8"함선 병기화"};

struct OfficerProfile {
  int id = 0;
  int lead = 0;
  int war = 0;
  int intel = 0;
  int charm = 0;

  std::array<int, 5> infantry{};
  std::array<int, 5> cavalry{};
  std::array<int, 5> archer{};
  std::array<int, 5> ship{};
  std::array<int, 5> strategy{};
  std::array<int, 5> support{};

  // 병과 특기: 보장/기장/궁장/수군/조기/신산
  std::array<int, 6> branchTraits{};
  // 군사 특기: 원호/파성/행군/여력/과감/위풍
  std::array<int, 6> militaryTraits{};
};

bool SafeReadU8(uintptr_t address, uint8_t &out) {
  out = 0;
  if (!IsValidPtr(address, 1))
    return false;
  __try {
    out = *reinterpret_cast<uint8_t *>(address);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = 0;
    return false;
  }
}

bool SafeReadU16(uintptr_t address, uint16_t &out) {
  out = 0;
  if (!IsValidPtr(address, 2))
    return false;
  __try {
    out = *reinterpret_cast<uint16_t *>(address);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = 0;
    return false;
  }
}

int ReadLevel(uintptr_t base, uintptr_t offset) {
  uint8_t value = 0;
  if (!SafeReadU8(base + offset, value))
    return 0;
  return std::min<int>(value, 3);
}

int ReadStat(uintptr_t base, uintptr_t offset) {
  uint8_t value = 0;
  if (!SafeReadU8(base + offset, value))
    return 0;
  return static_cast<int>(value);
}

template <size_t N>
int SumLevels(const std::array<int, N> &values) {
  int total = 0;
  for (int v : values)
    total += v;
  return total;
}

template <size_t N>
int MaxLevel(const std::array<int, N> &values) {
  int result = 0;
  for (int v : values)
    result = (std::max)(result, v);
  return result;
}

bool ReadOfficerProfile(uintptr_t base, OfficerProfile &out) {
  if (!IsValidPtr(base, 0x1E8))
    return false;

  uint16_t id = 0;
  if (!SafeReadU16(base + 0x08, id) || id < 1 || id > kOfficerCount)
    return false;

  out = {};
  out.id = static_cast<int>(id);
  out.lead = ReadStat(base, 0xAA);
  out.war = ReadStat(base, 0xAB);
  out.intel = ReadStat(base, 0xAC);
  out.charm = ReadStat(base, 0xAE);

  for (int i = 0; i < 5; ++i) {
    out.infantry[i] = ReadLevel(base, 0x139 + i);
    out.cavalry[i] = ReadLevel(base, 0x13E + i);
    out.archer[i] = ReadLevel(base, 0x143 + i);
    out.ship[i] = ReadLevel(base, 0x148 + i);
    out.strategy[i] = ReadLevel(base, 0x14D + i);
    out.support[i] = ReadLevel(base, 0x152 + i);
  }

  for (int i = 0; i < 6; ++i) {
    out.branchTraits[i] = ReadLevel(base, 0x1DC + i);
    out.militaryTraits[i] = ReadLevel(base, 0x1E2 + i);
  }

  return true;
}

std::array<bool, 10> EvaluateSpecialAbilities(const OfficerProfile &p) {
  std::array<bool, 10> result{};

  const int infantrySum = SumLevels(p.infantry);
  const int cavalrySum = SumLevels(p.cavalry);
  const int archerSum = SumLevels(p.archer);
  const int shipSum = SumLevels(p.ship);
  const int militaryTraitSum = SumLevels(p.militaryTraits);

  // 0x1000 군악대
  // 분기(0x152)와 고무(0x153)가 핵심 효과이므로 둘의 숙련도를 직접 기준으로 사용.
  const int bunki = p.support[0];
  const int gomu = p.support[1];
  result[0] = (bunki >= 2 || gomu >= 2) && (bunki + gomu >= 4);

  // 0x1001 무쌍 보병
  // 강격/맹돌 중 하나는 Lv2 이상 + 보병 전법/보장 특기 + 높은 무력.
  const int infantryScore = infantrySum + p.branchTraits[0] * 2; // 보장
  result[1] = p.war >= 80 &&
              (p.infantry[0] >= 2 || p.infantry[3] >= 2) &&
              infantryScore >= 9;

  // 0x1002 불꽃 기병
  // 연격/기사 중 하나는 Lv2 이상 + 기병 전법/기장 특기 + 높은 무력.
  const int cavalryScore = cavalrySum + p.branchTraits[1] * 2; // 기장
  result[2] = p.war >= 80 &&
              (p.cavalry[0] >= 2 || p.cavalry[3] >= 2) &&
              cavalryScore >= 9;

  // 0x1003 원격 궁병
  // 궁병 전법/궁장 특기가 충분하고, 원사/시람이 강하거나 궁병 전법 전체가 매우 높을 때.
  const int archerScore = archerSum + p.branchTraits[2] * 2; // 궁장
  result[3] = p.war >= 75 &&
              archerScore >= 9 &&
              (p.archer[3] >= 2 || p.archer[4] >= 2 || archerSum >= 8);

  // 0x1004 등갑군
  // 현재 효과가 화염 취약 추가 페널티라 전법/특기와 자연스러운 자동 판정 기준이 없음.
  // 1차 자동 부여에서는 제외하고 수동 지정만 유지.
  result[4] = false;

  // 0x1005 총사령관
  // 높은 통솔 + 여러 병종 운용 능력 + 군사 특기 폭을 함께 요구.
  int strongPhysicalBranches = 0;
  if (infantrySum >= 6) ++strongPhysicalBranches;
  if (cavalrySum >= 6) ++strongPhysicalBranches;
  if (archerSum >= 6) ++strongPhysicalBranches;
  result[5] = p.lead >= 90 &&
              strongPhysicalBranches >= 2 &&
              (infantrySum + cavalrySum + archerSum) >= 16 &&
              militaryTraitSum >= 6;

  // 0x1006 기습부대
  // 교란(보병), 급습(기병), 요격(군략) 중 2종 이상을 실제로 다룰 수 있고 합계 숙련도가 높을 때.
  const int disruption = p.infantry[2];
  const int raid = p.cavalry[2];
  const int intercept = p.strategy[3];
  int ambushKinds = 0;
  if (disruption > 0) ++ambushKinds;
  if (raid > 0) ++ambushKinds;
  if (intercept > 0) ++ambushKinds;
  result[6] = ambushKinds >= 2 && (disruption + raid + intercept) >= 5;

  // 0x1007 대군사
  // 군략계 4종(동토 제외) 숙련 + 지력 + 군사 특기 또는 신산을 요구.
  const int strategyAffected =
      p.strategy[0] + p.strategy[1] + p.strategy[2] + p.strategy[3];
  result[7] = p.intel >= 85 &&
              strategyAffected >= 7 &&
              (militaryTraitSum >= 5 || p.branchTraits[5] >= 2); // 신산

  // 0x1008 무신
  // 보병/기병/궁병을 모두 실제로 다루는 최상위 무력형 장수만 대상.
  result[8] = p.war >= 95 &&
              infantrySum >= 5 && cavalrySum >= 5 && archerSum >= 5 &&
              (infantrySum + cavalrySum + archerSum) >= 18 &&
              MaxLevel(p.infantry) >= 2 &&
              MaxLevel(p.cavalry) >= 2 &&
              MaxLevel(p.archer) >= 2;

  // 0x1009 함선 병기화
  // 함선 전법 운용 능력 + 수군 특기를 함께 요구.
  result[9] = shipSum >= 7 && p.branchTraits[3] >= 2; // 수군

  return result;
}

struct PendingAssignment {
  int officerId = 0;
  std::array<bool, 10> abilities{};
};

} // namespace

bool AutoAssignSpecialAbilities() {
  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
  uintptr_t rosterBase = 0;
  if (!exeBase || !TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase < 0x10000) {
    AddLog(u8"[특수능력/자동] 무장 배열 주소를 찾지 못했습니다.");
    return false;
  }

  std::vector<PendingAssignment> pending;
  pending.reserve(kOfficerCount);

  bool seenIds[kOfficerCount + 1] = {};
  int validOfficers = 0;
  int matchedOfficers = 0;
  std::array<int, 10> matchedCounts{};

  for (int i = 0; i < kOfficerCount; ++i) {
    const uintptr_t base =
        rosterBase + static_cast<uintptr_t>(i) * kOfficerStride;

    const RosterStats stats = SafeReadRosterStats(base);
    if (!stats.valid || stats.id_08 < 1 || stats.id_08 > kOfficerCount)
      continue;

    OfficerProfile profile;
    if (!ReadOfficerProfile(base, profile))
      continue;
    if (profile.id != static_cast<int>(stats.id_08) || seenIds[profile.id])
      continue;
    seenIds[profile.id] = true;
    ++validOfficers;

    const std::array<bool, 10> matches =
        EvaluateSpecialAbilities(profile);

    bool any = false;
    for (int a = 0; a < static_cast<int>(matches.size()); ++a) {
      if (!matches[a])
        continue;
      any = true;
      ++matchedCounts[a];
    }

    if (!any)
      continue;

    ++matchedOfficers;
    pending.push_back({profile.id, matches});
  }

  int newlyAssigned = 0;
  std::array<int, 10> newlyAssignedCounts{};

  {
    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    for (const PendingAssignment &item : pending) {
      auto &skills = g_customSkillCounts[item.officerId];
      for (int a = 0; a < static_cast<int>(item.abilities.size()); ++a) {
        if (!item.abilities[a])
          continue;

        const uintptr_t key = kAbilityOffsets[a];
        auto it = skills.find(key);
        if (it != skills.end() && it->second > 0)
          continue;

        skills[key] = 1;
        ++newlyAssigned;
        ++newlyAssignedCounts[a];
      }
    }
  }

  if (newlyAssigned > 0)
    SaveSkillCounts();

  AddLog(u8"[특수능력/자동] 판정 완료: 유효 무장 %d명 / 조건 충족 %d명 / 신규 부여 %d건",
         validOfficers, matchedOfficers, newlyAssigned);

  for (int a = 0; a < static_cast<int>(kAbilityNames.size()); ++a) {
    if (a == 4) {
      AddLog(u8"[특수능력/자동] 등갑군: 자동 판정 제외(수동 지정 유지)");
      continue;
    }
    if (matchedCounts[a] == 0 && newlyAssignedCounts[a] == 0)
      continue;

    AddLog(u8"[특수능력/자동] %s: 조건 충족 %d명 / 신규 %d명",
           kAbilityNames[a], matchedCounts[a], newlyAssignedCounts[a]);
  }

  return true;
}

} // namespace DX11Base
