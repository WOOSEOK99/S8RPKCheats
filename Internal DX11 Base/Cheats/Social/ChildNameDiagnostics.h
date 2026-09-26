#pragma once

#include "../../pch.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../Officer/OfficerRosterResolve.h"

#include <Windows.h>
#include <cstdint>

namespace DX11Base {
namespace ChildNameDiagnostics {

// SAN8RPK.pdb 실측 심볼:
//   san8r::PersonData::GetName() const
// PDB section 1(.text) offset 0x1712DB0 + section RVA 0x1000.
static constexpr uintptr_t kPersonDataGetNameRva = 0x1713DB0;
static constexpr uintptr_t kOfficerStride = 0x3D0;

using NativePersonGetName = const wchar_t* (__fastcall*)(const void* self);

static bool SafeRead16Local(uintptr_t addr, uint16_t* out) {
  if (!out)
    return false;
  __try {
    *out = *(const uint16_t*)addr;
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = 0;
    return false;
  }
}

static uintptr_t ResolveOfficerById(uintptr_t rosterBase, uint16_t id) {
  if (rosterBase <= 0x10000 || id < 1 || id > 5102)
    return 0;

  const uintptr_t direct = rosterBase + (uintptr_t)(id - 1) * kOfficerStride;
  uint16_t verify = 0;
  if (SafeRead16Local(direct + 0x08, &verify) && verify == id)
    return direct;

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p = rosterBase + (uintptr_t)i * kOfficerStride;
    if (SafeRead16Local(p + 0x08, &verify) && verify == id)
      return p;
  }
  return 0;
}

static bool CopyWideSafe(const wchar_t* src, wchar_t* out, size_t capacity) {
  if (!src || !out || capacity < 2)
    return false;

  __try {
    for (size_t i = 0; i + 1 < capacity; ++i) {
      const wchar_t ch = src[i];
      out[i] = ch;
      if (ch == L'\0')
        return true;
    }
    out[capacity - 1] = L'\0';
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out[0] = L'\0';
    return false;
  }
}

static bool WideToUtf8(const wchar_t* wide, char* out, int outSize) {
  if (!wide || !out || outSize <= 1)
    return false;
  out[0] = '\0';
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                    out, outSize, nullptr, nullptr);
  return n > 0;
}

static const wchar_t* CallNativeGetName(uintptr_t fnAddr, uintptr_t officer) {
  if (fnAddr <= 0x10000 || officer <= 0x10000)
    return nullptr;

  const auto fn = reinterpret_cast<NativePersonGetName>(fnAddr);
  const wchar_t* result = nullptr;
  __try {
    result = fn(reinterpret_cast<const void*>(officer));
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    result = nullptr;
  }
  return result;
}

static void LogOneName(uintptr_t fnAddr, uintptr_t rosterBase, uint16_t id) {
  const uintptr_t officer = ResolveOfficerById(rosterBase, id);
  if (!officer) {
    AddLog(u8"[자녀이름DBG] id=%u 레코드 찾기 실패", (unsigned)id);
    return;
  }

  const wchar_t* namePtr = CallNativeGetName(fnAddr, officer);
  if (!namePtr) {
    AddLog(u8"[자녀이름DBG] id=%u GetName 호출 실패/NULL officer=%p",
           (unsigned)id, (void*)officer);
    return;
  }

  wchar_t wide[64]{};
  if (!CopyWideSafe(namePtr, wide, _countof(wide))) {
    AddLog(u8"[자녀이름DBG] id=%u 이름 포인터 읽기 실패 namePtr=%p",
           (unsigned)id, (const void*)namePtr);
    return;
  }

  char utf8[256]{};
  if (!WideToUtf8(wide, utf8, (int)sizeof(utf8))) {
    AddLog(u8"[자녀이름DBG] id=%u UTF-8 변환 실패 namePtr=%p",
           (unsigned)id, (const void*)namePtr);
    return;
  }

  AddLog(u8"[자녀이름DBG] id=%u officer=%p namePtr=%p name=%s",
         (unsigned)id, (void*)officer, (const void*)namePtr, utf8);
}

static void ProbeNativePersonGetName() {
  const uintptr_t exeBase = (uintptr_t)GetModuleHandleW(nullptr);
  if (!exeBase) {
    AddLog(u8"[자녀이름DBG] SAN8RPK.exe 베이스 확보 실패");
    return;
  }

  uintptr_t rosterBase = 0;
  if (!TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    AddLog(u8"[자녀이름DBG] 무장 배열 베이스 확보 실패");
    return;
  }

  const uintptr_t fnAddr = exeBase + kPersonDataGetNameRva;
  AddLog(u8"[자녀이름DBG] ===== PDB PersonData::GetName 직접 진단 시작 =====");
  AddLog(u8"[자녀이름DBG] exe=%p roster=%p GetName=+0x%llX (%p)",
         (void*)exeBase,
         (void*)rosterBase,
         (unsigned long long)kPersonDataGetNameRva,
         (void*)fnAddr);

  // 일반 무장 952를 대조군으로 먼저 확인하고, 기존 생성 자녀만 읽는다.
  LogOneName(fnAddr, rosterBase, 952);
  LogOneName(fnAddr, rosterBase, 4001);
  LogOneName(fnAddr, rosterBase, 4002);
  LogOneName(fnAddr, rosterBase, 4003);

  AddLog(u8"[자녀이름DBG] ===== PDB PersonData::GetName 직접 진단 종료 =====");
}

struct State {
  bool done = false;
  ULONGLONG openedAt = 0;
};

static State g_state{};

static void Tick() {
  if (g_state.done)
    return;

  if (!bShowChildManagerWin) {
    g_state.openedAt = 0;
    return;
  }

  const ULONGLONG now = GetTickCount64();
  if (g_state.openedAt == 0) {
    g_state.openedAt = now;
    return;
  }

  if (now - g_state.openedAt < 300)
    return;

  ProbeNativePersonGetName();
  g_state.done = true;
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
