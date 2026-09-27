#include "../../pch.h"
#include "../../Cheats.h"
#include "PregnancyManager.h"
#include "ChildEarlyAppearance.h"
#include "../../MenuState.h"
#include "../../Cheats/System/MonthCapture.h"
#include "../../Cheats/Officer/OfficerData.h"
#include "../../Cheats/Officer/OfficerRosterResolve.h"
#include "../../showlog.h"
#include "../../debug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DX11Base {
namespace {
using ChildManagerDetail::NormalizeOfficerPtr;
using ChildManagerDetail::ResolveHeroAndRoster;
using ChildManagerDetail::SafeRead16;
using ChildManagerDetail::SafeReadPtr;

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

struct PregnancyCanonicalTable {
  bool valid = false;
  uintptr_t base = 0;
  std::array<PregnancyDebugRecordDump, 3> slots{};
  std::array<uint16_t, 3> spouseIds{};
};

static PregnancyCanonicalTable g_pregnancyCanonicalTable;
static std::mutex g_pregnancySpouseMutex;

struct PregnancySpouseOption {
  uint16_t id = 0;
  uintptr_t addr = 0;
  uint16_t birthYear = 0;
  std::vector<uint16_t> childIds;
};

static std::vector<PregnancySpouseOption> g_pregnancyCurrentSpouses;
static int g_pregnancySwapSlot = -1;
static uint16_t g_pregnancySwapSpouseId = 0;

// 현재 게임 버전에서 실측/검증된 임신 3슬롯 경로:
// table base = GetGameBase() + 0x5B40
// child offset = 0x5B50, stride = 0x28.
// 검증에 실패하면 gameBase +4MB 경량 검색으로 offset을 자동 복구합니다.
static constexpr uint32_t kVerifiedChildRearingOffset = 0x5B50;
static uint32_t g_childRearingOffset = kVerifiedChildRearingOffset;
static PregnancyCanonicalTable GetCanonicalPregnancySnapshot();

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

static void PopulateSpouseFamilyData(
    uintptr_t rosterBase,
    uintptr_t heroMaster,
    std::vector<PregnancySpouseOption>& options) {
  if (rosterBase <= 0x10000 || heroMaster <= 0x10000 || options.empty())
    return;

  const uintptr_t heroNorm = NormalizeOfficerPtr(heroMaster);
  std::unordered_map<uintptr_t, size_t> spouseByPtr;
  spouseByPtr.reserve(options.size());

  for (size_t i = 0; i < options.size(); ++i) {
    PregnancySpouseOption& option = options[i];
    option.childIds.clear();
    option.birthYear = 0;
    SafeRead16(option.addr + 0x34, &option.birthYear);
    spouseByPtr[NormalizeOfficerPtr(option.addr)] = i;
  }

  // 자녀 관리와 같은 혈연 포인터(+0x48/+0x50)를 사용합니다.
  // 주인공과 해당 배우자를 동시에 부모로 가진 실제 자녀만 표시합니다.
  std::unordered_set<uint16_t> seenChildIds;
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t childAddr = rosterBase + (uintptr_t)i * 0x3D0;

    uint16_t childId = 0;
    if (!SafeRead16(childAddr + 0x08, &childId) ||
        childId < 1 || childId > 5102) {
      continue;
    }

    uint16_t appearance = 0;
    uint16_t birth = 0;
    uint16_t death = 0;
    if (!SafeRead16(childAddr + 0x32, &appearance) ||
        !SafeRead16(childAddr + 0x34, &birth) ||
        !SafeRead16(childAddr + 0x36, &death) ||
        birth == 0 || appearance == 0 || death == 0 ||
        appearance < birth || death < birth) {
      continue;
    }

    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    SafeReadPtr(childAddr + 0x48, &dadPtr);
    SafeReadPtr(childAddr + 0x50, &momPtr);
    const uintptr_t dadNorm = NormalizeOfficerPtr(dadPtr);
    const uintptr_t momNorm = NormalizeOfficerPtr(momPtr);

    uintptr_t otherParent = 0;
    if (dadNorm == heroNorm)
      otherParent = momNorm;
    else if (momNorm == heroNorm)
      otherParent = dadNorm;
    else
      continue;

    const auto spouseIt = spouseByPtr.find(otherParent);
    if (spouseIt == spouseByPtr.end())
      continue;

    if (!seenChildIds.insert(childId).second)
      continue;

    options[spouseIt->second].childIds.push_back(childId);
  }

  for (PregnancySpouseOption& option : options)
    std::sort(option.childIds.begin(), option.childIds.end());
}

static std::string BuildSpouseChildrenText(
    const PregnancySpouseOption& option) {
  if (option.childIds.empty())
    return u8"-";

  std::string text;
  for (size_t i = 0; i < option.childIds.size(); ++i) {
    const uint16_t childId = option.childIds[i];
    if (i != 0)
      text += u8", ";

    const auto nameIt = g_officerNames.find(childId);
    if (nameIt != g_officerNames.end() && !nameIt->second.empty()) {
      text += nameIt->second;
      text += " (";
      text += std::to_string(childId);
      text += ")";
    } else {
      text += u8"ID ";
      text += std::to_string(childId);
    }
  }
  return text;
}

static bool RefreshCurrentPregnancySpousesFast() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ResolveHeroAndRoster(rosterBase, heroMaster, heroId))
    return false;

  OfficerRelationshipInfo relInfo;
  if (!GetOfficerRelationshipInfo(heroMaster, relInfo) ||
      !relInfo.valid) {
    return false;
  }

  std::vector<uint16_t> spouseIds = relInfo.spouses;
  std::sort(spouseIds.begin(), spouseIds.end());
  spouseIds.erase(
      std::unique(spouseIds.begin(), spouseIds.end()),
      spouseIds.end());

  std::vector<PregnancySpouseOption> options;
  options.reserve(spouseIds.size());

  for (uint16_t id : spouseIds) {
    if (id < 1 || id > 5102)
      continue;

    // 일반적으로 ID와 roster 인덱스가 대응하므로 먼저 O(1)로 확인합니다.
    uintptr_t officerAddr =
        rosterBase + (uintptr_t)(id - 1) * 0x3D0;
    uint16_t verifyId = 0;
    if (!SafeRead16(officerAddr + 0x08, &verifyId) ||
        verifyId != id) {
      // 예외적인 배열 배치만 전체 roster에서 한 번 찾아봅니다.
      officerAddr = 0;
      for (int i = 0; i < 5102; ++i) {
        const uintptr_t candidate =
            rosterBase + (uintptr_t)i * 0x3D0;
        if (SafeRead16(candidate + 0x08, &verifyId) &&
            verifyId == id) {
          officerAddr = candidate;
          break;
        }
      }
    }

    if (officerAddr <= 0x10000)
      continue;

    PregnancySpouseOption option;
    option.id = id;
    option.addr = NormalizeOfficerPtr(officerAddr);
    options.push_back(option);
  }

  PopulateSpouseFamilyData(rosterBase, heroMaster, options);

  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancyCurrentSpouses = std::move(options);
  }

  AddLog(
      u8"[임신슬롯] 배우자 목록 즉시 갱신: Hero ID %u / %zu명",
      heroId, spouseIds.size());
  return true;
}

static bool TryReadDirectPregnancyTableAt(
    uintptr_t base,
    PregnancyCanonicalTable* out) {
  if (!out || base <= 0x10000 ||
      !IsValidPtr(base, 0x28 * 3)) {
    return false;
  }

  std::unordered_set<uint16_t> currentSpouseIds;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    for (const PregnancySpouseOption& option :
         g_pregnancyCurrentSpouses) {
      if (option.id != 0)
        currentSpouseIds.insert(option.id);
    }
  }

  PregnancyCanonicalTable table;
  table.base = base;
  std::unordered_set<uint16_t> uniqueSlotSpouses;
  int matchedCurrentSpouses = 0;

  for (int slot = 0; slot < 3; ++slot) {
    PregnancyDebugRecordDump d;
    if (!ReadPregnancyDebugRecord(
            base + (uintptr_t)slot * 0x28, &d)) {
      return false;
    }

    if (d.pregnancyCooldown > 100 ||
        d.pregnancyFlag > 1 ||
        d.remainingMonths > 12) {
      return false;
    }

    const uintptr_t spousePtr = NormalizeOfficerPtr(d.q00);
    const uint16_t spouseId = d.q00OfficerId;

    // 빈 슬롯은 +00이 NULL이어야 하고, 사용 슬롯은 정상 무장 포인터여야 합니다.
    if (spousePtr == 0) {
      if (spouseId != 0)
        return false;
    } else {
      if (spouseId == 0 ||
          !uniqueSlotSpouses.insert(spouseId).second) {
        return false;
      }

      if (currentSpouseIds.count(spouseId) != 0)
        matchedCurrentSpouses++;
    }

    if (d.childPtr != 0 && d.childOfficerId == 0)
      return false;

    table.slots[(size_t)slot] = d;
    table.spouseIds[(size_t)slot] = spouseId;
  }

  // 현재 배우자와 슬롯의 일치 여부는 테이블 자체의 유효성 조건이 아닙니다.
  // 새 게임/커스텀 배우자는 관계가 성립해도 임신 슬롯이 아직 비어 있을 수 있으므로,
  // 슬롯 포인터/flag/month/child 구조가 정상이라면 canonical table로 인정합니다.
  (void)matchedCurrentSpouses;

  table.valid = true;
  *out = table;
  return true;
}

static bool FindPregnancyTableNearGameBase(
    uintptr_t gameBase,
    PregnancyCanonicalTable* out) {
  if (!out || gameBase <= 0x10000)
    return false;

  std::unordered_set<uintptr_t> spousePtrs;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    for (const PregnancySpouseOption& option :
         g_pregnancyCurrentSpouses) {
      if (option.addr > 0x10000)
        spousePtrs.insert(NormalizeOfficerPtr(option.addr));
    }
  }

  if (spousePtrs.empty())
    return false;

  constexpr uintptr_t kScanSize = 0x400000; // gameBase 기준 +4MB만 검색
  constexpr size_t kChunkSize = 0x10000;    // 64KB
  const uintptr_t scanStart = gameBase;
  const uintptr_t scanEnd = gameBase + kScanSize;
  const ULONGLONG started = GetTickCount64();

  size_t pointerHits = 0;
  size_t candidatesTested = 0;
  std::unordered_set<uintptr_t> testedBases;
  std::vector<uint8_t> buffer(kChunkSize);

  uintptr_t cursor = scanStart;
  while (cursor < scanEnd) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) != sizeof(mbi))
      break;

    const uintptr_t regionStart =
        (std::max)(cursor, (uintptr_t)mbi.BaseAddress);
    const uintptr_t regionEnd =
        (std::min)(
            scanEnd,
            (uintptr_t)mbi.BaseAddress + mbi.RegionSize);

    const bool readable =
        mbi.State == MEM_COMMIT &&
        !(mbi.Protect & PAGE_GUARD) &&
        !(mbi.Protect & PAGE_NOACCESS);

    if (readable && regionEnd > regionStart) {
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
            memcpy(
                &rawPtr,
                buffer.data() + i,
                sizeof(rawPtr));

            if (spousePtrs.count(
                    NormalizeOfficerPtr(rawPtr)) == 0) {
              continue;
            }

            ++pointerHits;
            const uintptr_t hitAddr = curr + i;

            // 배우자 포인터는 slot +0x00이므로,
            // 이 hit가 slot0/1/2 중 어디인지 각각 시험합니다.
            for (int slot = 0; slot < 3; ++slot) {
              const uintptr_t back =
                  (uintptr_t)slot * 0x28;
              if (hitAddr < gameBase + back)
                continue;

              const uintptr_t candidateBase =
                  hitAddr - back;
              if (candidateBase < scanStart ||
                  candidateBase + 0x28 * 3 > scanEnd ||
                  !testedBases.insert(candidateBase).second) {
                continue;
              }

              ++candidatesTested;

              PregnancyCanonicalTable table;
              if (!TryReadDirectPregnancyTableAt(
                      candidateBase, &table)) {
                continue;
              }

              const uintptr_t relative =
                  candidateBase - gameBase;
              if (relative + 0x10 <= 0xFFFFFFFFull) {
                g_childRearingOffset =
                    (uint32_t)(relative + 0x10);
              }

              *out = table;

              AddLog(
                  u8"[임신슬롯] gameBase 근처 검색 성공: table=%p / relative=+0x%llX / childOffset=0x%X / ptrHits=%zu / 후보=%zu / %llums",
                  (void*)candidateBase,
                  (unsigned long long)relative,
                  g_childRearingOffset,
                  pointerHits,
                  candidatesTested,
                  (unsigned long long)(
                      GetTickCount64() - started));
              return true;
            }
          }
        }

        curr += toRead;
      }
    }

    if (regionEnd <= cursor)
      break;
    cursor = regionEnd;
  }

  AddLog(
      u8"[임신슬롯] gameBase 근처 +4MB 검색 실패: ptrHits=%zu / 후보=%zu / %llums",
      pointerHits,
      candidatesTested,
      (unsigned long long)(
          GetTickCount64() - started));
  return false;
}

static bool ResolvePregnancyTableDirect(
    PregnancyCanonicalTable* out) {
  if (!out)
    return false;

  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;

  auto tryOffset =
      [&](uint32_t offset) -> bool {
    if (offset < 0x10)
      return false;

    PregnancyCanonicalTable table;
    const uintptr_t base =
        gameBase + (uintptr_t)offset - 0x10;

    if (!TryReadDirectPregnancyTableAt(base, &table))
      return false;

    g_childRearingOffset = offset;
    *out = table;
    return true;
  };

  // 현재 버전에서 검증된 0x5B50을 우선 사용합니다.
  // fallback이 다른 offset을 찾은 세션에서는 그 캐시를 먼저 재검증합니다.
  if (g_childRearingOffset != 0 &&
      tryOffset(g_childRearingOffset)) {
    return true;
  }

  if (g_childRearingOffset != kVerifiedChildRearingOffset &&
      tryOffset(kVerifiedChildRearingOffset)) {
    return true;
  }

  // 빌드/세이브 구조 변화 시에만 gameBase 주변 +4MB를 빠르게 검색합니다.
  return FindPregnancyTableNearGameBase(gameBase, out);
}

static bool RefreshPregnancyTableDirect() {
  PregnancyCanonicalTable table;
  if (!ResolvePregnancyTableDirect(&table)) {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancyCanonicalTable.valid = false;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    g_pregnancyCanonicalTable = table;
  }

  AddLog(
      u8"[임신슬롯] 직접 경로 갱신 성공: base=%p / stride=0x28 / IDs=%u,%u,%u",
      (void*)table.base,
      table.spouseIds[0],
      table.spouseIds[1],
      table.spouseIds[2]);
  return true;
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

  // 슬롯 교체 직후 배우자 나이/자녀 캐시도 다시 읽어 다음 프레임에 함께 반영합니다.
  RefreshCurrentPregnancySpousesFast();

  AddLog(
      u8"[임신슬롯교체] slot%d 배우자 교체 성공: ID %u -> ID %u / cooldown=100(가능성 0%%)",
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

    if (!ReadPregnancyDebugRecord(slotAddr, &d)) {
      g_pregnancyCanonicalTable.valid = false;
      return false;
    }

    const uint16_t expectedId =
        refreshed.spouseIds[(size_t)slot];
    if (expectedId != 0) {
      if (d.q00OfficerId != expectedId) {
        g_pregnancyCanonicalTable.valid = false;
        return false;
      }
    } else if (NormalizeOfficerPtr(d.q00) != 0) {
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


} // namespace

void RunPregnancyManagerUpdate() {
  RefreshCanonicalPregnancyTableCached();
}

void RefreshPregnancyManagerOnWindowOpen() {
  RefreshCurrentPregnancySpousesFast();
  if (!RefreshPregnancyTableDirect()) {
    AddLog(
        u8"[임신슬롯] 직접 경로 갱신 실패 - 전체 메모리 검색은 실행하지 않음");
  }
}

void DrawPregnancyManagerSection(float scale) {
  const PregnancyCanonicalTable pregnancy =
      GetCanonicalPregnancySnapshot();

  // 현재 배우자 전체 목록은 임신 3슬롯과 별도로 항상 표시합니다.
  // 배우자별 나이와 주인공 사이의 자녀도 함께 표시합니다.
  std::vector<PregnancySpouseOption> currentSpouses;
  {
    std::lock_guard<std::mutex> lock(g_pregnancySpouseMutex);
    currentSpouses = g_pregnancyCurrentSpouses;
  }

  unsigned short currentYear = 0;
  const bool hasCurrentYear =
      ReadScenarioYear(&currentYear) && currentYear >= 171 && currentYear < 270;

  ImGui::Text(
      u8"현재 배우자 목록 (%zu명)",
      currentSpouses.size());

  if (currentSpouses.empty()) {
    ImGui::TextDisabled(u8"현재 확인된 배우자가 없습니다.");
  } else {
    const float rowHeight =
        ImGui::GetTextLineHeightWithSpacing();
    const float tableHeight =
        (std::min)(200.0f * scale,
                   (rowHeight * (float)(currentSpouses.size() + 1)) +
                       8.0f * scale);

    if (ImGui::BeginTable(
            "CurrentPregnancySpouseList", 6,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoSavedSettings,
            ImVec2(0, tableHeight))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn(
          u8"번호", ImGuiTableColumnFlags_WidthFixed,
          42.0f * scale);
      ImGui::TableSetupColumn(
          u8"배우자", ImGuiTableColumnFlags_WidthFixed,
          120.0f * scale);
      ImGui::TableSetupColumn(
          u8"나이", ImGuiTableColumnFlags_WidthFixed,
          50.0f * scale);
      ImGui::TableSetupColumn(
          u8"ID", ImGuiTableColumnFlags_WidthFixed,
          50.0f * scale);
      ImGui::TableSetupColumn(
          u8"자녀", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn(
          u8"임신 슬롯", ImGuiTableColumnFlags_WidthFixed,
          75.0f * scale);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < currentSpouses.size(); ++i) {
        const PregnancySpouseOption& option =
            currentSpouses[i];

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::Text("%zu", i + 1);

        ImGui::TableNextColumn();
        auto nameIt = g_officerNames.find(option.id);
        if (nameIt != g_officerNames.end() &&
            !nameIt->second.empty()) {
          ImGui::TextUnformatted(nameIt->second.c_str());
        } else {
          ImGui::Text(u8"ID %u", option.id);
        }

        ImGui::TableNextColumn();
        if (hasCurrentYear && option.birthYear != 0 &&
            currentYear >= option.birthYear) {
          ImGui::Text(u8"%u세",
                      (unsigned)(currentYear - option.birthYear));
        } else {
          ImGui::TextUnformatted(u8"-");
        }

        ImGui::TableNextColumn();
        ImGui::Text("%u", option.id);

        ImGui::TableNextColumn();
        const std::string childrenText =
            BuildSpouseChildrenText(option);
        ImGui::TextWrapped("%s", childrenText.c_str());

        ImGui::TableNextColumn();
        if (pregnancy.valid) {
          int pregnancySlot = -1;
          for (int slot = 0; slot < 3; ++slot) {
            if (pregnancy.spouseIds[(size_t)slot] ==
                option.id) {
              pregnancySlot = slot;
              break;
            }
          }

          if (pregnancySlot >= 0) {
            ImGui::TextColored(
                ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                u8"슬롯 %d",
                pregnancySlot + 1);
          } else {
            ImGui::TextDisabled(u8"슬롯 밖");
          }
        } else {
          ImGui::TextDisabled(u8"확인 불가");
        }
      }

      ImGui::EndTable();
    }
  }

  ImGui::Spacing();
  ImGui::TextUnformatted(u8"임신 상태 (게임 기본 3슬롯)");

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
          u8"임신 가능성", ImGuiTableColumnFlags_WidthFixed,
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
          u8"3슬롯 밖 배우자 교체 : 임신을 하기 위한 배우자는 1~3슬롯에 배치해야 합니다.");

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
            u8"새 슬롯은 cooldown=100(임신 가능성 0%%)으로 초기화합니다.");
        ImGui::EndTooltip();
      }
    }

    if (bShowDebug) {
      ImGui::TextDisabled(
          u8"canonical base=%p / stride=0x28",
          (void*)pregnancy.base);
    }
  } else {
    ImGui::TextDisabled(
        u8"임신 3슬롯 직접 경로를 확인하지 못했습니다. 창을 닫았다가 다시 열면 다시 확인합니다.");
  }
}

} // namespace DX11Base