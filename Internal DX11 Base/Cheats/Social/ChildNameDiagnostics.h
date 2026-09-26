#pragma once

#include "../../pch.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "ChildEarlyAppearance.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uint16_t kNamedChildId = 4004;
static constexpr uint16_t kBlankChildId = 4005;
static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;
static constexpr uintptr_t kPregnancySlotStride = 0x28;
static constexpr uintptr_t kPregnancyChildPtrOffset = 0x10;
static constexpr size_t kOfficerRecordSize = 0x3D0;

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(
             GetCurrentProcess(),
             (LPCVOID)addr,
             out,
             size,
             &read) != FALSE &&
         read == size;
}

static bool IsReadablePointer(uintptr_t addr) {
  if (addr <= 0x10000)
    return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT ||
      (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS)) {
    return false;
  }
  return true;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000)
    return 0;

  const uintptr_t direct =
      rosterBase + (uintptr_t)(targetId - 1) * kOfficerRecordSize;
  uint16_t verify = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verify) &&
      verify == targetId) {
    return direct;
  }

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p =
        rosterBase + (uintptr_t)i * kOfficerRecordSize;
    if (ChildManagerDetail::SafeRead16(p + 0x08, &verify) &&
        verify == targetId) {
      return p;
    }
  }
  return 0;
}

static bool IsChildLinkedInPregnancySlot(uint16_t childId) {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;

  const uintptr_t tableBase = gameBase + kPregnancyTableOffset;
  for (int slot = 0; slot < 3; ++slot) {
    uintptr_t rawChildPtr = 0;
    if (!ChildManagerDetail::SafeReadPtr(
            tableBase + (uintptr_t)slot * kPregnancySlotStride +
                kPregnancyChildPtrOffset,
            &rawChildPtr)) {
      continue;
    }

    const uintptr_t childPtr =
        ChildManagerDetail::NormalizeOfficerPtr(rawChildPtr);
    if (childPtr <= 0x10000)
      continue;

    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childPtr + 0x08, &id) &&
        id == childId) {
      return true;
    }
  }
  return false;
}

static void LogRecord16Snapshot(uintptr_t record) {
  std::array<uint8_t, kOfficerRecordSize> raw{};
  if (!SafeReadMem(record, raw.data(), raw.size())) {
    AddLog(u8"[자녀이름DBG] 4004 0x3D0 레코드 스냅샷 읽기 실패");
    return;
  }

  AddLog(u8"[자녀이름DBG] -- ID4004 16-bit 전체 스냅샷 시작 --");
  for (size_t off = 0; off < raw.size(); off += 0x10) {
    uint16_t words[8]{};
    const size_t remaining = raw.size() - off;
    const size_t bytesThisLine = (remaining < 0x10) ? remaining : 0x10;
    memcpy(words, raw.data() + off, bytesThisLine);

    AddLog(
        u8"[자녀이름DBG] D16 +0x%03zX: %04X %04X %04X %04X %04X %04X %04X %04X",
        off,
        (unsigned)words[0], (unsigned)words[1],
        (unsigned)words[2], (unsigned)words[3],
        (unsigned)words[4], (unsigned)words[5],
        (unsigned)words[6], (unsigned)words[7]);
  }
  AddLog(u8"[자녀이름DBG] -- ID4004 16-bit 전체 스냅샷 종료 --");
}

static void LogPointerCandidates(uintptr_t namedRecord,
                                 uintptr_t blankRecord) {
  int logged = 0;
  for (size_t off = 0;
       off + sizeof(uintptr_t) <= kOfficerRecordSize;
       off += sizeof(uintptr_t)) {
    uintptr_t namedValue = 0;
    uintptr_t blankValue = 0;
    if (!SafeReadMem(namedRecord + off, &namedValue, sizeof(namedValue)) ||
        !SafeReadMem(blankRecord + off, &blankValue, sizeof(blankValue))) {
      continue;
    }

    const uintptr_t namedPtr =
        namedValue & 0x0000FFFFFFFFFFFFULL;
    const uintptr_t blankPtr =
        blankValue & 0x0000FFFFFFFFFFFFULL;

    if (namedPtr == blankPtr || !IsReadablePointer(namedPtr))
      continue;

    AddLog(
        u8"[자녀이름DBG] PTR +0x%03zX: 4004=%p / 4005=%p",
        off, (void*)namedPtr, (void*)blankPtr);

    if (++logged >= 64) {
      AddLog(u8"[자녀이름DBG] 포인터 후보가 많아 64개까지만 표시");
      break;
    }
  }

  if (logged == 0) {
    AddLog(
        u8"[자녀이름DBG] 4004/4005 차이 중 읽을 수 있는 포인터 후보 없음");
  }
}

static bool RunOnce() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    return false;
  }

  const uintptr_t namedRecord =
      FindOfficerRecord(rosterBase, kNamedChildId);
  const uintptr_t blankRecord =
      FindOfficerRecord(rosterBase, kBlankChildId);
  if (!namedRecord || !blankRecord)
    return false;

  uint16_t namedBirth = 0;
  uint16_t blankBirth = 0;
  if (!ChildManagerDetail::SafeRead16(namedRecord + 0x34, &namedBirth) ||
      namedBirth == 0) {
    return false;
  }
  ChildManagerDetail::SafeRead16(blankRecord + 0x34, &blankBirth);

  if (!IsChildLinkedInPregnancySlot(kNamedChildId))
    return false;

  AddLog(
      u8"[자녀이름DBG] ===== ID4004 이름결정 완료 스냅샷: record=%p birth=%u / ID4005 birth=%u =====",
      (void*)namedRecord,
      (unsigned)namedBirth,
      (unsigned)blankBirth);

  LogRecord16Snapshot(namedRecord);
  LogPointerCandidates(namedRecord, blankRecord);

  AddLog(u8"[자녀이름DBG] ===== ID4004 이름 진단 종료 =====");
  return true;
}

static void Tick() {
  static bool completed = false;
  static ULONGLONG lastCheckMs = 0;

  if (!bShowChildManagerWin) {
    completed = false;
    lastCheckMs = 0;
    return;
  }
  if (completed)
    return;

  const ULONGLONG now = GetTickCount64();
  if (lastCheckMs != 0 && now - lastCheckMs < 250)
    return;
  lastCheckMs = now;

  // 출산 직후 레코드가 만들어지는 시점에는 아직 이름 입력 화면이 진행 중일 수 있습니다.
  // 임신 슬롯 childPtr가 4004에 연결된 뒤에만 스냅샷을 남겨 최종 이름 상태를 잡습니다.
  if (RunOnce())
    completed = true;
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
