#pragma once

#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"
#include "../System/MonthCapture.h"
#include "ChildEarlyAppearance.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace DX11Base {
namespace ChildLimitDiagnostics {

struct SlotSnapshot {
  bool valid = false;
  uint16_t spouseId = 0;
  uint16_t childId = 0;
  uint8_t cooldown = 0;
  uint8_t pregnancyFlag = 0;
  uint8_t remainingMonths = 0;
};

struct GeneratedChildSnapshot {
  bool valid = false;
  uint16_t appearance = 0;
  uint16_t birth = 0;
  uint16_t death = 0;
  uint16_t fatherId = 0;
  uint16_t motherId = 0;
  bool heroLink = false;
};

static bool SafeRead8Local(uintptr_t addr, uint8_t* out) {
  if (!out)
    return false;
  __try {
    *out = *(uint8_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static uint16_t ReadOfficerIdFromPtr(uintptr_t rawPtr) {
  const uintptr_t p = ChildManagerDetail::NormalizeOfficerPtr(rawPtr);
  if (p <= 0x10000)
    return 0;

  uint16_t id = 0;
  if (!ChildManagerDetail::SafeRead16(p + 0x08, &id) ||
      id < 1 || id > 5102) {
    return 0;
  }
  return id;
}

static bool ReadSlotSnapshot(uintptr_t slotAddr, SlotSnapshot& out) {
  SlotSnapshot s;
  uintptr_t spousePtr = 0;
  uintptr_t childPtr = 0;

  if (!ChildManagerDetail::SafeReadPtr(slotAddr + 0x00, &spousePtr) ||
      !SafeRead8Local(slotAddr + 0x08, &s.cooldown) ||
      !SafeRead8Local(slotAddr + 0x09, &s.pregnancyFlag) ||
      !SafeRead8Local(slotAddr + 0x0A, &s.remainingMonths) ||
      !ChildManagerDetail::SafeReadPtr(slotAddr + 0x10, &childPtr)) {
    return false;
  }

  s.spouseId = ReadOfficerIdFromPtr(spousePtr);
  s.childId = ReadOfficerIdFromPtr(childPtr);
  s.valid = true;
  out = s;
  return true;
}

static bool IsValidLifeYears(uint16_t appearance,
                             uint16_t birth,
                             uint16_t death) {
  return birth != 0 &&
         appearance != 0 &&
         death != 0 &&
         appearance >= birth &&
         death >= birth;
}

static uintptr_t FindOfficerById(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000 || targetId < 1 || targetId > 5102)
    return 0;

  const uintptr_t direct =
      rosterBase + (uintptr_t)(targetId - 1) * 0x3D0;
  uint16_t verifyId = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verifyId) &&
      verifyId == targetId) {
    return direct;
  }

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t addr = rosterBase + (uintptr_t)i * 0x3D0;
    if (ChildManagerDetail::SafeRead16(addr + 0x08, &verifyId) &&
        verifyId == targetId) {
      return addr;
    }
  }
  return 0;
}

static int FindSlotForSpouse(const std::array<SlotSnapshot, 3>& slots,
                             uint16_t spouseId) {
  for (int i = 0; i < 3; ++i) {
    if (slots[(size_t)i].valid &&
        slots[(size_t)i].spouseId == spouseId) {
      return i;
    }
  }
  return -1;
}

static bool ResolveGeneratedPool(
    uintptr_t rosterBase,
    std::array<uintptr_t, 20>& outAddrs) {
  outAddrs.fill(0);
  if (rosterBase <= 0x10000)
    return false;

  int found = 0;
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officer =
        rosterBase + (uintptr_t)i * 0x3D0;
    uint16_t id = 0;
    if (!ChildManagerDetail::SafeRead16(officer + 0x08, &id))
      continue;

    if (id < 4001 || id > 4020)
      continue;

    const size_t index = (size_t)(id - 4001);
    if (outAddrs[index] == 0) {
      outAddrs[index] = officer;
      ++found;
    }
  }

  return found == 20;
}

static bool ReadGeneratedChildSnapshot(
    uintptr_t officer,
    uintptr_t heroNorm,
    GeneratedChildSnapshot& out) {
  GeneratedChildSnapshot s;
  if (officer <= 0x10000)
    return false;

  uintptr_t dadPtr = 0;
  uintptr_t momPtr = 0;
  if (!ChildManagerDetail::SafeRead16(officer + 0x32, &s.appearance) ||
      !ChildManagerDetail::SafeRead16(officer + 0x34, &s.birth) ||
      !ChildManagerDetail::SafeRead16(officer + 0x36, &s.death) ||
      !ChildManagerDetail::SafeReadPtr(officer + 0x48, &dadPtr) ||
      !ChildManagerDetail::SafeReadPtr(officer + 0x50, &momPtr)) {
    return false;
  }

  s.fatherId = ReadOfficerIdFromPtr(dadPtr);
  s.motherId = ReadOfficerIdFromPtr(momPtr);
  s.heroLink =
      ChildManagerDetail::NormalizeOfficerPtr(dadPtr) == heroNorm ||
      ChildManagerDetail::NormalizeOfficerPtr(momPtr) == heroNorm;
  s.valid = true;
  out = s;
  return true;
}

static bool SameGeneratedChild(const GeneratedChildSnapshot& a,
                               const GeneratedChildSnapshot& b) {
  return a.valid == b.valid &&
         a.appearance == b.appearance &&
         a.birth == b.birth &&
         a.death == b.death &&
         a.fatherId == b.fatherId &&
         a.motherId == b.motherId &&
         a.heroLink == b.heroLink;
}

static bool SameSlot(const SlotSnapshot& a,
                     const SlotSnapshot& b) {
  return a.valid == b.valid &&
         a.spouseId == b.spouseId &&
         a.childId == b.childId &&
         a.cooldown == b.cooldown &&
         a.pregnancyFlag == b.pregnancyFlag &&
         a.remainingMonths == b.remainingMonths;
}

static bool HasHitNear(const std::vector<uintptr_t>& hits,
                       uintptr_t center,
                       uintptr_t radius) {
  if (hits.empty())
    return false;

  auto it = std::lower_bound(
      hits.begin(), hits.end(),
      center > radius ? center - radius : 0);
  return it != hits.end() && *it <= center + radius;
}

static void LogNearbyPairs(
    const char* label,
    const std::vector<uintptr_t>& starts,
    const std::vector<uintptr_t>& ends,
    const std::vector<uintptr_t>& count20,
    uintptr_t imageBase) {
  const uintptr_t kMaxDistance = 0x1000;
  int logged = 0;

  for (uintptr_t start : starts) {
    auto it = std::lower_bound(ends.begin(), ends.end(), start);
    uintptr_t best = 0;
    uintptr_t bestDistance = (uintptr_t)-1;

    if (it != ends.end()) {
      const uintptr_t d = *it >= start ? *it - start : start - *it;
      if (d < bestDistance) {
        bestDistance = d;
        best = *it;
      }
    }
    if (it != ends.begin()) {
      --it;
      const uintptr_t d = *it >= start ? *it - start : start - *it;
      if (d < bestDistance) {
        bestDistance = d;
        best = *it;
      }
    }

    if (best == 0 || bestDistance > kMaxDistance)
      continue;

    const uintptr_t center = (start + best) / 2;
    AddLog(
        u8"[자녀할당코드DBG] %s 후보: startRVA=+0x%llX endRVA=+0x%llX delta=0x%llX nearCount20=%u",
        label,
        (unsigned long long)(start - imageBase),
        (unsigned long long)(best - imageBase),
        (unsigned long long)bestDistance,
        (unsigned)(HasHitNear(count20, center, 0x200) ? 1 : 0));

    if (++logged >= 16) {
      AddLog(
          u8"[자녀할당코드DBG] %s 후보가 많아 16개까지만 표시",
          label);
      break;
    }
  }

  if (logged == 0) {
    AddLog(
        u8"[자녀할당코드DBG] %s 근접쌍 없음 (start=%zu / end=%zu)",
        label, starts.size(), ends.size());
  }
}

static void LogAllocatorConstantScan() {
  const uintptr_t imageBase =
      (uintptr_t)GetModuleHandle(nullptr);
  if (imageBase <= 0x10000) {
    AddLog(u8"[자녀할당코드DBG] 실행파일 베이스 확인 실패");
    return;
  }

  const IMAGE_DOS_HEADER* dos =
      (const IMAGE_DOS_HEADER*)imageBase;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    AddLog(u8"[자녀할당코드DBG] DOS 헤더 확인 실패");
    return;
  }

  const IMAGE_NT_HEADERS* nt =
      (const IMAGE_NT_HEADERS*)(imageBase + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    AddLog(u8"[자녀할당코드DBG] NT 헤더 확인 실패");
    return;
  }

  std::vector<uintptr_t> id4001_16;
  std::vector<uintptr_t> id4020_16;
  std::vector<uintptr_t> id4001_32;
  std::vector<uintptr_t> id4020_32;
  std::vector<uintptr_t> idx1100_32;
  std::vector<uintptr_t> idx1120_32;
  std::vector<uintptr_t> count20_32;

  const IMAGE_SECTION_HEADER* section =
      IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections;
       ++i, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
      continue;

    const uintptr_t sectionBase =
        imageBase + section->VirtualAddress;
    const size_t sectionSize =
        (size_t)section->Misc.VirtualSize;
    if (sectionSize < 4)
      continue;

    const uint8_t* p = (const uint8_t*)sectionBase;
    for (size_t j = 0; j + 4 <= sectionSize; ++j) {
      const uintptr_t addr = sectionBase + j;

      if (p[j] == 0xA1 && p[j + 1] == 0x0F)
        id4001_16.push_back(addr);
      if (p[j] == 0xB4 && p[j + 1] == 0x0F)
        id4020_16.push_back(addr);

      if (p[j] == 0xA1 && p[j + 1] == 0x0F &&
          p[j + 2] == 0x00 && p[j + 3] == 0x00)
        id4001_32.push_back(addr);
      if (p[j] == 0xB4 && p[j + 1] == 0x0F &&
          p[j + 2] == 0x00 && p[j + 3] == 0x00)
        id4020_32.push_back(addr);

      // CT의 roster index 1101은 zero-based 1100에서 시작할 가능성도 함께 봅니다.
      if (p[j] == 0x4C && p[j + 1] == 0x04 &&
          p[j + 2] == 0x00 && p[j + 3] == 0x00)
        idx1100_32.push_back(addr);
      if (p[j] == 0x60 && p[j + 1] == 0x04 &&
          p[j + 2] == 0x00 && p[j + 3] == 0x00)
        idx1120_32.push_back(addr);
      if (p[j] == 0x14 && p[j + 1] == 0x00 &&
          p[j + 2] == 0x00 && p[j + 3] == 0x00)
        count20_32.push_back(addr);
    }
  }

  AddLog(
      u8"[자녀할당코드DBG] 상수 스캔: ID4001 16b=%zu 32b=%zu / ID4020 16b=%zu 32b=%zu / idx1100=%zu idx1120=%zu / count20=%zu",
      id4001_16.size(), id4001_32.size(),
      id4020_16.size(), id4020_32.size(),
      idx1100_32.size(), idx1120_32.size(),
      count20_32.size());

  // 32비트 즉시값은 오탐이 적으므로 우선순위가 높습니다.
  LogNearbyPairs(
      "ID4001~4020(32bit)",
      id4001_32, id4020_32, count20_32, imageBase);
  LogNearbyPairs(
      "roster1100~1120(32bit)",
      idx1100_32, idx1120_32, count20_32, imageBase);

  // 16비트 ID 비교도 흔하므로 보조 후보로 남깁니다.
  LogNearbyPairs(
      "ID4001~4020(16bit)",
      id4001_16, id4020_16, count20_32, imageBase);
}

static void LogFullSnapshot() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    AddLog(u8"[자녀제한DBG] 주인공/무장 배열 확인 실패");
    return;
  }

  unsigned short currentYear = 0;
  const bool hasYear = ReadScenarioYear(&currentYear);
  const uintptr_t heroNorm =
      ChildManagerDetail::NormalizeOfficerPtr(heroMaster);

  OfficerRelationshipInfo relInfo;
  const bool hasRelations =
      GetOfficerRelationshipInfo(heroMaster, relInfo) &&
      relInfo.valid;

  const uintptr_t gameBase = GetGameBase();
  const uintptr_t tableBase =
      gameBase > 0x10000 ? gameBase + 0x5B40 : 0;

  std::array<SlotSnapshot, 3> slots{};
  bool slotsValid = tableBase > 0x10000;
  if (slotsValid) {
    for (int slot = 0; slot < 3; ++slot) {
      if (!ReadSlotSnapshot(
              tableBase + (uintptr_t)slot * 0x28,
              slots[(size_t)slot])) {
        slotsValid = false;
        break;
      }
    }
  }

  AddLog(
      u8"[자녀제한DBG] ===== 진단 시작 Hero=%u / year=%u / table=%p =====",
      heroId,
      hasYear ? (unsigned)currentYear : 0u,
      (void*)tableBase);

  if (slotsValid) {
    for (int slot = 0; slot < 3; ++slot) {
      const SlotSnapshot& s = slots[(size_t)slot];
      AddLog(
          u8"[자녀제한DBG] slot%d spouse=%u cooldown=%u flag=%u months=%u child=%u",
          slot + 1,
          s.spouseId,
          (unsigned)s.cooldown,
          (unsigned)s.pregnancyFlag,
          (unsigned)s.remainingMonths,
          s.childId);
    }
  } else {
    AddLog(u8"[자녀제한DBG] 임신 3슬롯 직접 읽기 실패");
  }

  std::unordered_map<uint16_t, uintptr_t> officersById;
  officersById.reserve(5102);
  std::unordered_map<uint16_t, int> childCountByOtherParent;
  std::vector<uint16_t> heroChildren;

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officer =
        rosterBase + (uintptr_t)i * 0x3D0;

    uint16_t id = 0;
    if (!ChildManagerDetail::SafeRead16(officer + 0x08, &id) ||
        id < 1 || id > 5102) {
      continue;
    }
    officersById[id] = officer;

    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    if (!ChildManagerDetail::SafeReadPtr(
            officer + 0x48, &dadPtr) ||
        !ChildManagerDetail::SafeReadPtr(
            officer + 0x50, &momPtr)) {
      continue;
    }

    const uintptr_t dadNorm =
        ChildManagerDetail::NormalizeOfficerPtr(dadPtr);
    const uintptr_t momNorm =
        ChildManagerDetail::NormalizeOfficerPtr(momPtr);
    const bool heroIsDad = dadNorm == heroNorm;
    const bool heroIsMom = momNorm == heroNorm;
    if (!heroIsDad && !heroIsMom)
      continue;

    uint16_t appearance = 0;
    uint16_t birth = 0;
    uint16_t death = 0;
    if (!ChildManagerDetail::SafeRead16(
            officer + 0x32, &appearance) ||
        !ChildManagerDetail::SafeRead16(
            officer + 0x34, &birth) ||
        !ChildManagerDetail::SafeRead16(
            officer + 0x36, &death)) {
      continue;
    }

    // ChildManager 본체와 같은 검증을 써서 더미/미사용 레코드(ID 2 등)를 제외합니다.
    if (!IsValidLifeYears(appearance, birth, death))
      continue;

    heroChildren.push_back(id);
    const uint16_t otherParentId =
        ReadOfficerIdFromPtr(heroIsDad ? momPtr : dadPtr);
    if (otherParentId != 0)
      childCountByOtherParent[otherParentId]++;

    AddLog(
        u8"[자녀제한DBG] child id=%u otherParent=%u birth=%u appear=%u death=%u",
        id, otherParentId, birth, appearance, death);
  }

  std::sort(heroChildren.begin(), heroChildren.end());
  AddLog(
      u8"[자녀제한DBG] 유효 혈연 자녀 총 %zu명",
      heroChildren.size());

  if (hasRelations) {
    std::vector<uint16_t> spouses = relInfo.spouses;
    std::sort(spouses.begin(), spouses.end());
    spouses.erase(
        std::unique(spouses.begin(), spouses.end()),
        spouses.end());

    AddLog(
        u8"[자녀제한DBG] 현재 배우자 총 %zu명",
        spouses.size());

    for (uint16_t spouseId : spouses) {
      uintptr_t spouse = 0;
      auto it = officersById.find(spouseId);
      if (it != officersById.end())
        spouse = it->second;
      if (spouse == 0)
        spouse = FindOfficerById(rosterBase, spouseId);

      uint16_t birth = 0;
      if (spouse > 0x10000)
        ChildManagerDetail::SafeRead16(
            spouse + 0x34, &birth);

      int age = -1;
      if (hasYear && birth != 0 && currentYear >= birth)
        age = (int)currentYear - (int)birth;

      const int slotIndex =
          slotsValid ? FindSlotForSpouse(
                           slots, spouseId)
                     : -1;
      const int childCount =
          childCountByOtherParent.count(spouseId)
              ? childCountByOtherParent[spouseId]
              : 0;

      if (slotIndex >= 0) {
        const SlotSnapshot& s =
            slots[(size_t)slotIndex];
        AddLog(
            u8"[자녀제한DBG] spouse id=%u birth=%u age=%d children=%d slot=%d cooldown=%u flag=%u months=%u slotChild=%u",
            spouseId, birth, age, childCount,
            slotIndex + 1,
            (unsigned)s.cooldown,
            (unsigned)s.pregnancyFlag,
            (unsigned)s.remainingMonths,
            s.childId);
      } else {
        AddLog(
            u8"[자녀제한DBG] spouse id=%u birth=%u age=%d children=%d slot=OUT",
            spouseId, birth, age, childCount);
      }
    }
  } else {
    AddLog(u8"[자녀제한DBG] 배우자 관계 테이블 읽기 실패");
  }

  std::array<uintptr_t, 20> generatedAddrs{};
  const bool allPoolRecords =
      ResolveGeneratedPool(rosterBase, generatedAddrs);
  int used = 0;
  uint16_t firstFree = 0;

  AddLog(
      u8"[자녀제한DBG] -- 생성자녀 풀 ID 4001~4020 (records=%s) --",
      allPoolRecords ? "20/20" : "partial");

  for (int i = 0; i < 20; ++i) {
    if (generatedAddrs[(size_t)i] == 0)
      continue;

    GeneratedChildSnapshot s;
    if (!ReadGeneratedChildSnapshot(
            generatedAddrs[(size_t)i],
            heroNorm, s)) {
      continue;
    }

    const uint16_t id = (uint16_t)(4001 + i);
    const bool occupied =
        s.birth != 0 ||
        s.appearance != 0 ||
        s.death != 0 ||
        s.fatherId != 0 ||
        s.motherId != 0;

    if (occupied) {
      ++used;
      AddLog(
          u8"[자녀제한DBG] generated id=%u birth=%u appear=%u death=%u father=%u mother=%u heroLink=%u",
          id, s.birth, s.appearance, s.death,
          s.fatherId, s.motherId,
          (unsigned)(s.heroLink ? 1 : 0));
    } else if (firstFree == 0) {
      firstFree = id;
    }
  }

  AddLog(
      u8"[자녀제한DBG] 생성자녀 풀 사용=%d/20 / 다음 빈 후보=%u",
      used, firstFree);
  AddLog(u8"[자녀제한DBG] ===== 진단 종료 =====");
}

static void Tick() {
  static bool wasOpen = false;
  static ULONGLONG lastPollMs = 0;

  static bool haveSlotPrevious = false;
  static std::array<SlotSnapshot, 3> slotPrevious{};

  static uintptr_t previousRosterBase = 0;
  static uintptr_t previousHeroNorm = 0;
  static bool haveGeneratedPrevious = false;
  static std::array<uintptr_t, 20> generatedAddrs{};
  static std::array<GeneratedChildSnapshot, 20>
      generatedPrevious{};

  if (!bShowChildManagerWin) {
    wasOpen = false;
    haveSlotPrevious = false;
    haveGeneratedPrevious = false;
    previousRosterBase = 0;
    previousHeroNorm = 0;
    generatedAddrs.fill(0);
    return;
  }

  if (!wasOpen) {
    wasOpen = true;
    LogFullSnapshot();
    LogAllocatorConstantScan();
    lastPollMs = 0;
  }

  const ULONGLONG now = GetTickCount64();
  if (lastPollMs != 0 && now - lastPollMs < 1000)
    return;
  lastPollMs = now;

  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return;

  // 기존 3개 임신/양육 슬롯 변화 감시.
  const uintptr_t tableBase = gameBase + 0x5B40;
  std::array<SlotSnapshot, 3> slotCurrent{};
  for (int slot = 0; slot < 3; ++slot) {
    if (!ReadSlotSnapshot(
            tableBase + (uintptr_t)slot * 0x28,
            slotCurrent[(size_t)slot])) {
      return;
    }
  }

  if (!haveSlotPrevious) {
    slotPrevious = slotCurrent;
    haveSlotPrevious = true;
  } else {
    for (int slot = 0; slot < 3; ++slot) {
      const SlotSnapshot& oldS =
          slotPrevious[(size_t)slot];
      const SlotSnapshot& newS =
          slotCurrent[(size_t)slot];
      if (SameSlot(oldS, newS))
        continue;

      AddLog(
          u8"[자녀제한DBG] slot%d 변화: spouse %u->%u / cooldown %u->%u / flag %u->%u / months %u->%u / child %u->%u",
          slot + 1,
          oldS.spouseId, newS.spouseId,
          (unsigned)oldS.cooldown,
          (unsigned)newS.cooldown,
          (unsigned)oldS.pregnancyFlag,
          (unsigned)newS.pregnancyFlag,
          (unsigned)oldS.remainingMonths,
          (unsigned)newS.remainingMonths,
          oldS.childId, newS.childId);
    }
    slotPrevious = slotCurrent;
  }

  // 생성자녀 4001~4020 레코드를 직접 감시합니다.
  // 4003 등 아직 비어 있는 레코드에 생년/부모 포인터가 처음 쓰이는 순간을 로그로 남깁니다.
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    return;
  }

  const uintptr_t heroNorm =
      ChildManagerDetail::NormalizeOfficerPtr(heroMaster);
  if (rosterBase != previousRosterBase ||
      heroNorm != previousHeroNorm) {
    generatedAddrs.fill(0);
    ResolveGeneratedPool(rosterBase, generatedAddrs);
    haveGeneratedPrevious = false;
    previousRosterBase = rosterBase;
    previousHeroNorm = heroNorm;
  }

  std::array<GeneratedChildSnapshot, 20>
      generatedCurrent{};
  for (int i = 0; i < 20; ++i) {
    if (generatedAddrs[(size_t)i] == 0)
      continue;

    ReadGeneratedChildSnapshot(
        generatedAddrs[(size_t)i],
        heroNorm,
        generatedCurrent[(size_t)i]);
  }

  if (!haveGeneratedPrevious) {
    generatedPrevious = generatedCurrent;
    haveGeneratedPrevious = true;

    for (int i = 0; i < 20; ++i) {
      const GeneratedChildSnapshot& s =
          generatedCurrent[(size_t)i];
      if (!s.valid)
        continue;

      const bool freeRecord =
          s.birth == 0 &&
          s.appearance == 0 &&
          s.death == 0 &&
          s.fatherId == 0 &&
          s.motherId == 0;
      if (freeRecord) {
        AddLog(
            u8"[자녀풀DBG] 다음 빈 레코드 감시 시작: ID %u / addr=%p",
            (unsigned)(4001 + i),
            (void*)generatedAddrs[(size_t)i]);
        break;
      }
    }
    return;
  }

  for (int i = 0; i < 20; ++i) {
    const GeneratedChildSnapshot& oldS =
        generatedPrevious[(size_t)i];
    const GeneratedChildSnapshot& newS =
        generatedCurrent[(size_t)i];
    if (SameGeneratedChild(oldS, newS))
      continue;

    const uint16_t id = (uint16_t)(4001 + i);
    AddLog(
        u8"[자녀풀DBG] ID %u 변화: birth %u->%u / appear %u->%u / death %u->%u / father %u->%u / mother %u->%u / heroLink %u->%u",
        id,
        oldS.birth, newS.birth,
        oldS.appearance, newS.appearance,
        oldS.death, newS.death,
        oldS.fatherId, newS.fatherId,
        oldS.motherId, newS.motherId,
        (unsigned)(oldS.heroLink ? 1 : 0),
        (unsigned)(newS.heroLink ? 1 : 0));

    const bool becameAllocated =
        (oldS.birth == 0 && newS.birth != 0) ||
        (!oldS.heroLink && newS.heroLink);
    if (becameAllocated) {
      AddLog(
          u8"[자녀풀DBG] >>> 신규 생성자녀 할당 감지: ID %u / record=%p",
          id, (void*)generatedAddrs[(size_t)i]);
    }
  }

  generatedPrevious = generatedCurrent;
}

} // namespace ChildLimitDiagnostics
} // namespace DX11Base
