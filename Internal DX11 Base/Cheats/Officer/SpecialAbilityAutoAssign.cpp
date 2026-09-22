#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../NotificationManager.h"
#include "../System/SkillCountManager.h"
#include "OfficerRosterResolve.h"
#include "OfficerData.h"
#include "SpecialAbilityAutoAssign.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace DX11Base {
namespace {

constexpr int kOfficerSlotCount = 5102;
constexpr int kOfficerIdMax = 5102;
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
    if (id < 1 || id > kOfficerIdMax)
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
  std::array<bool, kOfficerIdMax + 1> seenIds{};
  std::array<bool, kOfficerIdMax + 1> excludedExistingIds{};
  std::vector<PendingAssignment> pending;
  int validOfficers = 0;
  int excludedExisting = 0;
  int matchedOfficers = 0;
  std::array<int, 10> matchedCounts{};
  std::string status;
  std::vector<std::string> resultLines;
};

AutoAssignJob g_autoAssignJob;
bool IsAnnualAutoAssignSafeState() {
  static uint64_t s_lastProbeTick = 0;
  static bool s_lastSafe = false;

  const uint64_t now = GetTickCount64();
  if (now - s_lastProbeTick < 100ull)
    return s_lastSafe;
  s_lastProbeTick = now;

  const uintptr_t gameBase = GetGameBaseFast();
  if (gameBase <= 0x10000) {
    s_lastSafe = false;
    return false;
  }

  uint8_t gameState = 0xFF;
  __try {
    if (!IsValidPtr(gameBase + 0xD0, 1)) {
      s_lastSafe = false;
      return false;
    }
    gameState = *reinterpret_cast<uint8_t *>(gameBase + 0xD0);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    s_lastSafe = false;
    return false;
  }

  // 0x05=평정, 0x07=내정. 두 정상 전략 상태에서는 읽기 허용.
  // 0x00/0x02/0x04/0x06/0x08 등 화면 전환 중간 상태에서는 worker를 멈춥니다.
  s_lastSafe = (gameState == 0x05 || gameState == 0x07);
  return s_lastSafe;
}

std::atomic<bool> g_autoWorkerRunning{false};
std::atomic<bool> g_autoWorkerDone{false};
std::atomic<bool> g_autoWorkerCancel{false};
std::atomic<bool> g_autoWorkerPause{false};
std::atomic<size_t> g_autoWorkerProgress{0};
std::mutex g_autoWorkerResultMutex;

struct AutoAssignWorkerResult {
  std::vector<PendingAssignment> pending;
  int validOfficers = 0;
  int excludedExisting = 0;
  int matchedOfficers = 0;
  std::array<int, 10> matchedCounts{};
};
AutoAssignWorkerResult g_autoWorkerResult;

void StartAutoAssignWorker(
    uintptr_t rosterBase,
    bool annualMode,
    std::array<bool, kOfficerIdMax + 1> excludedExistingIds) {
  g_autoWorkerRunning = true;
  g_autoWorkerDone = false;
  g_autoWorkerCancel = false;
  g_autoWorkerPause = false;
  g_autoWorkerProgress = 0;

  {
    std::lock_guard<std::mutex> lock(g_autoWorkerResultMutex);
    g_autoWorkerResult = {};
  }

  std::thread(
      [rosterBase, annualMode, excludedExistingIds = std::move(excludedExistingIds)]() mutable {
        AutoAssignWorkerResult local;
        local.pending.reserve(annualMode ? 256 : 1024);
        std::array<bool, kOfficerIdMax + 1> seenIds{};
        bool debugId5Found = false;

        for (int i = 0; i < kOfficerSlotCount; ++i) {
          if (g_autoWorkerCancel.load()) {
            break;
          }

          while (g_autoWorkerPause.load() && !g_autoWorkerCancel.load())
            Sleep(10);

          if (g_autoWorkerCancel.load())
            break;

          const uintptr_t base =
              rosterBase + static_cast<uintptr_t>(i) * kOfficerStride;

          const RosterStats stats = SafeReadRosterStats(base);
          g_autoWorkerProgress = static_cast<size_t>(i + 1);

          if (!stats.valid || stats.id_08 < 1 || stats.id_08 > kOfficerIdMax)
            continue;

          if (annualMode && stats.id_08 == 5) {
            debugId5Found = true;
            AddLog(u8"[특수능력/연말자동/DBG] ID5 슬롯 발견: slot=%d base=%p",
                   i + 1, (void *)base);
          }

          if (seenIds[stats.id_08])
            continue;

          if (annualMode && excludedExistingIds[stats.id_08]) {
            if (stats.id_08 == 5)
              AddLog(u8"[특수능력/연말자동/DBG] ID5 탈락: 기존 특수능력 보유로 연말 자동 대상 제외");
            seenIds[stats.id_08] = true;
            ++local.excludedExisting;
            continue;
          }

          OfficerProfile profile;
          if (!ReadOfficerProfile(base, profile)) {
            if (annualMode && stats.id_08 == 5)
              AddLog(u8"[특수능력/연말자동/DBG] ID5 탈락: OfficerProfile 읽기 실패");
            continue;
          }
          if (profile.id != static_cast<int>(stats.id_08)) {
            if (annualMode && stats.id_08 == 5)
              AddLog(u8"[특수능력/연말자동/DBG] ID5 탈락: stats ID=%u / profile ID=%d 불일치",
                     (unsigned)stats.id_08, profile.id);
            continue;
          }

          seenIds[profile.id] = true;
          ++local.validOfficers;

          const std::array<bool, 10> matches =
              EvaluateSpecialAbilities(profile);

          if (annualMode && profile.id == 5) {
            const int cavalrySum =
                profile.cavalry[0] + profile.cavalry[1] + profile.cavalry[2] +
                profile.cavalry[3] + profile.cavalry[4];
            const int cavalryScore =
                cavalrySum + profile.branchTraits[1] * 2;
            AddLog(
                u8"[특수능력/연말자동/DBG] ID5 판정: 무력=%d / 기병=%d,%d,%d,%d,%d / 기장=%d / 합=%d / 점수=%d / 연격>=2=%d / 기사>=2=%d / 불꽃기병=%s",
                profile.war,
                profile.cavalry[0], profile.cavalry[1], profile.cavalry[2],
                profile.cavalry[3], profile.cavalry[4],
                profile.branchTraits[1],
                cavalrySum, cavalryScore,
                profile.cavalry[0] >= 2 ? 1 : 0,
                profile.cavalry[3] >= 2 ? 1 : 0,
                matches[2] ? "YES" : "NO");
          }

          bool any = false;
          for (int a = 0; a < static_cast<int>(matches.size()); ++a) {
            if (!matches[a])
              continue;
            any = true;
            ++local.matchedCounts[a];
          }

          if (!any)
            continue;

          ++local.matchedOfficers;
          local.pending.push_back({profile.id, matches});
        }

        if (annualMode && !debugId5Found) {
          AddLog(u8"[특수능력/연말자동/DBG] ID5 탈락: 1~5102 스캔에서 ID5 슬롯을 찾지 못함");
        }

        {
          std::lock_guard<std::mutex> lock(g_autoWorkerResultMutex);
          g_autoWorkerResult = std::move(local);
        }

        g_autoWorkerRunning = false;
        g_autoWorkerDone = true;
      })
      .detach();
}

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

  if (annualMode)
    AddLog(u8"[특수능력/연말자동/DBG] 작업 생성: rosterBase=%p", (void *)rosterBase);

  if (annualMode) {
    // 연말 자동 판정은 이미 특수 능력(0x1000~0x1009)을 하나라도 가진
    // 무장을 worker 시작 전에 한 번만 스냅샷으로 제외합니다.
    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    for (const auto &[officerId, skills] : g_customSkillCounts) {
      if (officerId < 1 || officerId > kOfficerIdMax)
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

    AddLog(u8"[특수능력/연말자동/DBG] ID5 기존능력 제외 상태=%s",
           g_autoAssignJob.excludedExistingIds[5] ? "YES" : "NO");
    AddLog(u8"[특수능력/연말자동] 1~5102 전체 무장 공간 worker 판정 시작");
  } else {
    AddLog(u8"[특수능력/자동] 1~5102 전체 무장 공간 worker 판정 시작");
  }

  StartAutoAssignWorker(
      rosterBase,
      annualMode,
      g_autoAssignJob.excludedExistingIds);
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

      if (g_autoAssignJob.annualMode && item.officerId == 5)
        AddLog(u8"[특수능력/연말자동/DBG] ID5 저장 단계 진입");

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

      if (g_autoAssignJob.annualMode && item.officerId == 5) {
        AddLog(u8"[특수능력/연말자동/DBG] ID5 저장 결과: %s",
               assigned.empty() ? u8"신규 부여 없음" : assigned.c_str());
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

  // 연말 자동 worker는 내정(0x07) 안정 상태에서만 메모리를 읽습니다.
  // 화면 전환이 시작되면 worker를 잠시 멈췄다가 안전 상태에서 이어갑니다.
  if (g_autoAssignJob.annualMode)
    g_autoWorkerPause = !IsAnnualAutoAssignSafeState();
  else
    g_autoWorkerPause = false;

  if (g_autoAssignJob.cancelRequested)
    g_autoWorkerCancel = true;

  const size_t progress = g_autoWorkerProgress.load();
  g_autoAssignJob.cursor = progress;
  g_autoAssignJob.status =
      std::string(g_autoAssignJob.annualMode ? u8"연말 자동 판정 중: " : u8"처리 중: ") +
      std::to_string(progress) + u8" / " +
      std::to_string(kOfficerSlotCount) + u8"명";

  if (!g_autoWorkerDone.load())
    return;

  if (g_autoAssignJob.cancelRequested || g_autoWorkerCancel.load()) {
    g_autoAssignJob.running = false;
    g_autoAssignJob.pending.clear();
    g_autoAssignJob.resultLines.clear();
    g_autoAssignJob.status =
        std::string(u8"취소됨: ") + std::to_string(progress) +
        u8" / " + std::to_string(kOfficerSlotCount) +
        u8"명 처리 (특수 능력 변경 없음)";
    g_autoWorkerDone = false;
    g_autoWorkerCancel = false;
    AddLog(u8"[특수능력/자동] 사용자 취소: %zu / %d명 처리, 변경 없음",
           progress, kOfficerSlotCount);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_autoWorkerResultMutex);
    g_autoAssignJob.pending = std::move(g_autoWorkerResult.pending);
    g_autoAssignJob.validOfficers = g_autoWorkerResult.validOfficers;
    g_autoAssignJob.excludedExisting = g_autoWorkerResult.excludedExisting;
    g_autoAssignJob.matchedOfficers = g_autoWorkerResult.matchedOfficers;
    g_autoAssignJob.matchedCounts = g_autoWorkerResult.matchedCounts;
    g_autoWorkerResult = {};
  }

  g_autoWorkerDone = false;
  FinishAutoAssignJob();
}

bool AutoAssignSpecialAbilitiesFromCouncil() {
  if (g_autoAssignJob.running) {
    AddLog(u8"[특수능력/연말자동/DBG] 시작 실패: 이미 특수능력 작업이 실행 중");
    return false;
  }

  AddLog(u8"[특수능력/연말자동] 12월 평정 진입 감지 -> 자동 판정 시작");
  const bool started = StartAutoAssignJob(true);
  AddLog(u8"[특수능력/연말자동/DBG] StartAutoAssignJob 결과=%s",
         started ? "SUCCESS" : "FAIL");
  return started;
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
  if (g_autoAssignJob.cursor >= static_cast<size_t>(kOfficerSlotCount))
    return 1.0f;
  return static_cast<float>(g_autoAssignJob.cursor) /
         static_cast<float>(kOfficerSlotCount);
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
