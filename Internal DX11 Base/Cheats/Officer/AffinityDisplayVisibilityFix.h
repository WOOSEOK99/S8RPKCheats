#pragma once

#include "AffinityDisplay.h"

namespace DX11Base {
namespace AffinityDisplayVisibilityFixDetail {

using namespace AffinityDisplayDetail;

inline uintptr_t g_fixedInfoUpdateThunk = 0;

inline bool FixedIsVisible(uintptr_t officer) {
  std::array<uint8_t, 0x18> state{};
  *reinterpret_cast<uint32_t*>(&state[8]) =
      *reinterpret_cast<uint32_t*>(g_base + kVisibilityValue);
  *reinterpret_cast<uintptr_t*>(&state[0x10]) = officer;
  return reinterpret_cast<VisibleFn>(g_base + kVisibleCheck)(
             reinterpret_cast<uintptr_t>(state.data())) != 0;
}

inline uintptr_t __fastcall FixedRosterFmt(uintptr_t officer) {
  if (!FixedIsVisible(officer))
    return reinterpret_cast<uintptr_t>(kQuestion);
  return reinterpret_cast<AffinityFmtFn>(g_base + kAffinityFmt)(officer);
}

inline uintptr_t __fastcall FixedRosterSort(uintptr_t lhs, uintptr_t rhs) {
  const bool visible = FixedIsVisible(rhs);
  const uintptr_t result =
      reinterpret_cast<AffinitySortFn>(g_base + kAffinitySort)(lhs, rhs);
  if (!visible && result)
    *reinterpret_cast<uint32_t*>(result + 4) = 0x7FFFFFFF;
  return result;
}

inline uintptr_t __fastcall FixedInfoUpdateHook(uintptr_t rcx,
                                                uint32_t edx,
                                                uintptr_t r8,
                                                uint32_t r9d) {
  auto original = reinterpret_cast<UiUpdateFn>(g_base + kUiUpdate);
  const uintptr_t result = original(rcx, edx, r8, r9d);
  const uintptr_t r13 = g_infoR13;
  const uintptr_t r14 = g_infoR14;
  if (!r13 || *reinterpret_cast<uint32_t*>(r13 + 0x150) != 0x32)
    return result;

  const uintptr_t list = r13 + 0x140;
  original(list, 0x2F, reinterpret_cast<uintptr_t>(kAffinityTitle), 1);
  original(list, 0x30, FixedRosterFmt(r14), 1);
  return result;
}

inline bool ApplyVisibilityFix() {
  if (!g_fixedInfoUpdateThunk) {
    g_fixedInfoUpdateThunk = MakeCaptureThunk(
        g_base + kInfoUpdateCall,
        reinterpret_cast<uintptr_t>(&g_infoR13), true,
        reinterpret_cast<uintptr_t>(&g_infoR14),
        reinterpret_cast<uintptr_t>(&FixedInfoUpdateHook));
  }
  if (!g_fixedInfoUpdateThunk)
    return false;

  const uintptr_t fmt = reinterpret_cast<uintptr_t>(&FixedRosterFmt);
  const uintptr_t sort = reinterpret_cast<uintptr_t>(&FixedRosterSort);
  if (!WriteMem(g_base + kRosterDesc + 0x18, &fmt, sizeof(fmt)) ||
      !WriteMem(g_base + kRosterDesc + 0x28, &sort, sizeof(sort)) ||
      !WriteCall(g_base + kInfoUpdateCall, g_fixedInfoUpdateThunk)) {
    return false;
  }
  return true;
}

} // namespace AffinityDisplayVisibilityFixDetail

inline bool SetAffinityDisplayWithVisibilityFix(bool enable) {
  if (!SetAffinityDisplay(enable))
    return false;
  if (!enable)
    return true;

  if (!AffinityDisplayVisibilityFixDetail::ApplyVisibilityFix()) {
    SetAffinityDisplay(false);
    AddLog(u8"[상성표시] 가시성 보정 적용 실패 - 원본 상태로 복구");
    return false;
  }

  AddLog(u8"[상성표시] 무장 포인터 가시성 보정 적용 완료");
  return true;
}

} // namespace DX11Base
