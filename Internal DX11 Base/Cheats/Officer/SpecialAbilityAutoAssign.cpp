#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../NotificationManager.h"
#include "../System/SkillCountManager.h"
#include "../System/SystemMonth.h"
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

bool ReadOfficerProfile(uintptr_t base, OfficerProfile &out) {
  // 무장 레코드는 0x3D0 연속 구조이므로 레코드 전체를 한 번만 검증합니다.
  // 이후 내부 필드는 __try 범위에서 직접 읽어 장수마다 수십 번 VirtualQuery 하는 비용을 피합니다.
  if (!IsValidPtr(base, kOfficerStride))
    return false;

  __try {
    const uint16_t id = *reinterpret_cast<uint16_t *>(base + 0x08);
    if (id < 1 || id > kOfficerCount)
      return false;

    out = {};
    out.id = static_cast<int>(id);
    out.lead = *reinterpret_cast<uint8_t *>(base + 0xAA);
    out.war = *reinterpret_cast<uint8_t *>(base + 0xAB);
    out.intel = *reinterpret_cast<uint8_t *>(base + 0xAC);
    out.charm = *reinterpret_cast<uint8_t *>(base + 0xAE);

    for (int i = 0; i < 5; ++i) {
      out.infantry[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x139 + i), 3);
      out.cavalry[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x13E + i), 3);
      out.archer[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x143 + i), 3);
      out.ship[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x148 + i), 3);
      out.strategy[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x14D + i), 3);
      out.support[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x152 + i), 3);
    }

    for (int i = 0; i < 6; ++i) {
      out.branchTraits[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x1DC + i), 3);
      out.militaryTraits[i] = (std::min<int>)(*reinterpret_cast<uint8_t *>(base + 0x1E2 + i), 3);
    }
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = {};
    return false;
  }
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

struct AutoAssignJob {
  bool running = false;
  bool cancelRequested = false;
  bool annualMode = false;
  uintptr_t rosterBase = 0;
  size_t cursor = 0;
  std::array<bool, kOfficerCount + 1> seenIds{};
  std::array<bool, kOfficerCount + 1> excludedExistingIds{};
  std::vector<PendingAssignment> pending;
  int validOfficers = 0;
  int excludedExisting = 0;
  int matchedOfficers = 0;
  std::array<int, 10> matchedCounts{};
  std::string status;
  std::vector<std::string> resultLines;
};

AutoAssignJob g_autoAssignJob;
bool g_annualAssignPending = false;
uint8_t g_lastAnnualMonth = 0;
uint64_t g_lastAnnualMonthPollTick = 0;

bool StartAutoAssignJob(bool annualMode) {
  if (g_autoAssignJob.running)
    return false;

  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
  uintptr_t rosterBase = 0;
  if (!exeBase || !TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase < 0x10000) {
    AddLog(u8"[특수능력/자동] 무장 배열 주소를 찾지 못했습니다.");
    g_autoAssignJob.status =
        u8"무장 배열 주소를 찾지 못했습니다. 전략 화면에서 다시 시도하세요.";
    return false;
  }

  g_autoAssignJob = {};
  g_autoAssignJob.running = true;
  g_autoAssignJob.annualMode = annualMode;
  g_autoAssignJob.rosterBase = rosterBase;
  g_autoAssignJob.pending.reserve(annualMode ? 256 : 1024);
  g_autoAssignJob.status = annualMode
      ? u8"연말 자동 판정 시작: 0 / 5102명"
      : u8"처리 시작: 0 / 5102명";

  if (annualMode) {
    // 연말 자동 판정은 이미 특수 능력(0x1000~0x1009)을 하나라도 가진
    // 무장을 스캔 시작 전에 한 번만 스냅샷으로 제외합니다.
    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    for (const auto &[officerId, skills] : g_customSkillCounts) {
      if (officerId < 1 || officerId > kOfficerCount)
        continue;

      bool hasAny = false;
      for (uintptr_t key : kAbilityOffsets) {
        auto it = skills.find(key);
        if (it != skills.end() && it->second > 0) {
          hasAny = true;
          break;
        }
      }

      if (hasAny)
        g_autoAssignJob.excludedExistingIds[officerId] = true;
    }

    AddLog(u8"[특수능력/연말자동] 12월->1월 자동 판정 시작: 프레임당 24명 처리");
  } else {
    AddLog(u8"[특수능력/자동] 증분 판정 시작: 프레임당 24명 처리");
  }

  return true;
}

void FinishAutoAssignJob() {
  int newlyAssigned = 0;
  std::array<int, 10> newlyAssignedCounts{};
  g_autoAssignJob.resultLines.clear();
  LoadOfficerNames();

  {
    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    for (const PendingAssignment &item : g_autoAssignJob.pending) {
      auto &skills = g_customSkillCounts[item.officerId];
      std::string assigned;

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

        if (!assigned.empty())
          assigned += ", ";
        assigned += kAbilityNames[a];
      }

      if (!assigned.empty()) {
        std::string officerName = u8"무장";
        auto nameIt = g_officerNames.find(item.officerId);
        if (nameIt != g_officerNames.end() && !nameIt->second.empty())
          officerName = nameIt->second;

        g_autoAssignJob.resultLines.push_back(
            officerName + " (ID " + std::to_string(item.officerId) +
            ") : " + assigned);
      }
    }
  }

  if (newlyAssigned > 0)
    SaveSkillCounts();

  g_autoAssignJob.running = false;
  g_autoAssignJob.status =
      std::string(g_autoAssignJob.annualMode ? u8"연말 자동 완료: " : u8"완료: ") +
      u8"유효 무장 " +
      std::to_string(g_autoAssignJob.validOfficers) +
      u8"명 / 조건 충족 " +
      std::to_string(g_autoAssignJob.matchedOfficers) +
      u8"명 / 신규 부여 " +
      std::to_string(newlyAssigned) + u8"건";

  AddLog(g_autoAssignJob.annualMode
             ? u8"[특수능력/연말자동] 판정 완료: 유효 %d명 / 기존능력 제외 %d명 / 조건 충족 %d명 / 신규 부여 %d건"
             : u8"[특수능력/자동] 판정 완료: 유효 %d명 / 기존능력 제외 %d명 / 조건 충족 %d명 / 신규 부여 %d건",
         g_autoAssignJob.validOfficers,
         g_autoAssignJob.excludedExisting,
         g_autoAssignJob.matchedOfficers,
         newlyAssigned);

  for (int a = 0; a < static_cast<int>(kAbilityNames.size()); ++a) {
    if (a == 4) {
      AddLog(u8"[특수능력/자동] 등갑군: 자동 판정 제외(수동 지정 유지)");
      continue;
    }
    if (g_autoAssignJob.matchedCounts[a] == 0 && newlyAssignedCounts[a] == 0)
      continue;

    AddLog(u8"[특수능력/자동] %s: 조건 충족 %d명 / 신규 %d명",
           kAbilityNames[a],
           g_autoAssignJob.matchedCounts[a],
           newlyAssignedCounts[a]);
  }

  if (g_autoAssignJob.annualMode && !g_autoAssignJob.resultLines.empty()) {
    // 상단 마퀴는 요약 1건만 표시해 알림 폭주를 막습니다.
    AddNotification(
        std::string(u8"[연말 특수능력] ") +
        std::to_string(g_autoAssignJob.resultLines.size()) +
        u8"명의 무장에게 특수 능력이 새로 부여되었습니다. 알림 내역에서 확인하세요.");

    // 장수별 상세는 알림 내역에 직접 기록합니다.
    for (const std::string &line : g_autoAssignJob.resultLines) {
      g_notificationHistory.push_back(
          std::string(u8"[연말 특수능력] ") + line);
    }
    if (g_notificationHistory.size() > 200) {
      g_notificationHistory.erase(
          g_notificationHistory.begin(),
          g_notificationHistory.begin() +
              (g_notificationHistory.size() - 200));
    }
  }

  g_autoAssignJob.pending.clear();
}

} // namespace

bool AutoAssignSpecialAbilities() {
  if (g_autoAssignJob.running) {
    AddLog(u8"[특수능력/자동] 이미 자동 부여 작업이 진행 중입니다.");
    return false;
  }
  return StartAutoAssignJob(false);
}

void TickSpecialAbilityAutoAssign() {
  if (!g_autoAssignJob.running)
    return;

  if (g_autoAssignJob.cancelRequested) {
    const size_t processed = g_autoAssignJob.cursor;
    g_autoAssignJob.running = false;
    g_autoAssignJob.pending.clear();
    g_autoAssignJob.resultLines.clear();
    g_autoAssignJob.status =
        std::string(u8"취소됨: ") + std::to_string(processed) +
        u8" / 5102명 처리 (특수 능력 변경 없음)";
    AddLog(u8"[특수능력/자동] 사용자 취소: %zu / %d명 처리, 변경 없음",
           processed, kOfficerCount);
    return;
  }

  // 게임/UI 스레드를 오래 점유하지 않도록 프레임당 24명만 판정합니다.
  constexpr size_t kOfficersPerFrame = 24;
  const size_t end = (std::min)(
      g_autoAssignJob.cursor + kOfficersPerFrame,
      static_cast<size_t>(kOfficerCount));

  for (; g_autoAssignJob.cursor < end; ++g_autoAssignJob.cursor) {
    const uintptr_t base =
        g_autoAssignJob.rosterBase +
        static_cast<uintptr_t>(g_autoAssignJob.cursor) * kOfficerStride;

    const RosterStats stats = SafeReadRosterStats(base);
    if (!stats.valid || stats.id_08 < 1 || stats.id_08 > kOfficerCount)
      continue;

    if (g_autoAssignJob.seenIds[stats.id_08])
      continue;

    if (g_autoAssignJob.annualMode &&
        g_autoAssignJob.excludedExistingIds[stats.id_08]) {
      g_autoAssignJob.seenIds[stats.id_08] = true;
      ++g_autoAssignJob.excludedExisting;
      continue;
    }

    OfficerProfile profile;
    if (!ReadOfficerProfile(base, profile))
      continue;
    if (profile.id != static_cast<int>(stats.id_08))
      continue;

    g_autoAssignJob.seenIds[profile.id] = true;
    ++g_autoAssignJob.validOfficers;

    const std::array<bool, 10> matches =
        EvaluateSpecialAbilities(profile);

    bool any = false;
    for (int a = 0; a < static_cast<int>(matches.size()); ++a) {
      if (!matches[a])
        continue;
      any = true;
      ++g_autoAssignJob.matchedCounts[a];
    }

    if (!any)
      continue;

    ++g_autoAssignJob.matchedOfficers;
    g_autoAssignJob.pending.push_back({profile.id, matches});
  }

  g_autoAssignJob.status =
      std::string(u8"처리 중: ") +
      std::to_string(g_autoAssignJob.cursor) +
      u8" / 5102명";

  if (g_autoAssignJob.cursor >= static_cast<size_t>(kOfficerCount))
    FinishAutoAssignJob();
}

void TickAnnualSpecialAbilityAutoAssign(bool enabled) {
  if (!enabled) {
    g_annualAssignPending = false;
    g_lastAnnualMonth = 0;
    g_lastAnnualMonthPollTick = 0;
    return;
  }

  const uint64_t now = GetTickCount64();
  if (now - g_lastAnnualMonthPollTick >= 250ull) {
    g_lastAnnualMonthPollTick = now;

    const uint8_t month = GetSystemMonthValue();
    if (month >= 1 && month <= 12) {
      if (g_lastAnnualMonth == 12 && month == 1) {
        g_annualAssignPending = true;
        AddLog(u8"[특수능력/연말자동] 12월 -> 1월 전환 감지, 자동 판정 예약");
      }
      g_lastAnnualMonth = month;
    }
  }

  if (g_annualAssignPending && !g_autoAssignJob.running) {
    if (StartAutoAssignJob(true))
      g_annualAssignPending = false;
  }
}

void CancelSpecialAbilityAutoAssign() {
  if (g_autoAssignJob.running)
    g_autoAssignJob.cancelRequested = true;
}

bool IsSpecialAbilityAutoAssignRunning() {
  return g_autoAssignJob.running;
}

float GetSpecialAbilityAutoAssignProgress() {
  if (g_autoAssignJob.cursor == 0)
    return 0.0f;
  if (g_autoAssignJob.cursor >= static_cast<size_t>(kOfficerCount))
    return 1.0f;
  return static_cast<float>(g_autoAssignJob.cursor) /
         static_cast<float>(kOfficerCount);
}

const char *GetSpecialAbilityAutoAssignStatus() {
  return g_autoAssignJob.status.c_str();
}

std::size_t GetSpecialAbilityAutoAssignResultCount() {
  return g_autoAssignJob.resultLines.size();
}

const char *GetSpecialAbilityAutoAssignResultLine(std::size_t index) {
  if (index >= g_autoAssignJob.resultLines.size())
    return "";
  return g_autoAssignJob.resultLines[index].c_str();
}

} // namespace DX11Base
