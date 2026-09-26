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

static constexpr uint16_t kTargetChildId = 4004;
static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;
static constexpr uintptr_t kPregnancySlotStride = 0x28;
static constexpr uintptr_t kPregnancyChildPtrOffset = 0x10;
static constexpr size_t kOfficerRecordSize = 0x3D0;

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &read) != FALSE &&
         read == size;
}

static bool IsReadablePointer(uintptr_t addr) {
  if (addr <= 0x10000)
    return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS))
    return false;
  return true;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000)
    return 0;
  const uintptr_t direct = rosterBase + (uintptr_t)(targetId - 1) * kOfficerRecordSize;
  uint16_t verify = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verify) && verify == targetId)
    return direct;
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p = rosterBase + (uintptr_t)i * kOfficerRecordSize;
    if (ChildManagerDetail::SafeRead16(p + 0x08, &verify) && verify == targetId)
      return p;
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
    if (!ChildManagerDetail::SafeReadPtr(tableBase + (uintptr_t)slot * kPregnancySlotStride +
                                            kPregnancyChildPtrOffset,
                                        &rawChildPtr))
      continue;
    const uintptr_t childPtr = ChildManagerDetail::NormalizeOfficerPtr(rawChildPtr);
    if (childPtr <= 0x10000)
      continue;
    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childPtr + 0x08, &id) && id == childId)
      return true;
  }
  return false;
}

struct PointerWatch {
  uintptr_t recordOffset = 0;
  const char* label = nullptr;
  uintptr_t ptr = 0;
  bool valid = false;
  std::array<uint8_t, 0x80> bytes{};
};

struct MonitorState {
  bool armed = false;
  bool completed = false;
  uintptr_t record = 0;
  uint16_t initialBirth = 0;
  ULONGLONG lastPollMs = 0;
  ULONGLONG linkedMs = 0;
  int changeCount = 0;
  std::array<uint8_t, kOfficerRecordSize> recordBytes{};
  PointerWatch p10{0x10, "P10"};
  PointerWatch p360{0x360, "P360"};
};

static MonitorState g_state{};

static void LogWordDiffs(const char* label, const uint8_t* before,
                         const uint8_t* after, size_t size) {
  if (!label || !before || !after)
    return;
  for (size_t off = 0; off + 2 <= size; off += 2) {
    uint16_t a = 0, b = 0;
    memcpy(&a, before + off, sizeof(a));
    memcpy(&b, after + off, sizeof(b));
    if (a == b)
      continue;
    AddLog(u8"[자녀이름DBG] CHANGE %s +0x%03zX : 0x%04X(%u) -> 0x%04X(%u)",
           label, off, (unsigned)a, (unsigned)a, (unsigned)b, (unsigned)b);
    ++g_state.changeCount;
  }
}

static void LogSnapshot(const char* label, uintptr_t ptr,
                        const std::array<uint8_t, 0x80>& data) {
  AddLog(u8"[자녀이름DBG] SNAP %s ptr=%p", label, (void*)ptr);
  for (size_t off = 0; off < data.size(); off += 0x10) {
    uint16_t w[8]{};
    memcpy(w, data.data() + off, sizeof(w));
    AddLog(u8"[자녀이름DBG] SNAP %s +0x%02zX: %04X %04X %04X %04X %04X %04X %04X %04X",
           label, off, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
           (unsigned)w[4], (unsigned)w[5], (unsigned)w[6], (unsigned)w[7]);
  }
}

static bool ReadRecordPointer(uintptr_t record, uintptr_t offset, uintptr_t* outPtr) {
  if (!outPtr)
    return false;
  uintptr_t value = 0;
  if (!SafeReadMem(record + offset, &value, sizeof(value)))
    return false;
  value &= 0x0000FFFFFFFFFFFFULL;
  *outPtr = value;
  return IsReadablePointer(value);
}

static void InitPointerWatch(PointerWatch& watch) {
  uintptr_t ptr = 0;
  if (!ReadRecordPointer(g_state.record, watch.recordOffset, &ptr)) {
    watch.ptr = 0;
    watch.valid = false;
    watch.bytes.fill(0);
    AddLog(u8"[자녀이름DBG] BASE %s : 아직 유효 포인터 없음", watch.label);
    return;
  }
  watch.ptr = ptr;
  watch.valid = SafeReadMem(ptr, watch.bytes.data(), watch.bytes.size());
  if (watch.valid)
    AddLog(u8"[자녀이름DBG] BASE %s : 4004+0x%zX -> %p",
           watch.label, watch.recordOffset, (void*)watch.ptr);
}

static void PollPointerWatch(PointerWatch& watch) {
  uintptr_t currentPtr = 0;
  const bool currentValid = ReadRecordPointer(g_state.record, watch.recordOffset, &currentPtr);
  if (!currentValid) {
    if (watch.valid) {
      AddLog(u8"[자녀이름DBG] CHANGE %s pointer %p -> invalid", watch.label, (void*)watch.ptr);
      ++g_state.changeCount;
      watch.valid = false;
      watch.ptr = 0;
      watch.bytes.fill(0);
    }
    return;
  }

  std::array<uint8_t, 0x80> current{};
  if (!SafeReadMem(currentPtr, current.data(), current.size()))
    return;

  if (!watch.valid || watch.ptr != currentPtr) {
    AddLog(u8"[자녀이름DBG] CHANGE %s pointer %p -> %p",
           watch.label, (void*)watch.ptr, (void*)currentPtr);
    ++g_state.changeCount;
    LogSnapshot(watch.label, currentPtr, current);
    watch.ptr = currentPtr;
    watch.valid = true;
    watch.bytes = current;
    return;
  }

  LogWordDiffs(watch.label, watch.bytes.data(), current.data(), current.size());
  watch.bytes = current;
}

static bool ArmBeforeBirth() {
  uintptr_t rosterBase = 0, heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(rosterBase, heroMaster, heroId))
    return false;
  const uintptr_t record = FindOfficerRecord(rosterBase, kTargetChildId);
  if (!record)
    return false;
  uint16_t birth = 0;
  if (!ChildManagerDetail::SafeRead16(record + 0x34, &birth))
    return false;
  if (!SafeReadMem(record, g_state.recordBytes.data(), g_state.recordBytes.size()))
    return false;

  g_state.armed = true;
  g_state.record = record;
  g_state.initialBirth = birth;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;
  g_state.changeCount = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 출산 전 기준선 감시 시작: record=%p birth=%u =====",
         (void*)record, (unsigned)birth);
  InitPointerWatch(g_state.p10);
  InitPointerWatch(g_state.p360);
  return true;
}

static void Poll() {
  const ULONGLONG now = GetTickCount64();
  if (g_state.lastPollMs != 0 && now - g_state.lastPollMs < 100)
    return;
  g_state.lastPollMs = now;

  std::array<uint8_t, kOfficerRecordSize> current{};
  if (SafeReadMem(g_state.record, current.data(), current.size())) {
    LogWordDiffs("REC4004", g_state.recordBytes.data(), current.data(), current.size());
    g_state.recordBytes = current;
  }

  PollPointerWatch(g_state.p10);
  PollPointerWatch(g_state.p360);

  const bool linked = IsChildLinkedInPregnancySlot(kTargetChildId);
  if (linked && g_state.linkedMs == 0) {
    g_state.linkedMs = now;
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. 최종 상태도 2초 더 기록합니다.");
  }

  if (g_state.linkedMs != 0 && now - g_state.linkedMs >= 2000) {
    AddLog(u8"[자녀이름DBG] ===== ID4004 출산 전→이름확정 감시 종료 / 변화=%d =====",
           g_state.changeCount);
    g_state.armed = false;
    g_state.completed = true;
  }
}

static void Tick() {
  if (g_state.completed)
    return;
  if (!g_state.armed) {
    if (!bShowChildManagerWin)
      return;
    ArmBeforeBirth();
    return;
  }
  Poll();
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
