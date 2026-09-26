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

static uintptr_t FindOfficerById(
    uintptr_t rosterBase,
    uint16_t targetId) {
  if (rosterBase <= 0x10000 || targetId < 1 || targetId > 5102)
    return 0;

  uintptr_t direct =
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

static int FindSlotForSpouse(
    const std::array<SlotSnapshot, 3>& slots,
    uint16_t spouseId) {
  for (int i = 0; i < 3; ++i) {
    if (slots[(size_t)i].valid &&
        slots[(size_t)i].spouseId == spouseId) {
      return i;
    }
  }
  return -1;
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
      GetOfficerRelationshipInfo(heroMaster, relInfo) && relInfo.valid;

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
    if (!ChildManagerDetail::SafeReadPtr(officer + 0x48, &dadPtr) ||
        !ChildManagerDetail::SafeReadPtr(officer + 0x50, &momPtr)) {
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

    heroChildren.push_back(id);
    const uint16_t otherParentId =
        ReadOfficerIdFromPtr(heroIsDad ? momPtr : dadPtr);
    if (otherParentId != 0)
      childCountByOtherParent[otherParentId]++;

    uint16_t birth = 0;
    uint16_t appear = 0;
    uint16_t death = 0;
    ChildManagerDetail::SafeRead16(officer + 0x34, &birth);
    ChildManagerDetail::SafeRead16(officer + 0x32, &appear);
    ChildManagerDetail::SafeRead16(officer + 0x36, &death);

    AddLog(
        u8"[자녀제한DBG] child id=%u otherParent=%u birth=%u appear=%u death=%u",
        id, otherParentId, birth, appear, death);
  }

  std::sort(heroChildren.begin(), heroChildren.end());
  AddLog(
      u8"[자녀제한DBG] 혈연 포인터 기준 주인공 자녀 총 %zu명",
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
        ChildManagerDetail::SafeRead16(spouse + 0x34, &birth);

      int age = -1;
      if (hasYear && birth != 0 && currentYear >= birth)
        age = (int)currentYear - (int)birth;

      const int slotIndex =
          slotsValid ? FindSlotForSpouse(slots, spouseId) : -1;
      const int childCount =
          childCountByOtherParent.count(spouseId)
              ? childCountByOtherParent[spouseId]
              : 0;

      if (slotIndex >= 0) {
        const SlotSnapshot& s = slots[(size_t)slotIndex];
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

  // 구형 CT의 "1101~1120 / [4001~4020]" 표기가 실제로 무엇을 뜻하는지
  // 추측하지 않고 두 후보를 모두 읽어 비교합니다.
  AddLog(u8"[자녀제한DBG] -- CT 후보 A: roster index 1101~1120 --");
  for (int ordinal = 1101; ordinal <= 1120; ++ordinal) {
    const int index = ordinal - 1;
    const uintptr_t officer =
        rosterBase + (uintptr_t)index * 0x3D0;
    uint16_t id = 0;
    uint16_t birth = 0;
    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    ChildManagerDetail::SafeRead16(officer + 0x08, &id);
    ChildManagerDetail::SafeRead16(officer + 0x34, &birth);
    ChildManagerDetail::SafeReadPtr(officer + 0x48, &dadPtr);
    ChildManagerDetail::SafeReadPtr(officer + 0x50, &momPtr);
    AddLog(
        u8"[자녀제한DBG] CTidx=%d actualId=%u birth=%u father=%u mother=%u heroLink=%u",
        ordinal, id, birth,
        ReadOfficerIdFromPtr(dadPtr),
        ReadOfficerIdFromPtr(momPtr),
        (unsigned)((ChildManagerDetail::NormalizeOfficerPtr(dadPtr) == heroNorm ||
                    ChildManagerDetail::NormalizeOfficerPtr(momPtr) == heroNorm) ? 1 : 0));
  }

  AddLog(u8"[자녀제한DBG] -- CT 후보 B: officer ID 4001~4020 --");
  for (uint16_t candidateId = 4001; candidateId <= 4020; ++candidateId) {
    auto it = officersById.find(candidateId);
    if (it == officersById.end()) {
      AddLog(
          u8"[자녀제한DBG] candidateId=%u record=NOT_FOUND",
          candidateId);
      continue;
    }

    const uintptr_t officer = it->second;
    uint16_t birth = 0;
    uint16_t appear = 0;
    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    ChildManagerDetail::SafeRead16(officer + 0x34, &birth);
    ChildManagerDetail::SafeRead16(officer + 0x32, &appear);
    ChildManagerDetail::SafeReadPtr(officer + 0x48, &dadPtr);
    ChildManagerDetail::SafeReadPtr(officer + 0x50, &momPtr);

    AddLog(
        u8"[자녀제한DBG] candidateId=%u birth=%u appear=%u father=%u mother=%u heroLink=%u",
        candidateId, birth, appear,
        ReadOfficerIdFromPtr(dadPtr),
        ReadOfficerIdFromPtr(momPtr),
        (unsigned)((ChildManagerDetail::NormalizeOfficerPtr(dadPtr) == heroNorm ||
                    ChildManagerDetail::NormalizeOfficerPtr(momPtr) == heroNorm) ? 1 : 0));
  }

  AddLog(u8"[자녀제한DBG] ===== 진단 종료 =====");
}

static bool SameSlot(
    const SlotSnapshot& a,
    const SlotSnapshot& b) {
  return a.valid == b.valid &&
         a.spouseId == b.spouseId &&
         a.childId == b.childId &&
         a.cooldown == b.cooldown &&
         a.pregnancyFlag == b.pregnancyFlag &&
         a.remainingMonths == b.remainingMonths;
}

static void Tick() {
  static bool wasOpen = false;
  static ULONGLONG lastPollMs = 0;
  static bool havePrevious = false;
  static std::array<SlotSnapshot, 3> previous{};

  if (!bShowChildManagerWin) {
    wasOpen = false;
    havePrevious = false;
    return;
  }

  if (!wasOpen) {
    wasOpen = true;
    LogFullSnapshot();
    lastPollMs = 0;
  }

  const ULONGLONG now = GetTickCount64();
  if (lastPollMs != 0 && now - lastPollMs < 1000)
    return;
  lastPollMs = now;

  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return;

  const uintptr_t tableBase = gameBase + 0x5B40;
  std::array<SlotSnapshot, 3> current{};
  for (int slot = 0; slot < 3; ++slot) {
    if (!ReadSlotSnapshot(
            tableBase + (uintptr_t)slot * 0x28,
            current[(size_t)slot])) {
      return;
    }
  }

  if (!havePrevious) {
    previous = current;
    havePrevious = true;
    return;
  }

  for (int slot = 0; slot < 3; ++slot) {
    const SlotSnapshot& oldS = previous[(size_t)slot];
    const SlotSnapshot& newS = current[(size_t)slot];
    if (SameSlot(oldS, newS))
      continue;

    AddLog(
        u8"[자녀제한DBG] slot%d 변화: spouse %u->%u / cooldown %u->%u / flag %u->%u / months %u->%u / child %u->%u",
        slot + 1,
        oldS.spouseId, newS.spouseId,
        (unsigned)oldS.cooldown, (unsigned)newS.cooldown,
        (unsigned)oldS.pregnancyFlag, (unsigned)newS.pregnancyFlag,
        (unsigned)oldS.remainingMonths, (unsigned)newS.remainingMonths,
        oldS.childId, newS.childId);
  }

  previous = current;
}

} // namespace ChildLimitDiagnostics
} // namespace DX11Base
