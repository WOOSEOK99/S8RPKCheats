#pragma once

#include "../../pch.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <array>
#include <cstring>

namespace DX11Base {
namespace AffinityDisplayDetail {

inline constexpr uintptr_t kRosterArrayPtr1 = 0x362AAA8;
inline constexpr uintptr_t kRosterArrayPtr2 = 0x3632C20;
inline constexpr uintptr_t kRosterArrayPtr3 = 0x36331A8;
inline constexpr uintptr_t kRosterArray1 = 0x3A6C348;
inline constexpr uintptr_t kRosterArray2 = 0x3B36800;
inline constexpr uintptr_t kRosterArray3 = 0x3B3F248;
inline constexpr uintptr_t kRosterArray4 = 0x3B1BFC8;
inline constexpr uintptr_t kRosterDesc = 0x2C72E70;
inline constexpr uintptr_t kInfoInitCall = 0x1CD2B73;
inline constexpr uintptr_t kInfoUpdateCall = 0x1CD0B78;
inline constexpr uintptr_t kGalleryInitCall = 0x137F69A;
inline constexpr uintptr_t kGalleryUpdateCall = 0x137E288;

inline constexpr uintptr_t kUiInit = 0x1D13E60;
inline constexpr uintptr_t kUiUpdate = 0x1D17190;
inline constexpr uintptr_t kAffinityFmt = 0x1AD9E50;
inline constexpr uintptr_t kAffinitySort = 0x1AD9E10;
inline constexpr uintptr_t kVisibleCheck = 0x1B0D5B0;
inline constexpr uintptr_t kVisibilityValue = 0x2C701B8;
inline constexpr uintptr_t kUiRootGetter = 0x14ED0;
inline constexpr uintptr_t kLineInit = 0x1EDE250;
inline constexpr uintptr_t kLineVtable = 0x25F7B18;
inline constexpr uintptr_t kLineLayout = 0x16977C0;
inline constexpr uintptr_t kAttachLine = 0x1D16AA0;

inline bool g_applied = false;
inline uintptr_t g_base = 0;
inline uintptr_t g_infoInitThunk = 0;
inline uintptr_t g_infoUpdateThunk = 0;
inline uintptr_t g_galleryInitThunk = 0;
inline uintptr_t g_galleryUpdateThunk = 0;
inline uintptr_t g_infoR13 = 0;
inline uintptr_t g_infoR14 = 0;
inline uintptr_t g_galleryRdi = 0;
inline uintptr_t g_galleryR14 = 0;

inline constexpr uint8_t kInfoInitOrig[5] = {0xE8, 0xE8, 0x12, 0x04, 0x00};
inline constexpr uint8_t kInfoUpdateOrig[5] = {0xE8, 0x13, 0x66, 0x04, 0x00};
inline constexpr uint8_t kGalleryInitOrig[5] = {0xE8, 0xC1, 0x47, 0x99, 0x00};
inline constexpr uint8_t kGalleryUpdateOrig[5] = {0xE8, 0x03, 0x8F, 0x99, 0x00};

inline constexpr uint32_t kArray1Off[14] = {
    0x00, 0x0C, 0x0D, 0xBF, 0x0E, 0x11, 0x12, 0x13,
    0x14, 0x16, 0x17, 0xE7, 0xFFFFFFFF, 0xFFFFFFFF};
inline constexpr uint32_t kArray1On[14] = {
    0x00, 0xA8, 0x0C, 0x0D, 0xBF, 0x0E, 0x11, 0x12,
    0x13, 0x14, 0x16, 0x17, 0xE7, 0xFFFFFFFF};
inline constexpr uint32_t kArray4Off[13] = {
    0xCE, 0x0C, 0x0D, 0xBF, 0x0E, 0x11, 0x12,
    0x13, 0x14, 0x16, 0x17, 0xFFFFFFFF, 0xFFFFFFFF};
inline constexpr uint32_t kArray4On[13] = {
    0xCE, 0xA8, 0x0C, 0x0D, 0xBF, 0x0E, 0x11,
    0x12, 0x13, 0x14, 0x16, 0x17, 0xFFFFFFFF};
inline constexpr uint32_t kArray23Off[13] = {
    0x00, 0x23, 0x34, 0x93, 0x9A, 0xD1, 0xD2,
    0xD3, 0x14, 0x8B, 0x8C, 0xFFFFFFFF, 0xFFFFFFFF};
inline constexpr uint32_t kArray23On[13] = {
    0x00, 0xA8, 0x23, 0x34, 0x93, 0x9A, 0xD1,
    0xD2, 0xD3, 0x14, 0x8B, 0x8C, 0xFFFFFFFF};

inline constexpr wchar_t kAffinityTitle[] = L"상성";
inline constexpr wchar_t kQuestion[] = L"?";

inline bool ReadEq(uintptr_t addr, const void* expected, size_t size) {
  return addr && expected &&
         std::memcmp(reinterpret_cast<const void*>(addr), expected, size) == 0;
}

inline bool WriteMem(uintptr_t addr, const void* data, size_t size) {
  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(addr), size,
                      PAGE_EXECUTE_READWRITE, &oldProtect)) {
    return false;
  }
  std::memcpy(reinterpret_cast<void*>(addr), data, size);
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(addr), size);
  DWORD dummy = 0;
  VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &dummy);
  return true;
}

inline bool WriteCall(uintptr_t callsite, uintptr_t target) {
  const int64_t rel64 = static_cast<int64_t>(target) -
                        static_cast<int64_t>(callsite + 5);
  if (rel64 < INT32_MIN || rel64 > INT32_MAX)
    return false;
  uint8_t patch[5] = {0xE8, 0, 0, 0, 0};
  *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(rel64);
  return WriteMem(callsite, patch, sizeof(patch));
}

inline uintptr_t MakeJumpThunk(uintptr_t nearAddr, uintptr_t target) {
  const uintptr_t thunk = AllocNear(nearAddr, 64);
  if (!thunk)
    return 0;
  uint8_t code[14] = {0xFF, 0x25, 0, 0, 0, 0};
  *reinterpret_cast<uintptr_t*>(&code[6]) = target;
  std::memcpy(reinterpret_cast<void*>(thunk), code, sizeof(code));
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(thunk),
                        sizeof(code));
  return thunk;
}

inline uintptr_t MakeCaptureThunk(uintptr_t nearAddr,
                                  uintptr_t regStore1,
                                  bool firstIsR13,
                                  uintptr_t regStore2,
                                  uintptr_t target) {
  const uintptr_t thunk = AllocNear(nearAddr, 128);
  if (!thunk)
    return 0;

  uint8_t* p = reinterpret_cast<uint8_t*>(thunk);
  size_t i = 0;
  auto movRaxImm = [&](uintptr_t v) {
    p[i++] = 0x48;
    p[i++] = 0xB8;
    *reinterpret_cast<uintptr_t*>(&p[i]) = v;
    i += 8;
  };

  movRaxImm(regStore1);
  if (firstIsR13) {
    p[i++] = 0x4C; p[i++] = 0x89; p[i++] = 0x28; // mov [rax],r13
  } else {
    p[i++] = 0x48; p[i++] = 0x89; p[i++] = 0x38; // mov [rax],rdi
  }

  movRaxImm(regStore2);
  p[i++] = 0x4C; p[i++] = 0x89; p[i++] = 0x30;   // mov [rax],r14

  movRaxImm(target);
  p[i++] = 0xFF;
  p[i++] = 0xE0;                                  // jmp rax

  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(thunk), i);
  return thunk;
}

using UiInitFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint32_t, uintptr_t);
using UiUpdateFn = uintptr_t(__fastcall*)(uintptr_t, uint32_t, uintptr_t, uint32_t);
using AffinityFmtFn = uintptr_t(__fastcall*)(uintptr_t);
using AffinitySortFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
using VisibleFn = uint8_t(__fastcall*)(uintptr_t);
using NoArgFn = uintptr_t(__fastcall*)();
using OneArgFn = void(__fastcall*)(uintptr_t);
using VCall3Fn = uintptr_t(__fastcall*)(uintptr_t, uint32_t, uintptr_t);
using FiveArgFn = uintptr_t(__fastcall*)(uintptr_t, uint32_t, uint32_t, uint32_t, uintptr_t);
using AttachFn = uintptr_t(__fastcall*)(uintptr_t, uint32_t, uintptr_t, uint32_t, uintptr_t);

inline bool IsVisible(uintptr_t officer) {
  (void)officer;
  std::array<uint8_t, 0x18> state{};
  *reinterpret_cast<uint32_t*>(&state[8]) =
      *reinterpret_cast<uint32_t*>(g_base + kVisibilityValue);
  return reinterpret_cast<VisibleFn>(g_base + kVisibleCheck)(
             reinterpret_cast<uintptr_t>(state.data())) != 0;
}

inline uintptr_t __fastcall RosterFmt(uintptr_t officer) {
  if (!IsVisible(officer))
    return reinterpret_cast<uintptr_t>(kQuestion);
  return reinterpret_cast<AffinityFmtFn>(g_base + kAffinityFmt)(officer);
}

inline uintptr_t __fastcall RosterSort(uintptr_t lhs, uintptr_t rhs) {
  const bool visible = IsVisible(rhs);
  const uintptr_t result =
      reinterpret_cast<AffinitySortFn>(g_base + kAffinitySort)(lhs, rhs);
  if (!visible && result)
    *reinterpret_cast<uint32_t*>(result + 4) = 0x7FFFFFFF;
  return result;
}

inline uintptr_t __fastcall InfoInitHook(uintptr_t rcx,
                                         uintptr_t rdx,
                                         uint32_t r8d,
                                         uintptr_t r9) {
  auto original = reinterpret_cast<UiInitFn>(g_base + kUiInit);
  if (r8d != 0x2F)
    return original(rcx, rdx, r8d, r9);

  std::array<uint8_t, 0x13E0> buf{};
  std::memcpy(buf.data(), reinterpret_cast<const void*>(rdx), 0x11A0);
  std::memcpy(buf.data() + 0x11A0,
              reinterpret_cast<const void*>(rdx + 0xC60), 0xC0);
  *reinterpret_cast<uint32_t*>(buf.data() + 0x11A8) = 0xA3;
  *reinterpret_cast<uint32_t*>(buf.data() + 0x1208) = 0xA3;
  std::memcpy(buf.data() + 0x1260,
              reinterpret_cast<const void*>(rdx + 0xC00), 0x60);
  *reinterpret_cast<uint32_t*>(buf.data() + 0x1268) = 0x9B;

  const uintptr_t result =
      original(rcx, reinterpret_cast<uintptr_t>(buf.data()), 0x32, r9);

  const uintptr_t root = reinterpret_cast<NoArgFn>(g_base + kUiRootGetter)();
  if (!root)
    return result;
  const uintptr_t owner = *reinterpret_cast<uintptr_t*>(root + 0x118);
  if (!owner)
    return result;

  struct Temp {
    uint32_t id;
    uint32_t pad;
    uintptr_t ptr;
  } temp{0x35, 0, 0};

  const uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(owner);
  if (!vtbl)
    return result;
  auto createLine =
      reinterpret_cast<VCall3Fn>(*reinterpret_cast<uintptr_t*>(vtbl + 0x28));
  const uintptr_t line =
      createLine(owner, 0x1A0, reinterpret_cast<uintptr_t>(&temp));
  if (!line)
    return result;

  reinterpret_cast<OneArgFn>(g_base + kLineInit)(line);
  *reinterpret_cast<uintptr_t*>(line) = g_base + kLineVtable;
  reinterpret_cast<FiveArgFn>(g_base + kLineLayout)(line, 0x1EC, 0x9B, 0x10C, r9);
  reinterpret_cast<AttachFn>(g_base + kAttachLine)(rcx, 0x31, line, 0x14, 1);
  return result;
}

inline uintptr_t __fastcall InfoUpdateHook(uintptr_t rcx,
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
  original(list, 0x30, RosterFmt(r14), 1);
  return result;
}

inline uintptr_t __fastcall GalleryInitHook(uintptr_t rcx,
                                            uintptr_t rdx,
                                            uint32_t r8d,
                                            uintptr_t r9) {
  auto original = reinterpret_cast<UiInitFn>(g_base + kUiInit);
  if (r8d != 0x1E)
    return original(rcx, rdx, r8d, r9);

  std::array<uint8_t, 0xC20> buf{};
  std::memcpy(buf.data(), reinterpret_cast<const void*>(rdx), 0xB40);
  std::memcpy(buf.data() + 0xB40,
              reinterpret_cast<const void*>(rdx + 0x660), 0x60);
  std::memcpy(buf.data() + 0xBA0,
              reinterpret_cast<const void*>(rdx + 0x480), 0x60);
  *reinterpret_cast<uint32_t*>(buf.data() + 0xB44) = 0x5BC;
  *reinterpret_cast<uint32_t*>(buf.data() + 0xB4C) = 0x64;
  *reinterpret_cast<uint32_t*>(buf.data() + 0xBA4) = 0x62C;
  *reinterpret_cast<uint32_t*>(buf.data() + 0xBA8) =
      *reinterpret_cast<uint32_t*>(buf.data() + 0xB48);
  *reinterpret_cast<uint32_t*>(buf.data() + 0xBAC) = 0x64;
  return original(rcx, reinterpret_cast<uintptr_t>(buf.data()), 0x20, r9);
}

inline uintptr_t __fastcall GalleryUpdateHook(uintptr_t rcx,
                                              uint32_t edx,
                                              uintptr_t r8,
                                              uint32_t r9d) {
  auto original = reinterpret_cast<UiUpdateFn>(g_base + kUiUpdate);
  const uintptr_t result = original(rcx, edx, r8, r9d);
  const uintptr_t rdi = g_galleryRdi;
  const uintptr_t r14 = g_galleryR14;
  if (!rdi || *reinterpret_cast<uint32_t*>(rdi + 0x10) != 0x20)
    return result;

  original(rdi, 0x1E, reinterpret_cast<uintptr_t>(kAffinityTitle), 1);
  const uintptr_t value =
      reinterpret_cast<AffinityFmtFn>(g_base + kAffinityFmt)(r14);
  original(rdi, 0x1F, value, 1);
  return result;
}

inline bool ValidateOffState() {
  const uintptr_t b = g_base;
  if (!b)
    return false;

  if (*reinterpret_cast<uintptr_t*>(b + kRosterArrayPtr1) != b + kRosterArray1 ||
      *reinterpret_cast<uintptr_t*>(b + kRosterArrayPtr2) != b + kRosterArray2 ||
      *reinterpret_cast<uintptr_t*>(b + kRosterArrayPtr3) != b + kRosterArray3) {
    return false;
  }

  if (!ReadEq(b + kInfoInitCall, kInfoInitOrig, sizeof(kInfoInitOrig)) ||
      !ReadEq(b + kInfoUpdateCall, kInfoUpdateOrig, sizeof(kInfoUpdateOrig)) ||
      !ReadEq(b + kGalleryInitCall, kGalleryInitOrig, sizeof(kGalleryInitOrig)) ||
      !ReadEq(b + kGalleryUpdateCall, kGalleryUpdateOrig,
              sizeof(kGalleryUpdateOrig))) {
    return false;
  }

  if (*reinterpret_cast<uint32_t*>(b + kRosterDesc) != 0xA8 ||
      *reinterpret_cast<uintptr_t*>(b + kRosterDesc + 8) != b + 0x2099DD8 ||
      *reinterpret_cast<uint32_t*>(b + kRosterDesc + 0x10) != 0x68 ||
      *reinterpret_cast<uint32_t*>(b + kRosterDesc + 0x14) != 1 ||
      *reinterpret_cast<uintptr_t*>(b + kRosterDesc + 0x18) != b + kAffinityFmt ||
      *reinterpret_cast<uintptr_t*>(b + kRosterDesc + 0x28) != b + kAffinitySort) {
    return false;
  }

  const uint8_t descTailExpected[8] = {0xFF, 0xFF, 0x00, 0x00,
                                       0x01, 0x00, 0x00, 0x00};
  if (!ReadEq(b + kRosterDesc + 0x48, descTailExpected,
              sizeof(descTailExpected))) {
    return false;
  }

  return ReadEq(b + kRosterArray1, kArray1Off, sizeof(kArray1Off)) &&
         ReadEq(b + kRosterArray4, kArray4Off, sizeof(kArray4Off)) &&
         ReadEq(b + kRosterArray2, kArray23Off, sizeof(kArray23Off)) &&
         ReadEq(b + kRosterArray3, kArray23Off, sizeof(kArray23Off));
}

inline bool PatchRoster(bool enable) {
  const uintptr_t b = g_base;
  if (enable) {
    const uintptr_t title = reinterpret_cast<uintptr_t>(kAffinityTitle);
    const uint32_t width = 0x4C;
    const uint32_t mode = 2;
    const uintptr_t fmt = reinterpret_cast<uintptr_t>(&RosterFmt);
    const uintptr_t sort = reinterpret_cast<uintptr_t>(&RosterSort);

    if (!WriteMem(b + kRosterDesc + 8, &title, sizeof(title)) ||
        !WriteMem(b + kRosterDesc + 0x10, &width, sizeof(width)) ||
        !WriteMem(b + kRosterDesc + 0x14, &mode, sizeof(mode)) ||
        !WriteMem(b + kRosterDesc + 0x18, &fmt, sizeof(fmt)) ||
        !WriteMem(b + kRosterDesc + 0x28, &sort, sizeof(sort))) {
      return false;
    }

    return WriteMem(b + kRosterArray1, kArray1On, sizeof(kArray1On)) &&
           WriteMem(b + kRosterArray4, kArray4On, sizeof(kArray4On)) &&
           WriteMem(b + kRosterArray2, kArray23On, sizeof(kArray23On)) &&
           WriteMem(b + kRosterArray3, kArray23On, sizeof(kArray23On));
  }

  const uintptr_t title = b + 0x2099DD8;
  const uint32_t width = 0x68;
  const uint32_t mode = 1;
  const uintptr_t fmt = b + kAffinityFmt;
  const uintptr_t sort = b + kAffinitySort;

  if (!WriteMem(b + kRosterArray3, kArray23Off, sizeof(kArray23Off)) ||
      !WriteMem(b + kRosterArray2, kArray23Off, sizeof(kArray23Off)) ||
      !WriteMem(b + kRosterArray4, kArray4Off, sizeof(kArray4Off)) ||
      !WriteMem(b + kRosterArray1, kArray1Off, sizeof(kArray1Off))) {
    return false;
  }

  return WriteMem(b + kRosterDesc + 8, &title, sizeof(title)) &&
         WriteMem(b + kRosterDesc + 0x10, &width, sizeof(width)) &&
         WriteMem(b + kRosterDesc + 0x14, &mode, sizeof(mode)) &&
         WriteMem(b + kRosterDesc + 0x18, &fmt, sizeof(fmt)) &&
         WriteMem(b + kRosterDesc + 0x28, &sort, sizeof(sort));
}

inline bool InstallThunks() {
  if (!g_infoInitThunk) {
    g_infoInitThunk = MakeJumpThunk(
        g_base + kInfoInitCall, reinterpret_cast<uintptr_t>(&InfoInitHook));
  }
  if (!g_infoUpdateThunk) {
    g_infoUpdateThunk = MakeCaptureThunk(
        g_base + kInfoUpdateCall,
        reinterpret_cast<uintptr_t>(&g_infoR13), true,
        reinterpret_cast<uintptr_t>(&g_infoR14),
        reinterpret_cast<uintptr_t>(&InfoUpdateHook));
  }
  if (!g_galleryInitThunk) {
    g_galleryInitThunk = MakeJumpThunk(
        g_base + kGalleryInitCall, reinterpret_cast<uintptr_t>(&GalleryInitHook));
  }
  if (!g_galleryUpdateThunk) {
    g_galleryUpdateThunk = MakeCaptureThunk(
        g_base + kGalleryUpdateCall,
        reinterpret_cast<uintptr_t>(&g_galleryRdi), false,
        reinterpret_cast<uintptr_t>(&g_galleryR14),
        reinterpret_cast<uintptr_t>(&GalleryUpdateHook));
  }
  return g_infoInitThunk && g_infoUpdateThunk &&
         g_galleryInitThunk && g_galleryUpdateThunk;
}

} // namespace AffinityDisplayDetail

inline bool bAffinityDisplay = false;

inline bool SetAffinityDisplay(bool enable) {
  using namespace AffinityDisplayDetail;

  if (enable == g_applied)
    return true;

  g_base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
  if (!g_base)
    return false;

  if (enable) {
    if (!ValidateOffState()) {
      AddLog(u8"[상성표시] 원본 UI/관계 탭 상태가 예상과 달라 적용하지 않았습니다.");
      return false;
    }
    if (!InstallThunks()) {
      AddLog(u8"[상성표시] near thunk 할당 실패");
      return false;
    }
    if (!PatchRoster(true)) {
      PatchRoster(false);
      AddLog(u8"[상성표시] 목록 descriptor 적용 실패");
      return false;
    }

    if (!WriteCall(g_base + kInfoInitCall, g_infoInitThunk) ||
        !WriteCall(g_base + kInfoUpdateCall, g_infoUpdateThunk) ||
        !WriteCall(g_base + kGalleryInitCall, g_galleryInitThunk) ||
        !WriteCall(g_base + kGalleryUpdateCall, g_galleryUpdateThunk)) {
      WriteMem(g_base + kInfoInitCall, kInfoInitOrig, sizeof(kInfoInitOrig));
      WriteMem(g_base + kInfoUpdateCall, kInfoUpdateOrig, sizeof(kInfoUpdateOrig));
      WriteMem(g_base + kGalleryInitCall, kGalleryInitOrig, sizeof(kGalleryInitOrig));
      WriteMem(g_base + kGalleryUpdateCall, kGalleryUpdateOrig,
               sizeof(kGalleryUpdateOrig));
      PatchRoster(false);
      AddLog(u8"[상성표시] UI callsite 적용 실패 - 전체 복구");
      return false;
    }

    g_applied = true;
    AddLog(u8"[상성표시] CT 91930 포팅 적용 완료");
    return true;
  }

  bool ok = true;
  ok = WriteMem(g_base + kGalleryUpdateCall, kGalleryUpdateOrig,
                sizeof(kGalleryUpdateOrig)) && ok;
  ok = WriteMem(g_base + kGalleryInitCall, kGalleryInitOrig,
                sizeof(kGalleryInitOrig)) && ok;
  ok = WriteMem(g_base + kInfoUpdateCall, kInfoUpdateOrig,
                sizeof(kInfoUpdateOrig)) && ok;
  ok = WriteMem(g_base + kInfoInitCall, kInfoInitOrig,
                sizeof(kInfoInitOrig)) && ok;
  ok = PatchRoster(false) && ok;

  if (ok) {
    g_applied = false;
    g_infoR13 = 0;
    g_infoR14 = 0;
    g_galleryRdi = 0;
    g_galleryR14 = 0;
    AddLog(u8"[상성표시] 원본 UI 구성 복구 완료");
  }
  return ok;
}

inline bool IsAffinityDisplayApplied() {
  return AffinityDisplayDetail::g_applied;
}

} // namespace DX11Base
