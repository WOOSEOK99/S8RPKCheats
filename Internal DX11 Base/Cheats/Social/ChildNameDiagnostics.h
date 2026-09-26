#pragma once

#include "../../pch.h"
#include "../../showlog.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"

#include <Windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace DX11Base {
namespace ChildNameDiagnostics {

// SAN8RPK.pdb에서 주소까지 확인된 PersonData 원본 getter.
// GetName은 2026-09-26 실게임에서 일반 무장/생성 자녀 모두 검증 완료.
// GetAzana는 같은 PDB 공개 심볼이며 동일한 const wchar_t* 반환형이다.
static constexpr uintptr_t kPersonDataGetNameRva = 0x1713DB0;
static constexpr uintptr_t kPersonDataGetAzanaRva = 0x1712A80;
static constexpr uintptr_t kOfficerStride = 0x3D0;
static constexpr int kOfficerCount = 5102;
static constexpr uint16_t kGeneratedFirstId = 4001;
static constexpr uint16_t kGeneratedLastId = 4020;

using NativePersonTextGetter = const wchar_t* (__fastcall*)(const void* self);

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
static const wchar_t* CallNativeTextGetterSafe(uintptr_t fnAddr,
                                               uintptr_t officer) {
  if (fnAddr <= 0x10000 || officer <= 0x10000)
    return nullptr;

  const auto fn = reinterpret_cast<NativePersonTextGetter>(fnAddr);
  const wchar_t* result = nullptr;
  __try {
    result = fn(reinterpret_cast<const void*>(officer));
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    result = nullptr;
  }
  return result;
}

static bool ResolveNativeTextUtf8(uintptr_t fnAddr,
                                  uintptr_t officer,
                                  std::string& outText) {
  outText.clear();

  const wchar_t* result = CallNativeTextGetterSafe(fnAddr, officer);
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

  outText = NormalizeUtf8(utf8);
  return !outText.empty();
}

struct State {
  ULONGLONG lastFullSyncAt = 0;
  ULONGLONG lastChildSyncAt = 0;
  uintptr_t lastRosterBase = 0;
  std::unordered_map<int, std::string> lastNativeNames;
};

static State g_state{};

static bool HasExternalOverride(int id) {
  const auto current = g_officerNames.find(id);
  if (current == g_officerNames.end())
    return false;

  const auto native = g_state.lastNativeNames.find(id);
  if (native == g_state.lastNativeNames.end()) {
    // 첫 원본 동기화 전에 이미 존재하는 이름은 외부 JSON override로 간주한다.
    return true;
  }

  // 마지막으로 우리가 넣은 원본 이름과 달라졌다면 사용자가 이름을 수정한 것.
  return current->second != native->second;
}

static void DropPreviousNativeCache() {
  for (const auto& kv : g_state.lastNativeNames) {
    const auto current = g_officerNames.find(kv.first);
    if (current != g_officerNames.end() && current->second == kv.second)
      g_officerNames.erase(current);
  }
  g_state.lastNativeNames.clear();
}

static bool SyncOneOfficer(uintptr_t officer,
                           uint16_t expectedId,
                           uintptr_t getNameAddr,
                           uintptr_t getAzanaAddr,
                           int* nativeCount,
                           int* overrideCount,
                           bool logGeneratedChange) {
  uint16_t id = 0;
  if (!SafeRead16Local(officer + 0x08, &id) || id < 1 || id > kOfficerCount)
    return false;
  if (expectedId != 0 && id != expectedId)
    return false;

  if (HasExternalOverride((int)id)) {
    if (overrideCount)
      ++(*overrideCount);
    return false;
  }

  std::string name;
  if (!ResolveNativeTextUtf8(getNameAddr, officer, name))
    return false;

  std::string azana;
  ResolveNativeTextUtf8(getAzanaAddr, officer, azana);

  std::string display = name;
  if (!azana.empty() && azana != name)
    display += u8"(" + azana + u8")";

  const auto previous = g_state.lastNativeNames.find((int)id);
  const bool changed =
      previous == g_state.lastNativeNames.end() || previous->second != display;

  g_officerNames[(int)id] = display;
  g_state.lastNativeNames[(int)id] = display;
  if (nativeCount)
    ++(*nativeCount);

  if (logGeneratedChange && changed &&
      id >= kGeneratedFirstId && id <= kGeneratedLastId) {
    AddLog(u8"[무장이름] 생성 자녀 ID %u 원본 이름 적용: %s",
           (unsigned)id, display.c_str());
  }
  return true;
}

static bool ResolveRuntimeContext(uintptr_t* outExeBase,
                                  uintptr_t* outRosterBase) {
  if (!outExeBase || !outRosterBase)
    return false;
  *outExeBase = 0;
  *outRosterBase = 0;

  const uintptr_t exeBase = (uintptr_t)GetModuleHandleW(nullptr);
  if (!exeBase)
    return false;

  uintptr_t rosterBase = 0;
  if (!TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    return false;
  }

  *outExeBase = exeBase;
  *outRosterBase = rosterBase;
  return true;
}

static void SyncAllOfficerNames(uintptr_t exeBase, uintptr_t rosterBase) {
  LoadOfficerNames(); // 외부 S8RPK_cheat_char.json이 있으면 override로 먼저 읽는다.

  if (g_state.lastRosterBase != 0 && g_state.lastRosterBase != rosterBase)
    DropPreviousNativeCache();

  const uintptr_t getNameAddr = exeBase + kPersonDataGetNameRva;
  const uintptr_t getAzanaAddr = exeBase + kPersonDataGetAzanaRva;
  int nativeCount = 0;
  int overrideCount = 0;

  for (int i = 0; i < kOfficerCount; ++i) {
    const uintptr_t officer = rosterBase + (uintptr_t)i * kOfficerStride;
    SyncOneOfficer(officer, 0, getNameAddr, getAzanaAddr,
                   &nativeCount, &overrideCount, false);
  }

  if (g_state.lastRosterBase != rosterBase || g_state.lastFullSyncAt == 0) {
    AddLog(u8"[무장이름] 원본 이름/자 동기화 완료: native=%d override=%d / GetName +0x%llX / GetAzana +0x%llX",
           nativeCount, overrideCount,
           (unsigned long long)kPersonDataGetNameRva,
           (unsigned long long)kPersonDataGetAzanaRva);
  }
  g_state.lastRosterBase = rosterBase;
}

static void SyncGeneratedChildren(uintptr_t exeBase, uintptr_t rosterBase) {
  const uintptr_t getNameAddr = exeBase + kPersonDataGetNameRva;
  const uintptr_t getAzanaAddr = exeBase + kPersonDataGetAzanaRva;

  // 생성 자녀는 출산 직후 이름이 생길 수 있으므로 전체 5102명 재조회 대신
  // 4001~4020의 20개 슬롯만 짧은 주기로 갱신한다.
  for (uint16_t id = kGeneratedFirstId; id <= kGeneratedLastId; ++id) {
    const uintptr_t officer =
        rosterBase + (uintptr_t)(id - 1) * kOfficerStride;

    uint16_t birth = 0;
    if (!SafeRead16Local(officer + 0x34, &birth) || birth == 0)
      continue;

    SyncOneOfficer(officer, id, getNameAddr, getAzanaAddr,
                   nullptr, nullptr, true);
  }
}

static void Tick() {
  uintptr_t exeBase = 0;
  uintptr_t rosterBase = 0;
  if (!ResolveRuntimeContext(&exeBase, &rosterBase))
    return;

  const ULONGLONG now = GetTickCount64();

  // 전체 장수는 시작 시 즉시, 이후 30초 간격으로만 동기화한다.
  // 등록무장/세이브 변경을 따라가되 매 프레임 대량 호출하지 않는다.
  if (g_state.lastFullSyncAt == 0 ||
      g_state.lastRosterBase != rosterBase ||
      now - g_state.lastFullSyncAt >= 30000) {
    SyncAllOfficerNames(exeBase, rosterBase);
    g_state.lastFullSyncAt = now;
  }

  // 출산 직후 이름 반영만 2초 간격으로 20개 슬롯에 한정한다.
  if (g_state.lastChildSyncAt == 0 ||
      now - g_state.lastChildSyncAt >= 2000) {
    SyncGeneratedChildren(exeBase, rosterBase);
    g_state.lastChildSyncAt = now;
  }
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
