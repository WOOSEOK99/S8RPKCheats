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
#include <string>

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uint16_t kTargetChildId = 4004;
static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;
static constexpr uintptr_t kPregnancySlotStride = 0x28;
static constexpr uintptr_t kPregnancyChildPtrOffset = 0x10;
static constexpr size_t kOfficerRecordSize = 0x3D0;

// TraitTextNameHook가 이미 검증해서 사용하는 원본 이름 getter.
// 이 함수는 RCX+0x08의 16-bit ID를 읽고 UTF-16 문자열 포인터를 반환한다.
// 장수 레코드도 +0x08이 ID이므로, 생성 장수에도 통하는지 읽기 전용으로 직접 확인한다.
static constexpr uintptr_t kVersionNameGetterOffset = 0x5CB0;

using NameGetterFn = const wchar_t* (__fastcall*)(void*);

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &read) != FALSE &&
         read == size;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000 || targetId == 0)
    return 0;

  const uintptr_t direct =
      rosterBase + (uintptr_t)(targetId - 1) * kOfficerRecordSize;
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
    if (!ChildManagerDetail::SafeReadPtr(
            tableBase + (uintptr_t)slot * kPregnancySlotStride +
                kPregnancyChildPtrOffset,
            &rawChildPtr)) {
      continue;
    }

    const uintptr_t childPtr = ChildManagerDetail::NormalizeOfficerPtr(rawChildPtr);
    if (childPtr <= 0x10000)
      continue;

    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childPtr + 0x08, &id) && id == childId)
      return true;
  }
  return false;
}

// __try 함수 안에는 소멸자가 필요한 C++ 객체를 두지 않는다.
static bool SafeCallNameGetter(NameGetterFn fn, uintptr_t objectPtr,
                               const wchar_t** outPtr) {
  if (!fn || objectPtr <= 0x10000 || !outPtr)
    return false;

  *outPtr = nullptr;
  __try {
    *outPtr = fn((void*)objectPtr);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    *outPtr = nullptr;
    return false;
  }
}

static bool CopyReturnedWide(const wchar_t* src,
                             wchar_t* dst,
                             size_t dstCount) {
  if (!src || !dst || dstCount < 2)
    return false;

  memset(dst, 0, dstCount * sizeof(wchar_t));
  const uintptr_t base = (uintptr_t)src;
  for (size_t i = 0; i + 1 < dstCount; ++i) {
    wchar_t ch = 0;
    if (!SafeReadMem(base + i * sizeof(wchar_t), &ch, sizeof(ch)))
      return false;
    dst[i] = ch;
    if (ch == L'\0')
      return i > 0;
  }
  dst[dstCount - 1] = L'\0';
  return true;
}

static std::string WideToUtf8(const wchar_t* text) {
  if (!text || !*text)
    return std::string();

  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1)
    return std::string();

  std::string result((size_t)needed - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1,
                      result.data(), needed, nullptr, nullptr);
  return result;
}

static void ProbeOne(NameGetterFn getter,
                     uintptr_t rosterBase,
                     uint16_t id) {
  const uintptr_t record = FindOfficerRecord(rosterBase, id);
  if (!record) {
    AddLog(u8"[자녀이름DBG] GETTER ID=%u record 없음", (unsigned)id);
    return;
  }

  const wchar_t* returned = nullptr;
  if (!SafeCallNameGetter(getter, record, &returned)) {
    AddLog(u8"[자녀이름DBG] GETTER ID=%u record=%p CALL_EXCEPTION",
           (unsigned)id, (void*)record);
    return;
  }

  if (!returned) {
    AddLog(u8"[자녀이름DBG] GETTER ID=%u record=%p -> NULL",
           (unsigned)id, (void*)record);
    return;
  }

  wchar_t buffer[48]{};
  if (!CopyReturnedWide(returned, buffer, _countof(buffer))) {
    AddLog(u8"[자녀이름DBG] GETTER ID=%u record=%p -> ptr=%p READ_FAIL/EMPTY",
           (unsigned)id, (void*)record, (void*)returned);
    return;
  }

  const std::string utf8 = WideToUtf8(buffer);
  AddLog(u8"[자녀이름DBG] GETTER ID=%u record=%p -> ptr=%p text='%s'",
         (unsigned)id, (void*)record, (void*)returned, utf8.c_str());
}

static void RunNativeGetterProbe() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    AddLog(u8"[자녀이름DBG] native getter probe: roster resolve 실패");
    return;
  }

  const uintptr_t versionBase =
      (uintptr_t)GetModuleHandleW(L"version.dll");
  if (!versionBase) {
    AddLog(u8"[자녀이름DBG] native getter probe: version.dll 없음");
    return;
  }

  const uintptr_t target = versionBase + kVersionNameGetterOffset;
  NameGetterFn getter = (NameGetterFn)target;

  AddLog(u8"[자녀이름DBG] ===== version.dll+5CB0 장수 이름 getter 가설 검증 =====");
  AddLog(u8"[자녀이름DBG] target=%p / roster=%p / hero=%u",
         (void*)target, (void*)rosterBase, (unsigned)heroId);

  // 기존 생성 자녀, 현재 생성 자녀, 일반 장수를 한 번에 비교한다.
  const uint16_t ids[] = {4001, 4002, 4003, 4004, 952, 565, 163, 792};
  for (uint16_t id : ids)
    ProbeOne(getter, rosterBase, id);

  AddLog(u8"[자녀이름DBG] ===== version.dll+5CB0 장수 이름 getter 가설 검증 종료 =====");
}

struct MonitorState {
  bool armed = false;
  bool completed = false;
  bool probed = false;
  ULONGLONG lastPollMs = 0;
  ULONGLONG linkedMs = 0;
};

static MonitorState g_state{};

static bool Arm() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    return false;
  }

  const uintptr_t record = FindOfficerRecord(rosterBase, kTargetChildId);
  if (!record)
    return false;

  uint16_t birth = 0;
  if (!ChildManagerDetail::SafeRead16(record + 0x34, &birth))
    return false;

  g_state.armed = true;
  g_state.completed = false;
  g_state.probed = false;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 native getter 검증 대기: record=%p birth=%u =====",
         (void*)record, (unsigned)birth);
  return true;
}

static void Poll() {
  const ULONGLONG now = GetTickCount64();
  if (g_state.lastPollMs != 0 && now - g_state.lastPollMs < 100)
    return;
  g_state.lastPollMs = now;

  if (g_state.linkedMs == 0 && IsChildLinkedInPregnancySlot(kTargetChildId)) {
    g_state.linkedMs = now;
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. native getter 직접 호출을 준비합니다.");
  }

  if (g_state.linkedMs == 0)
    return;

  if (!g_state.probed && now - g_state.linkedMs >= 250) {
    RunNativeGetterProbe();
    g_state.probed = true;
  }

  if (g_state.probed && now - g_state.linkedMs >= 700) {
    AddLog(u8"[자녀이름DBG] ===== ID4004 native getter 진단 종료 =====");
    g_state.completed = true;
  }
}

static void Tick() {
  if (g_state.completed)
    return;

  if (!g_state.armed) {
    if (!bShowChildManagerWin)
      return;
    Arm();
    return;
  }

  Poll();
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
