#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "TechCityEditorVisibility.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  bool bTechCityEditorVisible = false;

  namespace {
    constexpr uintptr_t kSignatureOffset = 0x012CA4F1;
    constexpr uintptr_t kPatchOffset = 0x012CA4F5;
    constexpr size_t kSignatureSize = 14;
    constexpr size_t kPatchSize = 2;

    const uint8_t kOriginalSignature[kSignatureSize] = {
        0x40, 0x80, 0xFF, 0x03, 0x74, 0x50, 0x0F,
        0xB6, 0xCB, 0xE8, 0xC1, 0xFA, 0x4E, 0x00};
    const uint8_t kPatchedSignature[kSignatureSize] = {
        0x40, 0x80, 0xFF, 0x03, 0x90, 0x90, 0x0F,
        0xB6, 0xCB, 0xE8, 0xC1, 0xFA, 0x4E, 0x00};
    const uint8_t kOriginalBranch[kPatchSize] = {0x74, 0x50};
    const uint8_t kPatchedBranch[kPatchSize] = {0x90, 0x90};

    bool g_applied = false;

    bool ReadBytes(uintptr_t address, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(address, size))
        return false;

      __try {
        std::memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(out, 0, size);
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t *expected, size_t size) {
      uint8_t current[kSignatureSize]{};
      if (size > sizeof(current))
        return false;
      return ReadBytes(address, current, size) &&
             std::memcmp(current, expected, size) == 0;
    }

    bool WriteBranch(uintptr_t address, const uint8_t bytes[kPatchSize]) {
      if (!IsValidPtr(address, kPatchSize))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address),
                          kPatchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
      }

      bool success = false;
      __try {
        std::memcpy(reinterpret_cast<void *>(address), bytes, kPatchSize);
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address),
                     kPatchSize, oldProtect, &ignored);
      if (success) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(address), kPatchSize);
      }
      return success;
    }
  } // namespace

  bool IsTechCityEditorVisibleApplied() {
    return g_applied;
  }

  bool SetTechCityEditorVisible(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[기술도시편집] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      bTechCityEditorVisible = g_applied;
      return false;
    }

    const uintptr_t signatureAddress = exeBase + kSignatureOffset;
    const uintptr_t patchAddress = exeBase + kPatchOffset;

    if (enable == g_applied) {
      const uint8_t *expected = enable ? kPatchedSignature : kOriginalSignature;
      if (!BytesEqual(signatureAddress, expected, kSignatureSize)) {
        AddLog(u8"[기술도시편집] 현재 코드 상태가 추적 상태와 다릅니다. (+12CA4F1)");
        bTechCityEditorVisible = g_applied;
        return false;
      }
      bTechCityEditorVisible = g_applied;
      return true;
    }

    if (enable) {
      if (!BytesEqual(signatureAddress, kOriginalSignature, kSignatureSize)) {
        AddLog(u8"[기술도시편집] 적용 거부: 14-byte 원본 시그니처 불일치 (+12CA4F1)");
        bTechCityEditorVisible = g_applied;
        return false;
      }

      if (!WriteBranch(patchAddress, kPatchedBranch) ||
          !BytesEqual(signatureAddress, kPatchedSignature, kSignatureSize)) {
        // 설치 과정에서 우리가 쓴 2 bytes가 그대로 남아 있으면 그것만 원복합니다.
        if (BytesEqual(patchAddress, kPatchedBranch, kPatchSize))
          WriteBranch(patchAddress, kOriginalBranch);
        AddLog(u8"[기술도시편집] 패치 적용 또는 검증 실패");
        bTechCityEditorVisible = g_applied;
        return false;
      }

      g_applied = true;
      bTechCityEditorVisible = true;
      AddLog(u8"[기술도시편집] 게임 내 도시 편집기에 기술도시 표시 ON (+12CA4F5)");
      return true;
    }

    if (!BytesEqual(signatureAddress, kPatchedSignature, kSignatureSize)) {
      AddLog(u8"[기술도시편집] 해제 거부: 현재 코드가 이 기능의 패치 상태와 다릅니다.");
      bTechCityEditorVisible = g_applied;
      return false;
    }

    if (!WriteBranch(patchAddress, kOriginalBranch)) {
      AddLog(u8"[기술도시편집] 원본 분기 복구 실패");
      bTechCityEditorVisible = g_applied;
      return false;
    }

    if (!BytesEqual(signatureAddress, kOriginalSignature, kSignatureSize)) {
      // 분기 자체가 원본으로 돌아갔다면 우리 patch는 제거된 상태로 취급합니다.
      // 주변 bytes에 외부 변경이 생긴 경우 이를 덮어쓰지 않습니다.
      if (BytesEqual(patchAddress, kOriginalBranch, kPatchSize)) {
        g_applied = false;
        bTechCityEditorVisible = false;
      }
      AddLog(u8"[기술도시편집] 원본 분기 복구 후 주변 시그니처가 변경되어 전체 검증에 실패했습니다.");
      return false;
    }

    g_applied = false;
    bTechCityEditorVisible = false;
    AddLog(u8"[기술도시편집] 게임 내 도시 편집기에 기술도시 표시 OFF");
    return true;
  }
} // namespace DX11Base
