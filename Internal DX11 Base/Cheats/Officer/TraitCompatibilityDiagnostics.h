#pragma once

#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace DX11Base {

struct TraitCompatibilityDiagnosticState {
  bool effectQuery = true;
  bool officerTraitQuery = true;
  bool messageBridge = true;
  bool dataBridge = true;
  bool monthlyUpdate = true;
  bool transferEvent = true;
  bool eligibilityA = true;
  bool eligibilityB = true;
};

inline TraitCompatibilityDiagnosticState &GetTraitCompatibilityDiagnosticState() {
  static TraitCompatibilityDiagnosticState state{};
  return state;
}

namespace TraitCompatibilityDiagnosticDetail {

constexpr uintptr_t kTraitEffectQueryOffset = 0x17AAD60;
constexpr uintptr_t kOfficerTraitQueryOffset = 0x170C080;
constexpr uintptr_t kTraitMessageSetterOffset = 0x13F91E0;
constexpr uintptr_t kTraitDataGetterOffset = 0x16E63F0;
constexpr uintptr_t kMonthlyTraitUpdateOffset = 0x1C79C90;
constexpr uintptr_t kTransferTraitEventOffset = 0x18A5E30;
constexpr uintptr_t kTraitEligibilityPatchAOffset = 0x17C03E7;
constexpr uintptr_t kTraitEligibilityPatchBOffset = 0x17C042F;
constexpr uintptr_t kHeldTraitPointerOffset = 0x88;
constexpr uintptr_t kTraitIdOffset = 0x08;

constexpr std::array<uint8_t, 6> kTraitEligibilityExpected = {
    0x81, 0xFE, 0x95, 0x00, 0x00, 0x00};

inline uintptr_t GameBase() {
  return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

inline bool ReadBytes(uintptr_t address, void *out, std::size_t size) {
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<const void *>(address),
                           out, size, &read) != FALSE &&
         read == size;
}

inline void ApplyHookState(uintptr_t offset, bool enable) {
  const uintptr_t gameBase = GameBase();
  if (!gameBase)
    return;

  void *target = reinterpret_cast<void *>(gameBase + offset);
  if (enable)
    MH_EnableHook(target);
  else
    MH_DisableHook(target);
}

inline bool WriteExecutableByte(uintptr_t address, uint8_t value) {
  DWORD oldProtect = 0;
  void *dst = reinterpret_cast<void *>(address);
  if (!VirtualProtect(dst, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  *reinterpret_cast<volatile uint8_t *>(address) = value;

  DWORD ignored = 0;
  VirtualProtect(dst, 1, oldProtect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), dst, 1);
  return true;
}

inline void ApplyEligibilityState(uintptr_t offset, bool enable) {
  const uintptr_t gameBase = GameBase();
  if (!gameBase)
    return;

  const uintptr_t address = gameBase + offset;
  std::array<uint8_t, 6> bytes{};
  if (!ReadBytes(address, bytes.data(), bytes.size()))
    return;

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 2)
      continue;
    if (bytes[i] != kTraitEligibilityExpected[i])
      return;
  }

  const uint8_t wanted = enable ? 0x01 : 0x95;
  if (bytes[2] == wanted)
    return;
  if (bytes[2] != 0x01 && bytes[2] != 0x95)
    return;

  WriteExecutableByte(address + 2, wanted);
}

inline uintptr_t ResolveJumpTarget(uintptr_t address) {
  uint8_t bytes[16]{};
  if (!ReadBytes(address, bytes, sizeof(bytes)))
    return 0;

  if (bytes[0] == 0xE9) {
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, sizeof(rel));
    return address + 5 + static_cast<int64_t>(rel);
  }

  if (bytes[0] == 0xFF && bytes[1] == 0x25) {
    int32_t disp = 0;
    std::memcpy(&disp, bytes + 2, sizeof(disp));
    const uintptr_t pointerAddress =
        address + 6 + static_cast<int64_t>(disp);
    uintptr_t target = 0;
    if (ReadBytes(pointerAddress, &target, sizeof(target)))
      return target;
  }

  return 0;
}

inline uintptr_t ResolveEffectDetourAddress() {
  const uintptr_t gameBase = GameBase();
  if (!gameBase)
    return 0;

  uintptr_t current = gameBase + kTraitEffectQueryOffset;
  for (int i = 0; i < 3; ++i) {
    const uintptr_t next = ResolveJumpTarget(current);
    if (!next || next == current)
      break;
    current = next;
  }

  return current == gameBase + kTraitEffectQueryOffset ? 0 : current;
}

using EffectDetourFn = bool(__fastcall *)(void *, uint16_t);
inline EffectDetourFn g_effectDetourOriginal = nullptr;
inline uintptr_t g_effectDetourAddress = 0;
inline bool g_effectTraceHookInstalled = false;
inline bool g_effectTraceFailureLogged = false;

inline void ReadHeldTraitIds(void *officer, std::array<uint16_t, 3> &held) {
  held = {};
  if (!officer)
    return;

  const uintptr_t officerBase = reinterpret_cast<uintptr_t>(officer);
  for (std::size_t slot = 0; slot < held.size(); ++slot) {
    uintptr_t record = 0;
    if (!ReadBytes(officerBase + kHeldTraitPointerOffset +
                       slot * sizeof(uintptr_t),
                   &record, sizeof(record)) ||
        record < 0x10000) {
      continue;
    }

    ReadBytes(record + kTraitIdOffset, &held[slot], sizeof(held[slot]));
  }
}

inline bool __fastcall TraceEffectDetour(
    void *officer, uint16_t requestedTraitId) {
  EffectDetourFn original = g_effectDetourOriginal;
  if (!original)
    return false;

  const bool result = original(officer, requestedTraitId);
  if (!result || (requestedTraitId != 11 && requestedTraitId != 26))
    return result;

  std::array<uint16_t, 3> held{};
  ReadHeldTraitIds(officer, held);

  const uintptr_t gameBase = GameBase();
  const uintptr_t returnAddress =
      reinterpret_cast<uintptr_t>(_ReturnAddress());
  const uintptr_t callerRva =
      gameBase && returnAddress >= gameBase ? returnAddress - gameBase : 0;

  thread_local unsigned logCount = 0;
  if (logCount < 80) {
    AddLog(u8"[기재호환추적] req=%u result=1 held=%u,%u,%u officer=%p caller=+%llX",
           static_cast<unsigned>(requestedTraitId),
           static_cast<unsigned>(held[0]),
           static_cast<unsigned>(held[1]),
           static_cast<unsigned>(held[2]),
           officer,
           static_cast<unsigned long long>(callerRva));
    ++logCount;
  }

  return result;
}

inline void EnsureEffectTraceHook() {
  if (g_effectTraceHookInstalled)
    return;

  const uintptr_t detour = ResolveEffectDetourAddress();
  if (!detour)
    return;

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
    return;

  LPVOID original = nullptr;
  const MH_STATUS createStatus =
      MH_CreateHook(reinterpret_cast<LPVOID>(detour),
                    reinterpret_cast<LPVOID>(&TraceEffectDetour),
                    &original);
  if (createStatus != MH_OK) {
    if (!g_effectTraceFailureLogged) {
      AddLog(u8"[기재호환추적] 메인 효과 판정 추적 훅 생성 실패: %s / detour=%p",
             MH_StatusToString(createStatus),
             reinterpret_cast<void *>(detour));
      g_effectTraceFailureLogged = true;
    }
    return;
  }

  g_effectDetourOriginal = reinterpret_cast<EffectDetourFn>(original);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(detour));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(detour));
    g_effectDetourOriginal = nullptr;
    return;
  }

  g_effectDetourAddress = detour;
  g_effectTraceHookInstalled = true;
  g_effectTraceFailureLogged = false;
  AddLog(u8"[기재호환추적] 메인 효과 판정 추적 훅 설치 완료: detour=%p",
         reinterpret_cast<void *>(detour));
}

} // namespace TraitCompatibilityDiagnosticDetail

inline void MaintainTraitCompatibilityDiagnostics() {
  using namespace TraitCompatibilityDiagnosticDetail;
  TraitCompatibilityDiagnosticState &state =
      GetTraitCompatibilityDiagnosticState();

  ApplyHookState(kTraitEffectQueryOffset, state.effectQuery);
  if (state.effectQuery)
    EnsureEffectTraceHook();
  ApplyHookState(kOfficerTraitQueryOffset, state.officerTraitQuery);
  ApplyHookState(kTraitMessageSetterOffset, state.messageBridge);
  ApplyHookState(kTraitDataGetterOffset, state.dataBridge);
  ApplyHookState(kMonthlyTraitUpdateOffset, state.monthlyUpdate);
  ApplyHookState(kTransferTraitEventOffset, state.transferEvent);
  ApplyEligibilityState(kTraitEligibilityPatchAOffset, state.eligibilityA);
  ApplyEligibilityState(kTraitEligibilityPatchBOffset, state.eligibilityB);
}

inline void LogTraitCompatibilityDiagnosticChange(
    const char *name, bool enabled) {
  AddLog(u8"[기재호환진단] %s: %s", name, enabled ? "ON" : "OFF");
  MaintainTraitCompatibilityDiagnostics();
}

inline void ResetTraitCompatibilityDiagnostics() {
  GetTraitCompatibilityDiagnosticState() = TraitCompatibilityDiagnosticState{};
  MaintainTraitCompatibilityDiagnostics();
  AddLog(u8"[기재호환진단] 전체 기능 ON 복원");
}

} // namespace DX11Base
