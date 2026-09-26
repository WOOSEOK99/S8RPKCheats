#pragma once

#include "../../pch.h"
#include "../../showlog.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace DX11Base {
namespace ChildNameDiagnostics {

// SAN8RPK.pdb에서 확인된 PersonData 원본 이름 함수.
// 1) const wchar_t* GetName() const               -> +0x1713DB0
// 2) void GetName(wchar_t* out) const             -> +0x16F6850
// 두 번째 오버로드가 게임 측에서 완성 이름을 출력 버퍼에 조합하는 함수이므로
// 기본 이름 표시는 이 함수를 우선 사용한다.
static constexpr uintptr_t kPersonDataGetNameRva = 0x1713DB0;
static constexpr uintptr_t kPersonDataGetNameToBufferRva = 0x16F6850;
static constexpr uintptr_t kPersonDataGetAzanaRva = 0x1712A80;
static constexpr uintptr_t kOfficerStride = 0x3D0;
static constexpr int kOfficerCount = 5102;
static constexpr uint16_t kGeneratedFirstId = 4001;
static constexpr uint16_t kGeneratedLastId = 4020;

using NativePersonTextGetter = const wchar_t* (__fastcall*)(const void* self);
using NativePersonNameWriter = void (__fastcall*)(const void* self, wchar_t* out);

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

static bool CallNativeNameWriterSafe(uintptr_t fnAddr,
                                     uintptr_t officer,
                                     wchar_t* out) {
  if (!out)
    return false;
  out[0] = L'\0';
  if (fnAddr <= 0x10000 || officer <= 0x10000)
    return false;

  const auto fn = reinterpret_cast<NativePersonNameWriter>(fnAddr);
  __try {
    fn(reinterpret_cast<const void*>(officer), out);
    return out[0] != L'\0';
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out[0] = L'\0';
    return false;
  }
}

static bool WideBufferToUtf8(const wchar_t* wide, std::string& outText) {
  outText.clear();
  if (!wide || wide[0] == L'\0')
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

  return WideBufferToUtf8(wide, outText);
}

static bool ResolveNativeFullNameUtf8(uintptr_t writerAddr,
                                      uintptr_t fallbackGetterAddr,
                                      uintptr_t officer,
                                      std::string& outName) {
  outName.clear();

  // 게임의 완성 이름 조합용 출력 버퍼 오버로드를 최우선으로 사용한다.
  wchar_t wide[128]{};
  if (CallNativeNameWriterSafe(writerAddr, officer, wide) &&
      WideBufferToUtf8(wide, outName)) {
    return true;
  }

  // 특수 상황에서 writer 호출이 실패할 경우에만 기존 const wchar_t* getter 사용.
  return ResolveNativeTextUtf8(fallbackGetterAddr, officer, outName);
}

struct State {
  ULONGLONG lastFullSyncAt = 0;
  ULONGLONG lastChildSyncAt = 0;
  uintptr_t lastRosterBase = 0;
  std::unordered_map<int, std::string> lastNativeNames;
  std::array<uintptr_t, 20> generatedOfficers{};
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
  g_state.generatedOfficers.fill(0);
}

static bool SyncOneOfficer(uintptr_t officer,
                           uint16_t expectedId,
                           uintptr_t getNameWriterAddr,
                           uintptr_t getNameFallbackAddr,
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
  if (!ResolveNativeFullNameUtf8(getNameWriterAddr, getNameFallbackAddr,
                                 officer, name)) {
    return false;
  }

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

  const uintptr_t getNameWriterAddr = exeBase + kPersonDataGetNameToBufferRva;
  const uintptr_t getNameFallbackAddr = exeBase + kPersonDataGetNameRva;
  const uintptr_t getAzanaAddr = exeBase + kPersonDataGetAzanaRva;
  int nativeCount = 0;
  int overrideCount = 0;

  g_state.generatedOfficers.fill(0);

  for (int i = 0; i < kOfficerCount; ++i) {
    const uintptr_t officer = rosterBase + (uintptr_t)i * kOfficerStride;

    uint16_t id = 0;
    if (SafeRead16Local(officer + 0x08, &id) &&
        id >= kGeneratedFirstId && id <= kGeneratedLastId) {
      g_state.generatedOfficers[(size_t)(id - kGeneratedFirstId)] = officer;
    }

    SyncOneOfficer(officer, 0,
                   getNameWriterAddr, getNameFallbackAddr, getAzanaAddr,
                   &nativeCount, &overrideCount, false);
  }

  if (g_state.lastRosterBase != rosterBase || g_state.lastFullSyncAt == 0) {
    AddLog(u8"[무장이름] 원본 완성이름/자 동기화 완료: native=%d override=%d / GetName(out) +0x%llX / GetAzana +0x%llX",
           nativeCount, overrideCount,
           (unsigned long long)kPersonDataGetNameToBufferRva,
           (unsigned long long)kPersonDataGetAzanaRva);
  }
  g_state.lastRosterBase = rosterBase;
}

static void SyncGeneratedChildren(uintptr_t exeBase, uintptr_t rosterBase) {
  (void)rosterBase;
  const uintptr_t getNameWriterAddr = exeBase + kPersonDataGetNameToBufferRva;
  const uintptr_t getNameFallbackAddr = exeBase + kPersonDataGetNameRva;
  const uintptr_t getAzanaAddr = exeBase + kPersonDataGetAzanaRva;

  // 전체 동기화 때 실제 ID를 기준으로 찾아 둔 생성 자녀 20개 예약 슬롯만 갱신한다.
  for (uint16_t id = kGeneratedFirstId; id <= kGeneratedLastId; ++id) {
    const size_t index = (size_t)(id - kGeneratedFirstId);
    const uintptr_t officer = g_state.generatedOfficers[index];
    if (!officer)
      continue;

    uint16_t birth = 0;
    if (!SafeRead16Local(officer + 0x34, &birth) || birth == 0)
      continue;

    SyncOneOfficer(officer, id,
                   getNameWriterAddr, getNameFallbackAddr, getAzanaAddr,
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

  // 출산 직후 이름 반영은 이미 찾아 둔 20개 예약 슬롯만 2초 간격으로 갱신한다.
  if (g_state.lastChildSyncAt == 0 ||
      now - g_state.lastChildSyncAt >= 2000) {
    SyncGeneratedChildren(exeBase, rosterBase);
    g_state.lastChildSyncAt = now;
  }
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
