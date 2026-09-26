#pragma once

#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <cstring>

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

constexpr std::array<uint8_t, 6> kTraitEligibilityExpected = {
    0x81, 0xFE, 0x95, 0x00, 0x00, 0x00};

inline uintptr_t GameBase() {
  return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
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

inline bool ReadBytes(uintptr_t address, void *out, std::size_t size) {
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<const void *>(address),
                           out, size, &read) != FALSE &&
         read == size;
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

} // namespace TraitCompatibilityDiagnosticDetail

inline void MaintainTraitCompatibilityDiagnostics() {
  using namespace TraitCompatibilityDiagnosticDetail;
  TraitCompatibilityDiagnosticState &state =
      GetTraitCompatibilityDiagnosticState();

  ApplyHookState(kTraitEffectQueryOffset, state.effectQuery);
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
