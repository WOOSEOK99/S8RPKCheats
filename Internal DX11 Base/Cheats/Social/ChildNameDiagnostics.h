#pragma once

#include "../../pch.h"
#include "../../showlog.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>

namespace DX11Base {
namespace ChildNameDiagnostics {

// SAN8RPK.pdb + 실게임 검증 완료:
//   san8r::PersonData::GetName() const
// PDB section 1(.text) offset 0x1712DB0 + section RVA 0x1000.
// 2026-09-26 실측: ID 952=유비, 4001=유목, 4002=유순, 4003=유충.
static constexpr uintptr_t kPersonDataGetNameRva = 0x1713DB0;
static constexpr uintptr_t kOfficerStride = 0x3D0;
static constexpr uint16_t kGeneratedFirstId = 4001;
static constexpr uint16_t kGeneratedLastId = 4020;

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

// C2712 방지: __try는 C++ 소멸자가 필요한 std::string 함수와 분리한다.
static const wchar_t* CallNativeGetNameSafe(uintptr_t fnAddr, uintptr_t officer) {
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

static bool ResolveNativeNameUtf8(uintptr_t fnAddr,
                                  uintptr_t officer,
                                  std::string& outName) {
  outName.clear();

  const wchar_t* result = CallNativeGetNameSafe(fnAddr, officer);
  if (!result)
    return false;

  wchar_t wide[64]{};
  if (!CopyWideSafe(result, wide, _countof(wide)) || wide[0] == L'\0')
    return false;

  const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 1)
    return false;

  std::string utf8((size_t)needed, '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                          &utf8[0], needed, nullptr, nullptr) <= 0) {
    return false;
  }
  utf8.resize(strlen(utf8.c_str()));
  if (utf8.empty())
    return false;

  outName = utf8;
  return true;
}

struct State {
  ULONGLONG lastSyncAt = 0;
  uintptr_t lastRosterBase = 0;
  std::array<std::string, 20> lastNames{};
};

static State g_state{};

static void SyncGeneratedChildNames() {
  const uintptr_t exeBase = (uintptr_t)GetModuleHandleW(nullptr);
  if (!exeBase)
    return;

  uintptr_t rosterBase = 0;
  if (!TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    return;
  }

  // 기존 역사무장 이름은 JSON/내장 맵을 그대로 유지한다.
  // 생성 자녀 4001~4020만 원본 게임의 런타임 이름으로 덮어쓴다.
  LoadOfficerNames();

  std::array<uintptr_t, 20> generated{};
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officer = rosterBase + (uintptr_t)i * kOfficerStride;
    uint16_t id = 0;
    if (!SafeRead16Local(officer + 0x08, &id))
      continue;
    if (id >= kGeneratedFirstId && id <= kGeneratedLastId)
      generated[(size_t)(id - kGeneratedFirstId)] = officer;
  }

  const uintptr_t fnAddr = exeBase + kPersonDataGetNameRva;
  int activeCount = 0;
  bool changed = false;

  for (uint16_t id = kGeneratedFirstId; id <= kGeneratedLastId; ++id) {
    const size_t index = (size_t)(id - kGeneratedFirstId);
    const uintptr_t officer = generated[index];

    // 생성 자녀 레코드는 예약 슬롯이 항상 존재하므로 출생연도(+0x34)가
    // 설정된 슬롯만 실제 사용 중인 자녀로 취급한다.
    uint16_t birth = 0;
    if (!officer || !SafeRead16Local(officer + 0x34, &birth) || birth == 0) {
      g_officerNames.erase((int)id);
      if (!g_state.lastNames[index].empty()) {
        g_state.lastNames[index].clear();
        changed = true;
      }
      continue;
    }

    std::string runtimeName;
    if (!ResolveNativeNameUtf8(fnAddr, officer, runtimeName)) {
      g_officerNames.erase((int)id);
      continue;
    }

    ++activeCount;
    g_officerNames[(int)id] = runtimeName;
    if (g_state.lastNames[index] != runtimeName) {
      g_state.lastNames[index] = runtimeName;
      AddLog(u8"[자녀이름] ID %u 런타임 이름 적용: %s",
             (unsigned)id, runtimeName.c_str());
      changed = true;
    }
  }

  if (changed || g_state.lastRosterBase != rosterBase) {
    AddLog(u8"[자녀이름] 생성 자녀 이름 동기화 완료: %d명 / GetName RVA +0x%llX",
           activeCount,
           (unsigned long long)kPersonDataGetNameRva);
  }
  g_state.lastRosterBase = rosterBase;
}

static void Tick() {
  // 세이브 재로드 시 같은 프로세스/같은 배열 주소를 재사용할 수도 있으므로
  // 일회성 플래그 대신 2초 간격으로 가볍게 재동기화한다.
  const ULONGLONG now = GetTickCount64();
  if (g_state.lastSyncAt != 0 && now - g_state.lastSyncAt < 2000)
    return;
  g_state.lastSyncAt = now;

  SyncGeneratedChildNames();
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
