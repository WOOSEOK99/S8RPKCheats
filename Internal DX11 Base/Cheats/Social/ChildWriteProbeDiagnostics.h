#pragma once

#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "ChildEarlyAppearance.h"

#include <Windows.h>
#include <array>
#include <cstdint>

namespace DX11Base {
namespace ChildWriteProbeDiagnostics {

struct ProbeEvent {
  volatile LONG ready = 0;
  uintptr_t rip = 0;
  uintptr_t address = 0;
  DWORD threadId = 0;
};

static constexpr LONG kMaxProbeEvents = 64;
static ProbeEvent g_probeEvents[kMaxProbeEvents]{};
static volatile LONG g_probeEventWriteIndex = 0;
static LONG g_probeEventFlushIndex = 0;

static PVOID g_probeVeh = nullptr;
static volatile LONG g_probeArmed = 0;
static volatile LONG g_probeRearmThreadId = 0;
static uintptr_t g_probeRecordBase = 0;
static uintptr_t g_probePageBase = 0;
static SIZE_T g_probePageSize = 0;
static DWORD g_probeBaseProtect = 0;
static uint16_t g_probeChildId = 0;

static bool SafeRead8(uintptr_t addr, uint8_t* out) {
  if (!out)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(
             GetCurrentProcess(),
             (LPCVOID)addr,
             out,
             sizeof(*out),
             &read) != FALSE &&
         read == sizeof(*out);
}

static bool ReadPregnancySlot(uintptr_t addr,
                              uint8_t& flag,
                              uint8_t& months,
                              uint16_t& childId) {
  flag = 0;
  months = 0;
  childId = 0;

  uintptr_t childPtr = 0;
  if (!SafeRead8(addr + 0x09, &flag) ||
      !SafeRead8(addr + 0x0A, &months) ||
      !ChildManagerDetail::SafeReadPtr(addr + 0x10, &childPtr)) {
    return false;
  }

  const uintptr_t childNorm =
      ChildManagerDetail::NormalizeOfficerPtr(childPtr);
  if (childNorm > 0x10000) {
    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childNorm + 0x08, &id) &&
        id >= 1 && id <= 5102) {
      childId = id;
    }
  }

  return true;
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

static bool IsGeneratedRecordFree(uintptr_t record) {
  if (record <= 0x10000)
    return false;

  uint16_t appearance = 0;
  uint16_t birth = 0;
  uint16_t death = 0;
  uintptr_t father = 0;
  uintptr_t mother = 0;

  if (!ChildManagerDetail::SafeRead16(record + 0x32, &appearance) ||
      !ChildManagerDetail::SafeRead16(record + 0x34, &birth) ||
      !ChildManagerDetail::SafeRead16(record + 0x36, &death) ||
      !ChildManagerDetail::SafeReadPtr(record + 0x48, &father) ||
      !ChildManagerDetail::SafeReadPtr(record + 0x50, &mother)) {
    return false;
  }

  return appearance == 0 &&
         birth == 0 &&
         death == 0 &&
         ChildManagerDetail::NormalizeOfficerPtr(father) == 0 &&
         ChildManagerDetail::NormalizeOfficerPtr(mother) == 0;
}

static bool FindFirstFreeGeneratedRecord(
    uintptr_t rosterBase,
    uint16_t& outId,
    uintptr_t& outRecord) {
  outId = 0;
  outRecord = 0;

  std::array<uintptr_t, 20> pool{};
  ResolveGeneratedPool(rosterBase, pool);

  for (int i = 0; i < 20; ++i) {
    const uintptr_t record = pool[(size_t)i];
    if (record == 0)
      continue;
    if (!IsGeneratedRecordFree(record))
      continue;

    outId = (uint16_t)(4001 + i);
    outRecord = record;
    return true;
  }

  return false;
}

static bool IsTargetAllocated(uintptr_t record) {
  return !IsGeneratedRecordFree(record);
}

static const char* FieldNameForOffset(uintptr_t offset) {
  if (offset >= 0x32 && offset < 0x34)
    return "appearance";
  if (offset >= 0x34 && offset < 0x36)
    return "birth";
  if (offset >= 0x36 && offset < 0x38)
    return "death";
  if (offset >= 0x48 && offset < 0x50)
    return "father";
  if (offset >= 0x50 && offset < 0x58)
    return "mother";
  return "record-core";
}

static uintptr_t GetExeImageSize(uintptr_t imageBase) {
  if (imageBase <= 0x10000)
    return 0;

  const IMAGE_DOS_HEADER* dos =
      (const IMAGE_DOS_HEADER*)imageBase;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return 0;

  const IMAGE_NT_HEADERS* nt =
      (const IMAGE_NT_HEADERS*)(imageBase + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return 0;

  return (uintptr_t)nt->OptionalHeader.SizeOfImage;
}

static LONG CALLBACK ProbeVehHandler(PEXCEPTION_POINTERS info) {
  if (!info || !info->ExceptionRecord || !info->ContextRecord)
    return EXCEPTION_CONTINUE_SEARCH;

  const DWORD code = info->ExceptionRecord->ExceptionCode;
  const DWORD tid = GetCurrentThreadId();

  if (code == STATUS_SINGLE_STEP) {
    const LONG rearmTid = g_probeRearmThreadId;
    if (rearmTid != 0 && (DWORD)rearmTid == tid) {
      if (InterlockedCompareExchange(&g_probeArmed, 0, 0) != 0 &&
          g_probePageBase != 0 &&
          g_probePageSize != 0) {
        DWORD oldProtect = 0;
        VirtualProtect(
            (LPVOID)g_probePageBase,
            g_probePageSize,
            g_probeBaseProtect | PAGE_GUARD,
            &oldProtect);
      }

      InterlockedExchange(&g_probeRearmThreadId, 0);
      info->ContextRecord->EFlags &= ~0x100u;
      return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (code != STATUS_GUARD_PAGE_VIOLATION ||
      InterlockedCompareExchange(&g_probeArmed, 0, 0) == 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (info->ExceptionRecord->NumberParameters < 2)
    return EXCEPTION_CONTINUE_SEARCH;

  const ULONG_PTR accessType =
      info->ExceptionRecord->ExceptionInformation[0];
  const uintptr_t accessAddress =
      (uintptr_t)info->ExceptionRecord->ExceptionInformation[1];

  if (accessAddress < g_probePageBase ||
      accessAddress >= g_probePageBase + g_probePageSize) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // PAGE_GUARD는 첫 접근에서 해제되므로, 해당 명령을 한 번 실행한 직후
  // single-step 예외에서 다시 guard를 거는 방식으로 짧게 재무장합니다.
  InterlockedExchange(&g_probeRearmThreadId, (LONG)tid);
  info->ContextRecord->EFlags |= 0x100u;

  const uintptr_t recordStart = g_probeRecordBase;
  const uintptr_t recordEnd = recordStart + 0x80;
  if (accessType == 1 &&
      accessAddress >= recordStart &&
      accessAddress < recordEnd) {
    const LONG index =
        InterlockedIncrement(&g_probeEventWriteIndex) - 1;
    if (index >= 0 && index < kMaxProbeEvents) {
      ProbeEvent& event = g_probeEvents[index];
      event.rip = (uintptr_t)info->ContextRecord->Rip;
      event.address = accessAddress;
      event.threadId = tid;
      InterlockedExchange(&event.ready, 1);
    }
  }

  return EXCEPTION_CONTINUE_EXECUTION;
}

static bool EnsureVehInstalled() {
  if (g_probeVeh)
    return true;

  g_probeVeh = AddVectoredExceptionHandler(1, ProbeVehHandler);
  if (!g_probeVeh) {
    AddLog(u8"[자녀쓰기DBG] VEH 설치 실패");
    return false;
  }

  AddLog(u8"[자녀쓰기DBG] VEH 설치 완료");
  return true;
}

static void ResetEventBuffer() {
  InterlockedExchange(&g_probeEventWriteIndex, 0);
  g_probeEventFlushIndex = 0;
  for (LONG i = 0; i < kMaxProbeEvents; ++i) {
    InterlockedExchange(&g_probeEvents[i].ready, 0);
    g_probeEvents[i].rip = 0;
    g_probeEvents[i].address = 0;
    g_probeEvents[i].threadId = 0;
  }
}

static void DisarmProbe(const char* reason) {
  const bool wasArmed =
      InterlockedExchange(&g_probeArmed, 0) != 0;

  if (g_probePageBase != 0 &&
      g_probePageSize != 0 &&
      g_probeBaseProtect != 0) {
    DWORD oldProtect = 0;
    VirtualProtect(
        (LPVOID)g_probePageBase,
        g_probePageSize,
        g_probeBaseProtect,
        &oldProtect);
  }

  if (wasArmed) {
    AddLog(
        u8"[자녀쓰기DBG] 감시 해제: ID %u / %s",
        (unsigned)g_probeChildId,
        reason ? reason : "done");
  }

  g_probeRecordBase = 0;
  g_probePageBase = 0;
  g_probePageSize = 0;
  g_probeBaseProtect = 0;
  g_probeChildId = 0;
}

static bool ArmProbe(uint16_t childId, uintptr_t record) {
  if (record <= 0x10000)
    return false;

  if (InterlockedCompareExchange(&g_probeArmed, 0, 0) != 0) {
    if (g_probeChildId == childId &&
        g_probeRecordBase == record) {
      return true;
    }
    DisarmProbe("target changed");
  }

  if (!EnsureVehInstalled())
    return false;

  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const SIZE_T pageSize =
      si.dwPageSize != 0 ? (SIZE_T)si.dwPageSize : 0x1000;
  const uintptr_t pageBase =
      record & ~((uintptr_t)pageSize - 1);

  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(
          (LPCVOID)pageBase,
          &mbi,
          sizeof(mbi)) != sizeof(mbi)) {
    AddLog(
        u8"[자녀쓰기DBG] VirtualQuery 실패: ID %u / record=%p",
        (unsigned)childId,
        (void*)record);
    return false;
  }

  DWORD baseProtect = mbi.Protect & ~PAGE_GUARD;
  if (baseProtect == 0 ||
      baseProtect == PAGE_NOACCESS) {
    AddLog(
        u8"[자녀쓰기DBG] 감시 불가 보호속성: ID %u / protect=0x%08X",
        (unsigned)childId,
        (unsigned)mbi.Protect);
    return false;
  }

  ResetEventBuffer();
  g_probeRecordBase = record;
  g_probePageBase = pageBase;
  g_probePageSize = pageSize;
  g_probeBaseProtect = baseProtect;
  g_probeChildId = childId;

  DWORD oldProtect = 0;
  if (!VirtualProtect(
          (LPVOID)pageBase,
          pageSize,
          baseProtect | PAGE_GUARD,
          &oldProtect)) {
    AddLog(
        u8"[자녀쓰기DBG] PAGE_GUARD 설정 실패: ID %u / record=%p",
        (unsigned)childId,
        (void*)record);
    g_probeRecordBase = 0;
    g_probePageBase = 0;
    g_probePageSize = 0;
    g_probeBaseProtect = 0;
    g_probeChildId = 0;
    return false;
  }

  InterlockedExchange(&g_probeArmed, 1);
  AddLog(
      u8"[자녀쓰기DBG] 감시 시작: ID %u / record=%p / page=%p / core=+0x00..+0x7F",
      (unsigned)childId,
      (void*)record,
      (void*)pageBase);
  return true;
}

static void LogInstructionBytes(uintptr_t rip) {
  uint8_t bytes[16]{};
  SIZE_T read = 0;
  if (ReadProcessMemory(
          GetCurrentProcess(),
          (LPCVOID)rip,
          bytes,
          sizeof(bytes),
          &read) == FALSE ||
      read == 0) {
    AddLog(u8"[자녀쓰기DBG]   RIP 바이트 읽기 실패");
    return;
  }

  char text[16 * 3 + 1]{};
  size_t pos = 0;
  for (SIZE_T i = 0; i < read && i < 16; ++i) {
    const int written = sprintf_s(
        text + pos,
        sizeof(text) - pos,
        "%02X%s",
        (unsigned)bytes[i],
        (i + 1 < read && i + 1 < 16) ? " " : "");
    if (written <= 0)
      break;
    pos += (size_t)written;
    if (pos >= sizeof(text))
      break;
  }

  AddLog(u8"[자녀쓰기DBG]   bytes: %s", text);
}

static void FlushProbeEvents() {
  LONG writeCount =
      InterlockedCompareExchange(&g_probeEventWriteIndex, 0, 0);
  if (writeCount > kMaxProbeEvents)
    writeCount = kMaxProbeEvents;

  const uintptr_t imageBase =
      (uintptr_t)GetModuleHandle(nullptr);
  const uintptr_t imageSize = GetExeImageSize(imageBase);

  while (g_probeEventFlushIndex < writeCount) {
    ProbeEvent& event =
        g_probeEvents[g_probeEventFlushIndex];
    if (InterlockedCompareExchange(&event.ready, 0, 0) == 0)
      break;

    const uintptr_t offset =
        event.address >= g_probeRecordBase
            ? event.address - g_probeRecordBase
            : 0;
    const bool inExe =
        imageBase != 0 &&
        imageSize != 0 &&
        event.rip >= imageBase &&
        event.rip < imageBase + imageSize;

    if (inExe) {
      AddLog(
          u8"[자녀쓰기DBG] WRITE ID %u offset=+0x%llX field=%s RIP=%p RVA=+0x%llX thread=%u",
          (unsigned)g_probeChildId,
          (unsigned long long)offset,
          FieldNameForOffset(offset),
          (void*)event.rip,
          (unsigned long long)(event.rip - imageBase),
          (unsigned)event.threadId);
    } else {
      AddLog(
          u8"[자녀쓰기DBG] WRITE ID %u offset=+0x%llX field=%s RIP=%p RVA=outside-exe thread=%u",
          (unsigned)g_probeChildId,
          (unsigned long long)offset,
          FieldNameForOffset(offset),
          (void*)event.rip,
          (unsigned)event.threadId);
    }

    LogInstructionBytes(event.rip);
    InterlockedExchange(&event.ready, 2);
    ++g_probeEventFlushIndex;
  }

  const LONG rawCount =
      InterlockedCompareExchange(&g_probeEventWriteIndex, 0, 0);
  if (rawCount > kMaxProbeEvents &&
      g_probeEventFlushIndex == kMaxProbeEvents) {
    AddLog(
        u8"[자녀쓰기DBG] 이벤트 버퍼 초과: 총 %ld회 중 처음 %ld회만 기록",
        rawCount,
        kMaxProbeEvents);
  }
}

static bool HasPendingBirthAtZeroMonths() {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;

  const uintptr_t tableBase = gameBase + 0x5B40;
  for (int slot = 0; slot < 3; ++slot) {
    uint8_t flag = 0;
    uint8_t months = 0;
    uint16_t childId = 0;
    if (!ReadPregnancySlot(
            tableBase + (uintptr_t)slot * 0x28,
            flag,
            months,
            childId)) {
      continue;
    }

    if (flag == 1 &&
        months == 0 &&
        childId == 0) {
      return true;
    }
  }

  return false;
}

static void Tick() {
  static ULONGLONG lastResolveMs = 0;

  FlushProbeEvents();

  if (!bShowChildManagerWin) {
    if (InterlockedCompareExchange(&g_probeArmed, 0, 0) != 0)
      DisarmProbe("child manager closed");
    lastResolveMs = 0;
    return;
  }

  const ULONGLONG now = GetTickCount64();
  if (lastResolveMs != 0 && now - lastResolveMs < 100)
    return;
  lastResolveMs = now;

  if (InterlockedCompareExchange(&g_probeArmed, 0, 0) != 0) {
    if (g_probeRecordBase != 0 &&
        IsTargetAllocated(g_probeRecordBase)) {
      FlushProbeEvents();
      DisarmProbe("target record allocated");
      return;
    }

    if (!HasPendingBirthAtZeroMonths()) {
      FlushProbeEvents();
      DisarmProbe("birth pending state ended");
    }
    return;
  }

  if (!HasPendingBirthAtZeroMonths())
    return;

  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase,
          heroMaster,
          heroId)) {
    return;
  }

  uint16_t nextChildId = 0;
  uintptr_t nextRecord = 0;
  if (!FindFirstFreeGeneratedRecord(
          rosterBase,
          nextChildId,
          nextRecord)) {
    AddLog(
        u8"[자녀쓰기DBG] 출산 직전 상태지만 4001~4020 빈 레코드를 찾지 못함");
    return;
  }

  ArmProbe(nextChildId, nextRecord);
}

} // namespace ChildWriteProbeDiagnostics
} // namespace DX11Base
