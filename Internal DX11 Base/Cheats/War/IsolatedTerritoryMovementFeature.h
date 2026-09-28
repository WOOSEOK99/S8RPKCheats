#pragma once

#ifdef max
#undef max
#endif

#include "IsolatedTerritoryMovement.h"

namespace DX11Base {

inline bool bIsolatedTerritoryMovement = true;

namespace IsolatedTerritoryMovementFeatureDetail {

inline bool PatchBoolConsumer(uintptr_t stub, size_t offset) {
  using namespace IsolatedTerritoryMovementDetail;

  if (!stub)
    return false;

  const uint8_t expected[] = {0x85, 0xC0}; // test eax,eax
  const uint8_t patched[] = {0x84, 0xC0};  // test al,al

  if (ReadEq(stub + offset, patched, sizeof(patched)))
    return true;
  if (!ReadEq(stub + offset, expected, sizeof(expected)))
    return false;

  return WriteBytes(stub + offset, patched, sizeof(patched));
}

inline bool PrepareBoolAbiFix() {
  using namespace IsolatedTerritoryMovementDetail;

  gBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gBase || !PrepareStubs())
    return false;

  // C++ bool helper의 유효 반환은 AL입니다.
  // 실게임에서 검증한 5개 소비 지점만 test al,al 로 교정합니다.
  return PatchBoolConsumer(gHook1Stub, 97) &&
         PatchBoolConsumer(gHook2Stub, 74) &&
         PatchBoolConsumer(gHook3Stub, 94) &&
         PatchBoolConsumer(gHook45Stub, 73) &&
         PatchBoolConsumer(gHook6Stub, 132);
}

} // namespace IsolatedTerritoryMovementFeatureDetail

inline bool SetIsolatedTerritoryMovementFeature(bool enable) {
  if (enable &&
      !IsolatedTerritoryMovementFeatureDetail::PrepareBoolAbiFix()) {
    AddLog(u8"[영토단절] 적용 실패: bool ABI 교정 실패");
    return false;
  }

  return SetIsolatedTerritoryMovement(enable);
}

inline bool IsIsolatedTerritoryMovementFeatureApplied() {
  return IsIsolatedTerritoryMovementApplied();
}

} // namespace DX11Base
