#include "../../pch.h"
#include "../../Cheats.h"
#include "ChildEarlyAppearance.h"
#include "../../MenuState.h"
#include "../../Cheats/System/MonthCapture.h"
#include "../../Cheats/Officer/OfficerData.h"
#include "../../Cheats/Officer/OfficerRosterResolve.h"
#include "../../showlog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DX11Base {
namespace {

struct ChildEntry {
  uint16_t id = 0;
  uintptr_t addr = 0;
  bool selected = false;
  int yearsLater = 3;
  uint16_t appearanceYear = 0;
  uint16_t birthYear = 0;
  uint16_t deathYear = 0;
  uint16_t appliedTargetYear = 0;

  // 체크 해제 시 원래 일정으로 되돌리기 위한 백업
  bool hasOriginalSchedule = false;
  uint16_t originalAppearanceYear = 0;
  uint16_t originalBirthYear = 0;
};

static std::unordered_map<uint16_t, ChildEntry> g_children;
static ULONGLONG g_lastChildScanMs = 0;
static uint16_t g_lastHeroId = 0;

// 임신 레코드 구조 탐색용 임시 read-only 진단.
// 과거 CT 힌트: stride 0x28, +0x09 임신 flag, +0x0A 남은 개월,
// +0x10 자녀 pointer, +0x1E~+0x22 자녀 능력 상한 후보.
struct PregnancyDebugRecordDump {
  bool valid = false;
  uintptr_t base = 0;
  uintptr_t q00 = 0;
  uintptr_t q08 = 0;
  uintptr_t childPtr = 0;
  uint16_t q00OfficerId = 0;
  uint16_t q08OfficerId = 0;
  uint16_t childOfficerId = 0;
  uint8_t pregnancyCooldown = 0;
  uint8_t pregnancyFlag = 0;
  uint8_t remainingMonths = 0;
  std::array<uint8_t, 5> capRaw{};
};

struct PregnancyDebugHit {
  uint16_t anchorChildId = 0;
  uintptr_t anchorChildAddr = 0;
  uintptr_t childPtrRefAddr = 0;
  uintptr_t recordBase = 0;
  bool expectedFlagMonthRange = false;
  uint16_t spouseMatchId = 0;
  int spouseMatchOffset = -1;
  int score = 0;
  std::array<PregnancyDebugRecordDump, 5> neighborhood{};
};

static std::atomic<bool> g_pregnancyDebugScanning{false};
static std::atomic<float> g_pregnancyDebugProgress{0.0f};
static std::atomic<bool> g_pregnancyDebugResultsReady{false};
static std::mutex g_pregnancyDebugMutex;
static std::vector<PregnancyDebugHit> g_pregnancyDebugResults;
static size_t g_pregnancyDebugTotalHits = 0;

struct PregnancySpouseDebugHit {
  uint16_t spouseId = 0;
  uintptr_t spouseAddr = 0;
  uintptr_t recordBase = 0;
  PregnancyDebugRecordDump record{};
  int score = 0;
  int neighborSpouseMatches = 0;
  std::array<uint16_t, 7> neighborSpouseIds{};
};

static std::atomic<bool> g_pregnancySpouseScanning{false};
static std::atomic<float> g_pregnancySpouseProgress{0.0f};
static std::atomic<bool> g_pregnancySpouseResultsReady{false};
static std::mutex g_pregnancySpouseMutex;
static std::vector<PregnancySpouseDebugHit> g_pregnancySpouseResults;
static size_t g_pregnancySpouseTotalHits = 0;

struct PregnancyCanonicalTable {
  bool valid = false;
  uintptr_t base = 0;
  std::array<PregnancyDebugRecordDump, 3> slots{};
  std::array<uint16_t, 3> spouseIds{};
};

static PregnancyCanonicalTable g_pregnancyCanonicalTable;

struct PregnancySpouseOption {
  uint16_t id = 0;
  uintptr_t addr = 0;
};

static std::vector<PregnancySpouseOption> g_pregnancyCurrentSpouses;
static int g_pregnancySwapSlot = -1;
static uint16_t g_pregnancySwapSpouseId = 0;

static bool ResolveHeroAndRoster(
    uintptr_t& rosterBase,
    uintptr_t& heroMaster,
    uint16_t& heroId);

static PregnancyCanonicalTable GetCanonicalPregnancySnapshot();

static uintptr_t NormalizeOfficerPtr(uintptr_t p) {
  return p & 0x0000FFFFFFFFFFFFULL;
}

static bool SafeRead16(uintptr_t addr, uint16_t* out) {
  __try {
    *out = *(uint16_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeReadPtr(uintptr_t addr, uintptr_t* out) {
  __try {
    *out = *(uintptr_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeRead8(uintptr_t addr, uint8_t* out) {
  __try {
    *out = *(uint8_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  __try {
    memcpy(out, (const void*)addr, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static uint16_t TryReadOfficerIdFromPointer(uintptr_t rawPtr) {
  const uintptr_t p = NormalizeOfficerPtr(rawPtr);
  if (p <= 0x10000)
    return 0;

  uint16_t id = 0;
  if (!SafeRead16(p + 0x08, &id) || id < 1 || id > 5102)
    return 0;
  return id;
}

static bool ReadPregnancyDebugRecord(
    uintptr_t base,
    PregnancyDebugRecordDump* out) {
  if (!out || base <= 0x10000)
    return false;

  PregnancyDebugRecordDump dump;
  dump.base = base;

  if (!SafeReadPtr(base + 0x00, &dump.q00) ||
      !SafeReadPtr(base + 0x08, &dump.q08) ||
      !SafeRead8(base + 0x08, &dump.pregnancyCooldown) ||
      !SafeRead8(base + 0x09, &dump.pregnancyFlag) ||
      !SafeRead8(base + 0x0A, &dump.remainingMonths) ||
      !SafeReadPtr(base + 0x10, &dump.childPtr) ||
      !SafeReadMem(base + 0x1E, dump.capRaw.data(), dump.capRaw.size())) {
    return false;
  }

  dump.q00OfficerId = TryReadOfficerIdFromPointer(dump.q00);
  dump.q08OfficerId = TryReadOfficerIdFromPointer(dump.q08);
  dump.childOfficerId = TryReadOfficerIdFromPointer(dump.childPtr);
  dump.valid = true;
  *out = dump;
  return true;
}

static bool IsPregnancyDebugScanRegion(
    const MEMORY_BASIC_INFORMATION& mbi) {
  if (mbi.State != MEM_COMMIT ||
      (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS)) {
    return false;
  }

  const DWORD protection = mbi.Protect & 0xFF;
  return protection == PAGE_READWRITE ||
         protection == PAGE_WRITECOPY ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

static bool PregnancyCapsLookPlausible(
    const PregnancyDebugRecordDump& dump) {
  bool any = false;
  for (uint8_t v : dump.capRaw) {
    if (v != 0)
      any = true;
    if (v > 100)
      return false;
  }
  return any;
}

static bool TryBuildCanonicalPregnancyTable(
    const std::vector<PregnancySpouseDebugHit>& hits,
    PregnancyCanonicalTable* out) {
  if (!out)
    return false;

  PregnancyCanonicalTable best;
  int bestScore = -1;

  for (const PregnancySpouseDebugHit& hit : hits) {
    // 실측 canonical 후보는 3개의 현재 배우자가 정확히 0x28 stride로 연속.
    // 현재 hit가 세 슬롯 중 어느 위치일 수 있으므로 -2..0 시작점을 모두 시험합니다.
    for (int startRel = -2; startRel <= 0; ++startRel) {
      const intptr_t baseSigned =
          (intptr_t)hit.recordBase + (intptr_t)startRel * 0x28;
      if (baseSigned <= 0x10000)
        continue;

      PregnancyCanonicalTable table;
      table.base = (uintptr_t)baseSigned;

      std::unordered_set<uint16_t> uniqueSpouses;
      bool ok = true;
      int score = 0;

      for (int slot = 0; slot < 3; ++slot) {
        PregnancyDebugRecordDump d;
        if (!ReadPregnancyDebugRecord(
                table.base + (uintptr_t)slot * 0x28, &d)) {
          ok = false;
          break;
        }

        // canonical 슬롯의 +00은 현재 배우자 무장 포인터여야 합니다.
        const uint16_t spouseId = d.q00OfficerId;
        if (spouseId == 0) {
          ok = false;
          break;
        }

        table.slots[(size_t)slot] = d;
        table.spouseIds[(size_t)slot] = spouseId;
        uniqueSpouses.insert(spouseId);

        if (d.pregnancyFlag <= 1)
          score += 20;
        if (d.remainingMonths <= 12)
          score += 20;
        if (d.childPtr == 0 || d.childOfficerId != 0)
          score += 10;
      }

      if (!ok || uniqueSpouses.size() != 3)
        continue;

      // 이 세 ID가 실제 현재 배우자 검색 결과에 등장한 ID인지 확인.
      int currentSpouseMatches = 0;
      for (uint16_t id : table.spouseIds) {
        for (const PregnancySpouseDebugHit& h : hits) {
          if (h.spouseId == id) {
            currentSpouseMatches++;
            break;
          }
        }
      }
      if (currentSpouseMatches != 3)
        continue;

      score += 300;
      if (score > bestScore) {
        bestScore = score;
        table.valid = true;
        best = table;
      }
    }
  }

  if (!best.valid)
    return false;

  *out = best;
  return true;
}

static void StartPregnancySpouseDebugScanAsync() {
  if (g_pregnancySpouseScanning.load())
    return;

  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ResolveHeroAndRoster(rosterBase, heroMaster, heroId)) {
    AddLog(u8"[임신배우자DBG] 주인공/무장 배열 주소 해석 실패");
    return;
  }

  OfficerRelationshipInfo relInfo;
  if (!GetOfficerRelationshipInfo(heroMaster, relInfo) ||
      !relInfo.valid || relInfo.spouses.empty()) {
    AddLog(u8"[임신배우자DBG] 현재 배우자 관계를 읽지 못했습니다.");
    return;
  }

  std::unordered_set<uint16_t> spouseIds;
  for (uint16_t id : relInfo.spouses)
    spouseIds.insert(id);

  std::unordered_map<uintptr_t, uint16_t> spouseTargets;
  bool seenOfficerIds[5103] = {};
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officerBase =
        rosterBase + (uintptr_t)i * 0x3D0;

    uint16_t id = 0;
    if (!SafeRead16(officerBase + 0x08, &id) ||
        id < 1 || id > 5102 || seenOfficerIds[id]) {
      continue;
    }
    seenOfficerIds[id] = true;

    if (spouseIds.count(id) != 0)
      spouseTargets[NormalizeOfficerPtr(officerBase)] = id;
  }

  if (spouseTargets.empty()) {
    AddLog(u8"[임신배우자DBG] 배우자 무장 주소를 찾지 못했습니다.");
    return;
  }

  {
    std::vector<PregnancySpouseOption> options;
    options.reserve(spouseTargets.size());
    for (const auto& kv : spouseTargets) {
      PregnancySpouseOption option;
      option.addr = kv.first;
      option.id = kv.second;
      options.push_back(option);
    }
    std::sort(
        options.begin(), options.end(),
        [](const PregnancySpouseOption& a,
           const PregnancySpouseOption& b) {
          return a.id < b.id;
        });

    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancyCurrentSpouses = std::move(options);
  }

  AddLog(
      u8"[임신배우자DBG] 검색 시작: Hero ID %u / 배우자 %zu명 / +00 배우자 포인터 기준",
      heroId, spouseTargets.size());

  g_pregnancySpouseScanning = true;
  g_pregnancySpouseProgress = 0.0f;
  g_pregnancySpouseResultsReady = false;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancySpouseResults.clear();
    g_pregnancySpouseTotalHits = 0;
  }

  std::thread([spouseTargets = std::move(spouseTargets)]() {
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    unsigned long long totalSize = 0;
    uintptr_t addr = 0;

    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
      if (IsPregnancyDebugScanRegion(mbi)) {
        regions.push_back(mbi);
        totalSize += mbi.RegionSize;
      }

      const uintptr_t next =
          (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      if (next <= addr)
        break;
      addr = next;
    }

    constexpr size_t kChunkSize = 256 * 1024;
    constexpr size_t kMaxStoredHits = 256;
    std::vector<uint8_t> buffer(kChunkSize);
    std::vector<PregnancySpouseDebugHit> hits;
    hits.reserve(32);
    std::unordered_set<uintptr_t> seenBases;
    unsigned long long processedSize = 0;
    size_t totalHits = 0;

    for (const auto& region : regions) {
      const uintptr_t regionStart = (uintptr_t)region.BaseAddress;
      const uintptr_t regionEnd = regionStart + region.RegionSize;

      for (uintptr_t curr = regionStart; curr < regionEnd;) {
        const size_t remaining = (size_t)(regionEnd - curr);
        const size_t toRead = (std::min)(remaining, kChunkSize);

        if (SafeReadMem(curr, buffer.data(), toRead)) {
          size_t i = (size_t)((8 - (curr & 7)) & 7);
          for (; i + sizeof(uintptr_t) <= toRead;
               i += sizeof(uintptr_t)) {
            uintptr_t rawPtr = 0;
            memcpy(&rawPtr, buffer.data() + i, sizeof(rawPtr));

            auto spouseIt =
                spouseTargets.find(NormalizeOfficerPtr(rawPtr));
            if (spouseIt == spouseTargets.end())
              continue;

            // 실측된 후보 #1에서 배우자 포인터는 record +0x00.
            const uintptr_t recordBase = curr + i;
            if (!seenBases.insert(recordBase).second)
              continue;

            PregnancyDebugRecordDump dump;
            if (!ReadPregnancyDebugRecord(recordBase, &dump))
              continue;
            if (NormalizeOfficerPtr(dump.q00) != spouseIt->first)
              continue;

            totalHits++;
            if (hits.size() >= kMaxStoredHits)
              continue;

            PregnancySpouseDebugHit hit;
            hit.spouseId = spouseIt->second;
            hit.spouseAddr = spouseIt->first;
            hit.recordBase = recordBase;
            hit.record = dump;

            // 실측 결과:
            // 출산 전 손상향(ID 565): +09=1, +0A=3, +10=0
            // 출산 직후 같은 배우자: +09=1, +0A=0, +10=자녀 4001
            // 따라서 +09는 임신 여부 자체가 아니라 활성/사용 플래그 계열로 보고,
            // +0A를 남은 임신 개월 후보로 우선 평가합니다.
            if (dump.pregnancyFlag == 1)
              hit.score += 80;
            if (dump.remainingMonths >= 1 &&
                dump.remainingMonths <= 12)
              hit.score += 120;
            else if (dump.remainingMonths == 0)
              hit.score += 20;
            if (dump.childPtr == 0)
              hit.score += 30;
            else if (dump.childOfficerId != 0)
              hit.score += 60;
            if (PregnancyCapsLookPlausible(dump))
              hit.score += 20;

            // 실제 임신/배우자 테이블이라면 0x28 stride 주변 슬롯의 +00에도
            // 현재 배우자 포인터가 연속해서 배치될 가능성이 높습니다.
            // rel -3..+3을 읽어 현재 배우자 ID가 몇 개 잡히는지 기록합니다.
            for (int rel = -3; rel <= 3; ++rel) {
              const intptr_t slotSigned =
                  (intptr_t)recordBase + (intptr_t)rel * 0x28;
              if (slotSigned <= 0x10000)
                continue;

              uintptr_t neighborPtr = 0;
              if (!SafeReadPtr((uintptr_t)slotSigned, &neighborPtr))
                continue;

              auto neighborIt =
                  spouseTargets.find(NormalizeOfficerPtr(neighborPtr));
              if (neighborIt == spouseTargets.end())
                continue;

              hit.neighborSpouseIds[(size_t)(rel + 3)] =
                  neighborIt->second;
              hit.neighborSpouseMatches++;
            }

            if (hit.neighborSpouseMatches >= 2)
              hit.score += 200;
            else if (hit.neighborSpouseMatches == 1)
              hit.score += 20;

            hits.push_back(hit);
          }
        }

        processedSize += toRead;
        if (totalSize > 0) {
          g_pregnancySpouseProgress =
              (float)((double)processedSize / (double)totalSize);
        }
        curr += toRead;
      }
    }

    std::stable_sort(
        hits.begin(), hits.end(),
        [](const PregnancySpouseDebugHit& a,
           const PregnancySpouseDebugHit& b) {
          if (a.score != b.score)
            return a.score > b.score;
          if (a.spouseId != b.spouseId)
            return a.spouseId < b.spouseId;
          return a.recordBase < b.recordBase;
        });

    PregnancyCanonicalTable canonical;
    TryBuildCanonicalPregnancyTable(hits, &canonical);

    {
      std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
      g_pregnancySpouseResults = std::move(hits);
      g_pregnancySpouseTotalHits = totalHits;
      g_pregnancyCanonicalTable = canonical;
    }

    g_pregnancySpouseProgress = 1.0f;
    g_pregnancySpouseScanning = false;
    g_pregnancySpouseResultsReady = true;
  }).detach();
}

static void FlushPregnancySpouseDebugResults() {
  if (!g_pregnancySpouseResultsReady.exchange(false))
    return;

  std::vector<PregnancySpouseDebugHit> hits;
  size_t totalHits = 0;
  PregnancyCanonicalTable canonical;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    hits = g_pregnancySpouseResults;
    totalHits = g_pregnancySpouseTotalHits;
    canonical = g_pregnancyCanonicalTable;
  }

  AddLog(
      u8"[임신배우자DBG] 검색 완료: +00 배우자 포인터 후보 %zu개 / 저장 %zu개",
      totalHits, hits.size());

  if (canonical.valid) {
    AddLog(
        u8"[임신배우자DBG] canonical 3-slot table 발견: base=%p / stride=0x28",
        (void*)canonical.base);

    for (int slot = 0; slot < 3; ++slot) {
      const PregnancyDebugRecordDump& d =
          canonical.slots[(size_t)slot];
      AddLog(
          u8"[임신배우자DBG]   slot%d record=%p 배우자 ID=%u | +08 cooldown=%u(가능도=%d%%) +09=%u +0A=%u | +10=%p(ID:%u) | +1E..22=%u,%u,%u,%u,%u",
          slot,
          (void*)(canonical.base + (uintptr_t)slot * 0x28),
          canonical.spouseIds[(size_t)slot],
          (unsigned)d.pregnancyCooldown,
          d.pregnancyCooldown <= 100
              ? 100 - (int)d.pregnancyCooldown
              : -1,
          (unsigned)d.pregnancyFlag,
          (unsigned)d.remainingMonths,
          (void*)d.childPtr, d.childOfficerId,
          (unsigned)d.capRaw[0],
          (unsigned)d.capRaw[1],
          (unsigned)d.capRaw[2],
          (unsigned)d.capRaw[3],
          (unsigned)d.capRaw[4]);
    }
  } else {
    AddLog(
        u8"[임신배우자DBG] canonical 3-slot table 자동 식별 실패");
  }

  constexpr size_t kMaxLogHits = 40;
  const size_t logCount = (std::min)(hits.size(), kMaxLogHits);
  for (size_t i = 0; i < logCount; ++i) {
    const PregnancySpouseDebugHit& hit = hits[i];
    const PregnancyDebugRecordDump& d = hit.record;
    const char* state =
        (d.pregnancyFlag == 1 &&
         d.remainingMonths >= 1 &&
         d.remainingMonths <= 12 &&
         d.childPtr == 0)
            ? u8"임신중 후보"
            : (d.pregnancyFlag == 1 &&
               d.remainingMonths == 0 &&
               d.childOfficerId != 0)
                  ? u8"출산완료 후보"
                  : u8"기타";

    AddLog(
        u8"[임신배우자DBG] 후보 #%zu | %s | score=%d | 배우자 ID %u addr=%p | record=%p | +09=%u +0A=%u | +10=%p(ID:%u) | +1E..22=%u,%u,%u,%u,%u | 주변배우자=%d [%u,%u,%u,%u,%u,%u,%u]",
        i + 1, state, hit.score, hit.spouseId,
        (void*)hit.spouseAddr, (void*)hit.recordBase,
        (unsigned)d.pregnancyFlag,
        (unsigned)d.remainingMonths,
        (void*)d.childPtr, d.childOfficerId,
        (unsigned)d.capRaw[0],
        (unsigned)d.capRaw[1],
        (unsigned)d.capRaw[2],
        (unsigned)d.capRaw[3],
        (unsigned)d.capRaw[4],
        hit.neighborSpouseMatches,
        hit.neighborSpouseIds[0],
        hit.neighborSpouseIds[1],
        hit.neighborSpouseIds[2],
        hit.neighborSpouseIds[3],
        hit.neighborSpouseIds[4],
        hit.neighborSpouseIds[5],
        hit.neighborSpouseIds[6]);
  }

  if (hits.size() > kMaxLogHits) {
    AddLog(
        u8"[임신배우자DBG] 로그는 앞 %zu개 후보까지만 출력했습니다. (저장 후보 %zu개)",
        kMaxLogHits, hits.size());
  }

  size_t pregnantCount = 0;
  for (const PregnancySpouseDebugHit& hit : hits) {
    const PregnancyDebugRecordDump& d = hit.record;
    if (d.pregnancyFlag == 1 &&
        d.remainingMonths >= 1 &&
        d.remainingMonths <= 12 &&
        d.childPtr == 0) {
      pregnantCount++;
    }
  }

  AddLog(
      u8"[임신배우자DBG] 실측 규칙 임신중 후보: %zu개 | 기준 +09=1, +0A=1..12, +10=NULL",
      pregnantCount);
  AddLog(
      u8"[임신배우자DBG] read-only 진단입니다. +09는 활성/사용 플래그 계열로 보고 직접 수정하지 않습니다.");
}

static void StartPregnancyDebugScanAsync() {
  if (g_pregnancyDebugScanning.load())
    return;

  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    AddLog(
        u8"[임신DBG] 주인공/무장 배열 주소 해석 실패");
    return;
  }

  // 가장 최근 출생 자녀 하나만 앵커로 사용합니다.
  // 출생 직전/직후 비교에서 막 태어난 자녀 포인터가 가장 강한 표식입니다.
  const ChildEntry* targetChild = nullptr;
  for (const auto& kv : g_children) {
    const ChildEntry& child = kv.second;
    if (!targetChild ||
        child.birthYear > targetChild->birthYear ||
        (child.birthYear == targetChild->birthYear &&
         child.id > targetChild->id)) {
      targetChild = &child;
    }
  }

  if (!targetChild ||
      NormalizeOfficerPtr(targetChild->addr) <= 0x10000) {
    AddLog(
        u8"[임신DBG] 현재 주인공의 자녀 주소가 없어 검색을 시작할 수 없습니다.");
    return;
  }

  const uintptr_t targetChildAddr =
      NormalizeOfficerPtr(targetChild->addr);
  const uint16_t targetChildId = targetChild->id;
  const uint16_t targetChildBirth = targetChild->birthYear;

  // 현재 배우자 ID를 관계 테이블에서 얻고, 실제 무장 배열 주소와 연결합니다.
  std::unordered_set<uint16_t> spouseIds;
  OfficerRelationshipInfo relInfo;
  if (GetOfficerRelationshipInfo(heroMaster, relInfo) &&
      relInfo.valid) {
    for (uint16_t id : relInfo.spouses)
      spouseIds.insert(id);
  }

  std::unordered_map<uintptr_t, uint16_t> spouseTargets;
  if (!spouseIds.empty()) {
    bool seenOfficerIds[5103] = {};
    for (int i = 0; i < 5102; ++i) {
      const uintptr_t officerBase =
          rosterBase + (uintptr_t)i * 0x3D0;

      uint16_t id = 0;
      if (!SafeRead16(officerBase + 0x08, &id) ||
          id < 1 || id > 5102 ||
          seenOfficerIds[id]) {
        continue;
      }
      seenOfficerIds[id] = true;

      if (spouseIds.count(id) != 0) {
        spouseTargets[
            NormalizeOfficerPtr(officerBase)] = id;
      }
    }
  }

  AddLog(
      u8"[임신DBG] 집중 검색 시작: Hero ID %u / 최신 자녀 ID %u (출생 %u) / 배우자 %zu명",
      heroId, targetChildId, targetChildBirth,
      spouseTargets.size());

  g_pregnancyDebugScanning = true;
  g_pregnancyDebugProgress = 0.0f;
  g_pregnancyDebugResultsReady = false;
  {
    std::lock_guard<std::mutex> lock(
        g_pregnancyDebugMutex);
    g_pregnancyDebugResults.clear();
    g_pregnancyDebugTotalHits = 0;
  }

  std::thread([
      targetChildAddr,
      targetChildId,
      spouseTargets = std::move(spouseTargets)]() {
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    unsigned long long totalSize = 0;
    uintptr_t addr = 0;

    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
      if (IsPregnancyDebugScanRegion(mbi)) {
        regions.push_back(mbi);
        totalSize += mbi.RegionSize;
      }

      const uintptr_t next =
          (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      if (next <= addr)
        break;
      addr = next;
    }

    constexpr size_t kChunkSize = 256 * 1024;
    constexpr size_t kMaxStoredHits = 512;
    std::vector<uint8_t> buffer(kChunkSize);
    std::vector<PregnancyDebugHit> hits;
    hits.reserve(64);
    std::unordered_set<uintptr_t> seenRecordBases;
    unsigned long long processedSize = 0;
    size_t totalHits = 0;

    for (const auto& region : regions) {
      const uintptr_t regionStart =
          (uintptr_t)region.BaseAddress;
      const uintptr_t regionEnd =
          regionStart + region.RegionSize;

      for (uintptr_t curr = regionStart;
           curr < regionEnd;) {
        const size_t remaining =
            (size_t)(regionEnd - curr);
        const size_t toRead =
            (std::min)(remaining, kChunkSize);

        if (SafeReadMem(curr, buffer.data(), toRead)) {
          size_t i =
              (size_t)((8 - (curr & 7)) & 7);
          for (; i + sizeof(uintptr_t) <= toRead;
               i += sizeof(uintptr_t)) {
            uintptr_t rawPtr = 0;
            memcpy(&rawPtr, buffer.data() + i,
                   sizeof(rawPtr));

            if (NormalizeOfficerPtr(rawPtr) !=
                targetChildAddr) {
              continue;
            }

            const uintptr_t refAddr = curr + i;
            if (refAddr < 0x10)
              continue;

            const uintptr_t recordBase = refAddr - 0x10;
            if (!seenRecordBases.insert(recordBase).second)
              continue;

            PregnancyDebugRecordDump center;
            if (!ReadPregnancyDebugRecord(
                    recordBase, &center)) {
              continue;
            }

            if (NormalizeOfficerPtr(center.childPtr) !=
                targetChildAddr) {
              continue;
            }

            totalHits++;
            if (hits.size() >= kMaxStoredHits)
              continue;

            PregnancyDebugHit hit;
            hit.anchorChildId = targetChildId;
            hit.anchorChildAddr = targetChildAddr;
            hit.childPtrRefAddr = refAddr;
            hit.recordBase = recordBase;
            hit.expectedFlagMonthRange =
                center.pregnancyFlag <= 1 &&
                center.remainingMonths <= 12;

            // 배우자 포인터의 정확한 필드 위치는 아직 미확정입니다.
            // 0x28 레코드 안의 qword 경계들을 모두 비교해 후보를 우선순위화합니다.
            static constexpr int kProbeOffsets[] = {
                0x00, 0x08, 0x18, 0x20
            };
            for (int off : kProbeOffsets) {
              uintptr_t p = 0;
              if (!SafeReadPtr(recordBase + off, &p))
                continue;

              auto spouseIt = spouseTargets.find(
                  NormalizeOfficerPtr(p));
              if (spouseIt == spouseTargets.end())
                continue;

              hit.spouseMatchId = spouseIt->second;
              hit.spouseMatchOffset = off;
              break;
            }

            if (hit.spouseMatchId != 0)
              hit.score += 100;
            if (hit.expectedFlagMonthRange)
              hit.score += 20;
            if (center.childOfficerId == targetChildId)
              hit.score += 10;

            for (int rel = -2; rel <= 2; ++rel) {
              const intptr_t slotSigned =
                  (intptr_t)recordBase +
                  (intptr_t)rel * 0x28;
              if (slotSigned <= 0x10000)
                continue;

              ReadPregnancyDebugRecord(
                  (uintptr_t)slotSigned,
                  &hit.neighborhood[
                      (size_t)(rel + 2)]);
            }

            hits.push_back(hit);
          }
        }

        processedSize += toRead;
        if (totalSize > 0) {
          g_pregnancyDebugProgress =
              (float)((double)processedSize /
                      (double)totalSize);
        }

        curr += toRead;
      }
    }

    std::stable_sort(
        hits.begin(), hits.end(),
        [](const PregnancyDebugHit& a,
           const PregnancyDebugHit& b) {
          if (a.score != b.score)
            return a.score > b.score;
          return a.recordBase < b.recordBase;
        });

    {
      std::lock_guard<std::mutex> lock(
          g_pregnancyDebugMutex);
      g_pregnancyDebugResults = std::move(hits);
      g_pregnancyDebugTotalHits = totalHits;
    }

    g_pregnancyDebugProgress = 1.0f;
    g_pregnancyDebugScanning = false;
    g_pregnancyDebugResultsReady = true;
  }).detach();
}

static void FlushPregnancyDebugResults() {
  if (!g_pregnancyDebugResultsReady.exchange(false))
    return;

  std::vector<PregnancyDebugHit> hits;
  size_t totalHits = 0;
  {
    std::lock_guard<std::mutex> lock(
        g_pregnancyDebugMutex);
    hits = g_pregnancyDebugResults;
    totalHits = g_pregnancyDebugTotalHits;
  }

  AddLog(
      u8"[임신DBG] 집중 검색 완료: 최신 자녀 pointer(+0x10) 역참조 후보 %zu개 / 저장 %zu개",
      totalHits, hits.size());

  constexpr size_t kMaxLogHits = 64;
  const size_t logCount =
      (std::min)(hits.size(), kMaxLogHits);

  for (size_t i = 0; i < logCount; ++i) {
    const PregnancyDebugHit& hit = hits[i];
    AddLog(
        u8"[임신DBG] 후보 #%zu | score=%d | 자녀 ID %u addr=%p | ref=%p -> record=%p | 배우자 ID=%u off=%s | +09/+0A 범위=%s",
        i + 1, hit.score, hit.anchorChildId,
        (void*)hit.anchorChildAddr,
        (void*)hit.childPtrRefAddr,
        (void*)hit.recordBase,
        hit.spouseMatchId,
        hit.spouseMatchOffset >= 0
            ? (hit.spouseMatchOffset == 0x00 ? "+00" :
               hit.spouseMatchOffset == 0x08 ? "+08" :
               hit.spouseMatchOffset == 0x18 ? "+18" :
               hit.spouseMatchOffset == 0x20 ? "+20" : "?")
            : "-",
        hit.expectedFlagMonthRange ? "OK" : "RAW");

    for (int rel = -2; rel <= 2; ++rel) {
      const PregnancyDebugRecordDump& d =
          hit.neighborhood[(size_t)(rel + 2)];
      if (!d.valid) {
        AddLog(
            u8"[임신DBG]   rel %+d (stride 0x28) 읽기 실패",
            rel);
        continue;
      }

      AddLog(
          u8"[임신DBG]   rel %+d base=%p | +00=%p(ID:%u) +08=%p(ID:%u) | +09=%u +0A=%u | +10=%p(ID:%u) | +1E..22=%u,%u,%u,%u,%u",
          rel, (void*)d.base,
          (void*)d.q00, d.q00OfficerId,
          (void*)d.q08, d.q08OfficerId,
          (unsigned)d.pregnancyFlag,
          (unsigned)d.remainingMonths,
          (void*)d.childPtr, d.childOfficerId,
          (unsigned)d.capRaw[0],
          (unsigned)d.capRaw[1],
          (unsigned)d.capRaw[2],
          (unsigned)d.capRaw[3],
          (unsigned)d.capRaw[4]);
    }
  }

  if (hits.size() > kMaxLogHits) {
    AddLog(
        u8"[임신DBG] 로그는 앞 %zu개 후보까지만 출력했습니다. (저장 후보 %zu개)",
        kMaxLogHits, hits.size());
  }

  AddLog(
      u8"[임신DBG] 모든 값은 읽기 전용 진단입니다. 아직 임신 record base로 확정하지 않습니다.");
}

static bool IsPregnancySlotSafeForSwap(
    const PregnancyDebugRecordDump& d) {
  return d.valid &&
         d.pregnancyFlag == 0 &&
         d.remainingMonths == 0 &&
         d.childPtr == 0;
}

static std::vector<PregnancySpouseOption>
GetPregnancyOutsideSpouseOptions(
    const PregnancyCanonicalTable& table) {
  std::vector<PregnancySpouseOption> allSpouses;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    allSpouses = g_pregnancyCurrentSpouses;
  }

  std::vector<PregnancySpouseOption> outside;
  for (const PregnancySpouseOption& option : allSpouses) {
    bool inSlot = false;
    for (uint16_t slotId : table.spouseIds) {
      if (slotId == option.id) {
        inSlot = true;
        break;
      }
    }
    if (!inSlot)
      outside.push_back(option);
  }
  return outside;
}

static bool TryWritePregnancySlotRaw(
    uintptr_t slotAddr,
    uintptr_t newSpouseAddr) {
  DWORD oldProt = 0;
  DWORD tmpProt = 0;
  bool wrote = false;

  __try {
    if (!VirtualProtect(
            (LPVOID)slotAddr, 0x28,
            PAGE_READWRITE, &oldProt)) {
      return false;
    }

    *(uintptr_t*)(slotAddr + 0x00) = newSpouseAddr;
    *(uint8_t*)(slotAddr + 0x08) = 100;
    *(uint8_t*)(slotAddr + 0x09) = 0;
    *(uint8_t*)(slotAddr + 0x0A) = 0;
    *(uintptr_t*)(slotAddr + 0x10) = 0;
    memset((void*)(slotAddr + 0x1E), 0, 5);
    wrote = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    wrote = false;
  }

  if (oldProt != 0) {
    VirtualProtect(
        (LPVOID)slotAddr, 0x28,
        oldProt, &tmpProt);
  }

  return wrote;
}

static bool TryRestorePregnancySlotRaw(
    uintptr_t slotAddr,
    const uint8_t* backup,
    size_t backupSize) {
  if (!backup || backupSize != 0x28)
    return false;

  DWORD oldProt = 0;
  DWORD tmpProt = 0;
  bool restored = false;

  __try {
    if (!VirtualProtect(
            (LPVOID)slotAddr, 0x28,
            PAGE_READWRITE, &oldProt)) {
      return false;
    }

    memcpy(
        (void*)slotAddr,
        backup,
        backupSize);
    restored = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    restored = false;
  }

  if (oldProt != 0) {
    VirtualProtect(
        (LPVOID)slotAddr, 0x28,
        oldProt, &tmpProt);
  }

  return restored;
}

static bool ApplyPregnancySpouseSlotSwap(
    int slotIndex,
    uint16_t newSpouseId) {
  if (slotIndex < 0 || slotIndex >= 3 ||
      newSpouseId == 0) {
    return false;
  }

  PregnancyCanonicalTable snapshot =
      GetCanonicalPregnancySnapshot();
  if (!snapshot.valid) {
    AddLog(
        u8"[임신슬롯교체] canonical 3슬롯을 먼저 검색해야 합니다.");
    return false;
  }

  for (uint16_t slotId : snapshot.spouseIds) {
    if (slotId == newSpouseId) {
      AddLog(
          u8"[임신슬롯교체] ID %u는 이미 임신 슬롯에 있습니다.",
          newSpouseId);
      return false;
    }
  }

  const uintptr_t slotAddr =
      snapshot.base + (uintptr_t)slotIndex * 0x28;

  PregnancyDebugRecordDump current;
  if (!ReadPregnancyDebugRecord(slotAddr, &current) ||
      current.q00OfficerId !=
          snapshot.spouseIds[(size_t)slotIndex]) {
    AddLog(
        u8"[임신슬롯교체] slot%d 현재 데이터 재검증 실패",
        slotIndex + 1);
    return false;
  }

  if (!IsPregnancySlotSafeForSwap(current)) {
    AddLog(
        u8"[임신슬롯교체] slot%d 배우자 ID %u는 비임신 빈 상태가 아니어서 교체 차단",
        slotIndex + 1, current.q00OfficerId);
    return false;
  }

  uintptr_t newSpouseAddr = 0;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    for (const PregnancySpouseOption& option :
         g_pregnancyCurrentSpouses) {
      if (option.id == newSpouseId) {
        newSpouseAddr = NormalizeOfficerPtr(option.addr);
        break;
      }
    }
  }

  if (newSpouseAddr <= 0x10000 ||
      TryReadOfficerIdFromPointer(newSpouseAddr) != newSpouseId) {
    AddLog(
        u8"[임신슬롯교체] 새 배우자 ID %u의 무장 주소 검증 실패",
        newSpouseId);
    return false;
  }

  std::array<uint8_t, 0x28> backup{};
  if (!SafeReadMem(
          slotAddr, backup.data(), backup.size())) {
    AddLog(
        u8"[임신슬롯교체] slot%d 백업 읽기 실패",
        slotIndex + 1);
    return false;
  }

  // CETRAINER spouseSlotRelocation()의 슬롯 초기화/재배치와
  // 동일한 필드만 선택 슬롯 하나에 적용합니다.
  // __try는 C++ 객체를 가진 함수에서 쓸 수 없으므로 POD 전용 헬퍼로 분리합니다.
  const bool wrote =
      TryWritePregnancySlotRaw(
          slotAddr, newSpouseAddr);

  if (!wrote) {
    AddLog(
        u8"[임신슬롯교체] slot%d 쓰기 실패",
        slotIndex + 1);
  }

  PregnancyDebugRecordDump verify;
  const bool verified =
      wrote &&
      ReadPregnancyDebugRecord(slotAddr, &verify) &&
      verify.q00OfficerId == newSpouseId &&
      verify.pregnancyCooldown == 100 &&
      verify.pregnancyFlag == 0 &&
      verify.remainingMonths == 0 &&
      verify.childPtr == 0 &&
      std::all_of(
          verify.capRaw.begin(), verify.capRaw.end(),
          [](uint8_t v) { return v == 0; });

  if (!verified) {
    const bool restored =
        TryRestorePregnancySlotRaw(
            slotAddr,
            backup.data(),
            backup.size());

    AddLog(
        u8"[임신슬롯교체] slot%d 적용 검증 실패 -> 원복 %s",
        slotIndex + 1,
        restored ? u8"성공" : u8"실패");
    return false;
  }

  const uint16_t oldSpouseId =
      current.q00OfficerId;

  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancyCanonicalTable.spouseIds[
        (size_t)slotIndex] = newSpouseId;
    g_pregnancyCanonicalTable.slots[
        (size_t)slotIndex] = verify;

    // 교체되어 슬롯 밖으로 나온 배우자도 다음 교체 대상으로 유지합니다.
    bool oldFound = false;
    for (PregnancySpouseOption& option :
         g_pregnancyCurrentSpouses) {
      if (option.id == oldSpouseId) {
        option.addr = NormalizeOfficerPtr(current.q00);
        oldFound = true;
        break;
      }
    }
    if (!oldFound) {
      PregnancySpouseOption oldOption;
      oldOption.id = oldSpouseId;
      oldOption.addr = NormalizeOfficerPtr(current.q00);
      g_pregnancyCurrentSpouses.push_back(oldOption);
    }
  }

  g_pregnancySwapSpouseId = 0;

  AddLog(
      u8"[임신슬롯교체] slot%d 배우자 교체 성공: ID %u -> ID %u / cooldown=100(가능도 0%%)",
      slotIndex + 1, oldSpouseId, newSpouseId);
  return true;
}

static bool RefreshCanonicalPregnancyTableCached() {
  std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);

  if (!g_pregnancyCanonicalTable.valid ||
      g_pregnancyCanonicalTable.base <= 0x10000) {
    return false;
  }

  PregnancyCanonicalTable refreshed =
      g_pregnancyCanonicalTable;

  for (int slot = 0; slot < 3; ++slot) {
    PregnancyDebugRecordDump d;
    const uintptr_t slotAddr =
        refreshed.base + (uintptr_t)slot * 0x28;

    if (!ReadPregnancyDebugRecord(slotAddr, &d) ||
        d.q00OfficerId == 0 ||
        d.q00OfficerId != refreshed.spouseIds[(size_t)slot]) {
      g_pregnancyCanonicalTable.valid = false;
      return false;
    }

    refreshed.slots[(size_t)slot] = d;
  }

  g_pregnancyCanonicalTable = refreshed;
  return true;
}

static PregnancyCanonicalTable GetCanonicalPregnancySnapshot() {
  std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
  return g_pregnancyCanonicalTable;
}

static const char* GetPregnancySlotState(
    const PregnancyDebugRecordDump& d) {
  if (d.pregnancyFlag == 1 &&
      d.remainingMonths >= 1 &&
      d.remainingMonths <= 12 &&
      d.childPtr == 0) {
    return u8"임신 중";
  }

  if (d.pregnancyFlag == 1 &&
      d.remainingMonths == 0 &&
      d.childOfficerId != 0) {
    return u8"출산 완료";
  }

  if (d.pregnancyFlag == 0 &&
      d.remainingMonths == 0 &&
      d.childPtr == 0) {
    return u8"비임신";
  }

  return u8"미확인";
}

static bool RefreshChild(ChildEntry& e) {
  if (!e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  __try {
    const uint16_t id = *(uint16_t*)(e.addr + 0x08);
    if (id != e.id)
      return false;

    e.appearanceYear = *(uint16_t*)(e.addr + 0x32);
    e.birthYear = *(uint16_t*)(e.addr + 0x34);
    e.deathYear = *(uint16_t*)(e.addr + 0x36);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool ApplyChildSchedule(ChildEntry& e) {
  if (!e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  unsigned short currentYear = 0;
  if (!ReadScenarioYear(&currentYear) || currentYear < 171 || currentYear >= 270)
    return false;

  e.yearsLater = (std::max)(1, (std::min)(10, e.yearsLater));
  const uint16_t targetAppearance = (uint16_t)(currentYear + e.yearsLater);
  if (targetAppearance >= 270)
    return false;

  // 임관 시 15세가 되도록 출생년도도 함께 조정.
  const uint16_t targetBirth = (uint16_t)(targetAppearance - 15);

  __try {
    if (*(uint16_t*)(e.addr + 0x08) != e.id)
      return false;

    DWORD oldProt = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)(e.addr + 0x32), 4, PAGE_READWRITE, &oldProt))
      return false;

    *(uint16_t*)(e.addr + 0x32) = targetAppearance;
    *(uint16_t*)(e.addr + 0x34) = targetBirth;

    VirtualProtect((LPVOID)(e.addr + 0x32), 4, oldProt, &tmp);

    e.appearanceYear = targetAppearance;
    e.birthYear = targetBirth;
    e.appliedTargetYear = targetAppearance;

    AddLog(u8"[ChildManager] ID %u 임관 예약: %u년 (출생 %u년, %d년 후)",
           e.id, targetAppearance, targetBirth, e.yearsLater);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool RestoreChildSchedule(ChildEntry& e) {
  if (!e.hasOriginalSchedule || !e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  __try {
    if (*(uint16_t*)(e.addr + 0x08) != e.id)
      return false;

    DWORD oldProt = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)(e.addr + 0x32), 4, PAGE_READWRITE, &oldProt))
      return false;

    *(uint16_t*)(e.addr + 0x32) = e.originalAppearanceYear;
    *(uint16_t*)(e.addr + 0x34) = e.originalBirthYear;
    VirtualProtect((LPVOID)(e.addr + 0x32), 4, oldProt, &tmp);

    e.appearanceYear = e.originalAppearanceYear;
    e.birthYear = e.originalBirthYear;
    e.appliedTargetYear = 0;
    e.hasOriginalSchedule = false;

    AddLog(u8"[ChildManager] ID %u 임관 예약 취소: 등장 %u년 / 출생 %u년 복원",
           e.id, e.appearanceYear, e.birthYear);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool ResolveHeroAndRoster(uintptr_t& rosterBase, uintptr_t& heroMaster, uint16_t& heroId) {
  rosterBase = 0;
  heroMaster = 0;
  heroId = 0;

  uintptr_t gameBase = GetGameBase();
  if (!gameBase)
    return false;

  uintptr_t heroLive = 0;
  if (!SafeReadPtr(gameBase + 0xE0, &heroLive) || heroLive <= 0x10000)
    return false;

  if (!SafeRead16(heroLive + 0x08, &heroId) || heroId < 1 || heroId > 5102)
    return false;

  const uintptr_t exeBase = (uintptr_t)GetModuleHandle(nullptr);
  if (!exeBase || !TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) || rosterBase <= 0x10000)
    return false;

  heroMaster = rosterBase + (uintptr_t)(heroId - 1) * 0x3D0;
  uint16_t verify = 0;
  if (!SafeRead16(heroMaster + 0x08, &verify) || verify != heroId)
    return false;

  return true;
}

static void ScanCurrentHeroChildren(bool forceLog) {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ResolveHeroAndRoster(rosterBase, heroMaster, heroId))
    return;

  // 주인공이 교체되면 이전 주인공 기준 목록은 즉시 폐기.
  if (g_lastHeroId != 0 && g_lastHeroId != heroId) {
    g_children.clear();
    AddLog(u8"[ChildManager] 주인공 변경 감지: %u -> %u, 자녀 목록 초기화",
           g_lastHeroId, heroId);
  }
  g_lastHeroId = heroId;

  std::unordered_map<uint16_t, ChildEntry> found;
  found.reserve(8);

  const uintptr_t heroNorm = NormalizeOfficerPtr(heroMaster);

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officerBase = rosterBase + (uintptr_t)i * 0x3D0;

    uint16_t id = 0;
    if (!SafeRead16(officerBase + 0x08, &id) || id < 1 || id > 5102)
      continue;

    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    SafeReadPtr(officerBase + 0x48, &dadPtr);
    SafeReadPtr(officerBase + 0x50, &momPtr);

    const bool hasParentLink =
        (NormalizeOfficerPtr(dadPtr) == heroNorm) ||
        (NormalizeOfficerPtr(momPtr) == heroNorm);

    if (!hasParentLink)
      continue;

    // 일부 미사용/더미 무장 슬롯에도 혈연 포인터 값이 남아 있을 수 있습니다.
    // 실제 자녀 후보는 정상적인 생년/등장년/몰년 데이터를 가진 레코드만 허용합니다.
    uint16_t appearance = 0;
    uint16_t birth = 0;
    uint16_t death = 0;
    if (!SafeRead16(officerBase + 0x32, &appearance) ||
        !SafeRead16(officerBase + 0x34, &birth) ||
        !SafeRead16(officerBase + 0x36, &death))
      continue;

    const bool hasValidLifeYears =
        birth != 0 &&
        appearance != 0 &&
        death != 0 &&
        appearance >= birth &&
        death >= birth;

    if (!hasValidLifeYears)
      continue;

    ChildEntry e;
    auto oldIt = g_children.find(id);
    if (oldIt != g_children.end()) {
      e = oldIt->second; // 체크/연수/예약 상태 유지
    }

    e.id = id;
    e.addr = officerBase;
    RefreshChild(e);
    found[id] = e;
  }

  if (forceLog || found.size() != g_children.size()) {
    AddLog(u8"[ChildManager] 혈연 데이터 기준 자녀 검색 완료: 주인공 ID %u / %zu명",
           heroId, found.size());
    for (const auto& kv : found) {
      const ChildEntry& e = kv.second;
      AddLog(u8"[ChildManager] 자녀 ID %u | 출생 %u 등장 %u 사망 %u",
             e.id, e.birthYear, e.appearanceYear, e.deathYear);
    }
  }

  g_children.swap(found);
}

} // namespace

void EnsureChildManagerCapture() {
  // 더 이상 정보 > 자녀 화면 훅을 사용하지 않습니다.
  // 무장 마스터 배열의 부친(+0x48)/모친(+0x50) 혈연 포인터를 직접 사용합니다.
}

void RunChildManagerUpdate() {
  const ULONGLONG now = GetTickCount64();

  // 주인공이 바뀌었는지 빠르게 확인하기 위해 1초 간격으로 재검색.
  if (g_lastChildScanMs != 0 && (now - g_lastChildScanMs) < 1000)
    return;

  g_lastChildScanMs = now;
  ScanCurrentHeroChildren(false);

  for (auto& kv : g_children)
    RefreshChild(kv.second);

  RefreshCanonicalPregnancyTableCached();

  FlushPregnancyDebugResults();
  FlushPregnancySpouseDebugResults();
}

void DrawChildManagerWindow(float scale) {
  if (!bShowChildManagerWin)
    return;

  RunChildManagerUpdate();

  ImGui::SetNextWindowSize(ImVec2(650.0f * scale, 350.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"자녀 관리###ChildManager", &bShowChildManagerWin)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(1, 1, 0, 1),
                     u8"무장 혈연 데이터의 부친/모친 포인터를 기준으로 현재 주인공의 자녀를 직접 표시합니다.");
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                     u8"체크하거나 연수를 변경하면 그 시점 기준으로 한 번만 임관년도를 적용합니다.");

  if (ImGui::Button(u8"목록 새로고침", ImVec2(110.0f * scale, 0))) {
    ScanCurrentHeroChildren(true);
  }

  ImGui::Separator();

  if (g_children.empty()) {
    ImGui::TextUnformatted(u8"현재 주인공의 자녀가 없습니다.");
  } else if (ImGui::BeginTable("ChildManagerTable", 7,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(u8"적용", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
    ImGui::TableSetupColumn(u8"자녀", ImGuiTableColumnFlags_WidthFixed, 120.0f * scale);
    ImGui::TableSetupColumn(u8"출생", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"등장", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"사망", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"몇 년 후", ImGuiTableColumnFlags_WidthFixed, 85.0f * scale);
    ImGui::TableSetupColumn(u8"임관예정일", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    std::vector<uint16_t> ids;
    ids.reserve(g_children.size());
    for (const auto& kv : g_children)
      ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    for (uint16_t id : ids) {
      ChildEntry& e = g_children[id];
      ImGui::PushID((int)id);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      bool selected = e.selected;
      if (ImGui::Checkbox("##select", &selected)) {
        if (selected) {
          if (!e.hasOriginalSchedule) {
            e.originalAppearanceYear = e.appearanceYear;
            e.originalBirthYear = e.birthYear;
            e.hasOriginalSchedule = true;
          }

          if (ApplyChildSchedule(e)) {
            e.selected = true;
          } else {
            e.selected = false;
            e.hasOriginalSchedule = false;
            AddLog(u8"[ChildManager] ID %u 임관 예약 적용 실패", e.id);
          }
        } else {
          if (RestoreChildSchedule(e)) {
            e.selected = false;
          } else {
            // 복원에 실패했다면 실제 메모리 예약은 남아 있을 수 있으므로 체크 상태를 유지합니다.
            e.selected = true;
            AddLog(u8"[ChildManager] ID %u 임관 예약 취소 실패", e.id);
          }
        }
      }

      ImGui::TableNextColumn();
      auto nameIt = g_officerNames.find(id);
      if (nameIt != g_officerNames.end() && !nameIt->second.empty())
        ImGui::Text("%s (%u)", nameIt->second.c_str(), id);
      else
        ImGui::Text(u8"자녀 ID %u", id);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.birthYear);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.appearanceYear);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.deathYear);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(55.0f * scale);
      int years = e.yearsLater;
      if (ImGui::InputInt("##years", &years, 0, 0)) {
        e.yearsLater = (std::max)(1, (std::min)(10, years));
        if (e.selected)
          ApplyChildSchedule(e);
      }

      ImGui::TableNextColumn();
      if (e.appliedTargetYear)
        ImGui::Text(u8"%u년", e.appliedTargetYear);
      else
        ImGui::TextUnformatted(u8"-");

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 선택된 자녀만 등장년도와 출생년도를 함께 조정하며 사망년도는 변경하지 않습니다.");
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 자녀 출생/임관/주인공 변경은 혈연 데이터를 다시 읽어 목록에 자동 반영합니다.");

  ImGui::Separator();
  ImGui::TextUnformatted(u8"임신 상태 (게임 기본 3슬롯)");

  const PregnancyCanonicalTable pregnancy =
      GetCanonicalPregnancySnapshot();

  if (pregnancy.valid) {
    if (ImGui::BeginTable(
            "PregnancyStatusTable", 6,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn(
          u8"슬롯", ImGuiTableColumnFlags_WidthFixed,
          45.0f * scale);
      ImGui::TableSetupColumn(
          u8"배우자", ImGuiTableColumnFlags_WidthFixed,
          135.0f * scale);
      ImGui::TableSetupColumn(
          u8"상태", ImGuiTableColumnFlags_WidthFixed,
          85.0f * scale);
      ImGui::TableSetupColumn(
          u8"임신 가능도", ImGuiTableColumnFlags_WidthFixed,
          85.0f * scale);
      ImGui::TableSetupColumn(
          u8"출산까지", ImGuiTableColumnFlags_WidthFixed,
          75.0f * scale);
      ImGui::TableSetupColumn(
          u8"자녀", ImGuiTableColumnFlags_WidthFixed,
          90.0f * scale);
      ImGui::TableHeadersRow();

      for (int slot = 0; slot < 3; ++slot) {
        const PregnancyDebugRecordDump& d =
            pregnancy.slots[(size_t)slot];
        const uint16_t spouseId =
            pregnancy.spouseIds[(size_t)slot];

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::Text("%d", slot + 1);

        ImGui::TableNextColumn();
        auto spouseNameIt = g_officerNames.find(spouseId);
        if (spouseNameIt != g_officerNames.end() &&
            !spouseNameIt->second.empty()) {
          ImGui::Text(
              "%s (%u)",
              spouseNameIt->second.c_str(), spouseId);
        } else {
          ImGui::Text(u8"ID %u", spouseId);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(GetPregnancySlotState(d));

        ImGui::TableNextColumn();
        if (d.pregnancyFlag != 1 &&
            d.pregnancyCooldown <= 100) {
          ImGui::Text(
              "%u%%",
              (unsigned)(100 - d.pregnancyCooldown));
        } else {
          ImGui::TextUnformatted(u8"-");
        }

        ImGui::TableNextColumn();
        if (d.pregnancyFlag == 1 &&
            d.remainingMonths >= 1 &&
            d.remainingMonths <= 12) {
          ImGui::Text(u8"%u개월",
                      (unsigned)d.remainingMonths);
        } else {
          ImGui::TextUnformatted(u8"-");
        }

        ImGui::TableNextColumn();
        if (d.childOfficerId != 0)
          ImGui::Text(u8"ID %u", d.childOfficerId);
        else
          ImGui::TextUnformatted(u8"-");
      }

      ImGui::EndTable();
    }

    const std::vector<PregnancySpouseOption> outsideSpouses =
        GetPregnancyOutsideSpouseOptions(pregnancy);

    std::vector<int> swappableSlots;
    for (int slot = 0; slot < 3; ++slot) {
      if (IsPregnancySlotSafeForSwap(
              pregnancy.slots[(size_t)slot])) {
        swappableSlots.push_back(slot);
      }
    }

    if (!outsideSpouses.empty()) {
      ImGui::Spacing();
      ImGui::TextDisabled(
          u8"3슬롯 밖 배우자 교체 테스트");

      if (std::find(
              swappableSlots.begin(),
              swappableSlots.end(),
              g_pregnancySwapSlot) ==
          swappableSlots.end()) {
        g_pregnancySwapSlot =
            swappableSlots.empty()
                ? -1
                : swappableSlots.front();
      }

      bool selectedOutsideStillValid = false;
      for (const PregnancySpouseOption& option :
           outsideSpouses) {
        if (option.id == g_pregnancySwapSpouseId) {
          selectedOutsideStillValid = true;
          break;
        }
      }
      if (!selectedOutsideStillValid) {
        g_pregnancySwapSpouseId =
            outsideSpouses.front().id;
      }

      const char* targetPreview =
          u8"교체 가능한 슬롯 없음";
      char targetBuffer[128] = {};
      if (g_pregnancySwapSlot >= 0) {
        const uint16_t currentId =
            pregnancy.spouseIds[
                (size_t)g_pregnancySwapSlot];
        auto it = g_officerNames.find(currentId);
        if (it != g_officerNames.end() &&
            !it->second.empty()) {
          snprintf(
              targetBuffer, sizeof(targetBuffer),
              u8"슬롯%d - %s (%u)",
              g_pregnancySwapSlot + 1,
              it->second.c_str(), currentId);
        } else {
          snprintf(
              targetBuffer, sizeof(targetBuffer),
              u8"슬롯%d - ID %u",
              g_pregnancySwapSlot + 1,
              currentId);
        }
        targetPreview = targetBuffer;
      }

      ImGui::SetNextItemWidth(180.0f * scale);
      if (ImGui::BeginCombo(
              u8"교체할 슬롯",
              targetPreview)) {
        for (int slot : swappableSlots) {
          const uint16_t currentId =
              pregnancy.spouseIds[(size_t)slot];
          char label[128] = {};
          auto it = g_officerNames.find(currentId);
          if (it != g_officerNames.end() &&
              !it->second.empty()) {
            snprintf(
                label, sizeof(label),
                u8"슬롯%d - %s (%u)",
                slot + 1,
                it->second.c_str(), currentId);
          } else {
            snprintf(
                label, sizeof(label),
                u8"슬롯%d - ID %u",
                slot + 1, currentId);
          }

          const bool selected =
              g_pregnancySwapSlot == slot;
          if (ImGui::Selectable(label, selected))
            g_pregnancySwapSlot = slot;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine();

      char spousePreview[128] = {};
      const char* spousePreviewText =
          u8"슬롯 밖 배우자";
      for (const PregnancySpouseOption& option :
           outsideSpouses) {
        if (option.id != g_pregnancySwapSpouseId)
          continue;

        auto it = g_officerNames.find(option.id);
        if (it != g_officerNames.end() &&
            !it->second.empty()) {
          snprintf(
              spousePreview, sizeof(spousePreview),
              "%s (%u)",
              it->second.c_str(), option.id);
        } else {
          snprintf(
              spousePreview, sizeof(spousePreview),
              u8"ID %u", option.id);
        }
        spousePreviewText = spousePreview;
        break;
      }

      ImGui::SetNextItemWidth(150.0f * scale);
      if (ImGui::BeginCombo(
              u8"넣을 배우자",
              spousePreviewText)) {
        for (const PregnancySpouseOption& option :
             outsideSpouses) {
          char label[128] = {};
          auto it = g_officerNames.find(option.id);
          if (it != g_officerNames.end() &&
              !it->second.empty()) {
            snprintf(
                label, sizeof(label),
                "%s (%u)",
                it->second.c_str(), option.id);
          } else {
            snprintf(
                label, sizeof(label),
                u8"ID %u", option.id);
          }

          const bool selected =
              g_pregnancySwapSpouseId == option.id;
          if (ImGui::Selectable(label, selected))
            g_pregnancySwapSpouseId = option.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine();

      const bool canSwap =
          g_pregnancySwapSlot >= 0 &&
          g_pregnancySwapSpouseId != 0;

      if (!canSwap)
        ImGui::BeginDisabled();

      if (ImGui::Button(
              u8"슬롯 교체",
              ImVec2(100.0f * scale, 0))) {
        ApplyPregnancySpouseSlotSwap(
            g_pregnancySwapSlot,
            g_pregnancySwapSpouseId);
      }

      if (!canSwap)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"비임신(+09=0/+0A=0/+10=NULL) 슬롯만 교체합니다.");
        ImGui::TextUnformatted(
            u8"임신 중/출산 완료 슬롯은 선택 대상에서 제외합니다.");
        ImGui::TextUnformatted(
            u8"새 슬롯은 cooldown=100(임신 가능도 0%%)으로 초기화합니다.");
        ImGui::EndTooltip();
      }
    }

    ImGui::TextDisabled(
        u8"canonical base=%p / stride=0x28",
        (void*)pregnancy.base);
  } else {
    ImGui::TextDisabled(
        u8"임신 슬롯 주소를 아직 찾지 못했습니다. 아래 검색을 한 번 실행하세요.");
  }

  if (g_pregnancySpouseScanning.load()) {
    ImGui::TextUnformatted(u8"임신 슬롯 검색 중...");
    ImGui::ProgressBar(
        g_pregnancySpouseProgress.load(),
        ImVec2(220.0f * scale, 0));
  } else {
    if (ImGui::Button(
            u8"임신 슬롯 검색",
            ImVec2(130.0f * scale, 0))) {
      StartPregnancySpouseDebugScanAsync();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(
          u8"현재 배우자 포인터를 기준으로 게임 기본 3개 임신 슬롯을 찾습니다.");
      ImGui::TextUnformatted(
          u8"한 번 찾으면 현재 세션에서는 해당 3슬롯을 직접 다시 읽습니다.");
      ImGui::TextUnformatted(
          u8"현재 단계는 읽기 전용이며 메모리는 수정하지 않습니다.");
      ImGui::EndTooltip();
    }
  }

  ImGui::SameLine();

  if (g_pregnancyDebugScanning.load()) {
    ImGui::TextUnformatted(u8"출산 후 구조 확인 중...");
  } else {
    if (ImGui::Button(
            u8"출산 후 DBG",
            ImVec2(110.0f * scale, 0))) {
      StartPregnancyDebugScanAsync();
    }
  }

  ImGui::End();
}

} // namespace DX11Base
