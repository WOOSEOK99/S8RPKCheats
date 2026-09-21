#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "TroopCountCombatScaling.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kSignatureOffset = 0x01E2D9FF;
    constexpr uintptr_t kPatchOffset = 0x01E2DA01;

    const uint8_t kOriginalSignature[11] = {
        0x3B, 0xFD, 0x7E, 0x57, 0x80, 0x7C, 0x24, 0x70, 0x00, 0x75, 0x50};
    const uint8_t kPatchedSignature[11] = {
        0x3B, 0xFD, 0xEB, 0x57, 0x80, 0x7C, 0x24, 0x70, 0x00, 0x75, 0x50};
    const uint8_t kOriginalPatch[2] = {0x7E, 0x57};
    const uint8_t kEnabledPatch[2] = {0xEB, 0x57};

    bool g_applied = false;

    bool ReadBytes(uintptr_t address, uint8_t *out, size_t size) {
      if (!address || !out || !size || !IsValidPtr(address, size))
        return false;

      __try {
        memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t *expected, size_t size) {
      uint8_t current[16]{};
      if (!expected || size > sizeof(current))
        return false;
      return ReadBytes(address, current, size) &&
             memcmp(current, expected, size) == 0;
    }

    bool WriteBytes(uintptr_t address, const uint8_t *bytes, size_t size) {
      if (!address || !bytes || !size || !IsValidPtr(address, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool success = false;
      __try {
        memcpy(reinterpret_cast<void *>(address), bytes, size);
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);
      if (success) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(address), size);
      }
      return success;
    }
  } // namespace

  bool IsTroopCountCombatScalingApplied() {
    return g_applied;
  }

  bool SetTroopCountCombatScaling(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[병력공방] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t signatureAddress = exeBase + kSignatureOffset;
    const uintptr_t patchAddress = exeBase + kPatchOffset;

    if (enable) {
      if (g_applied) {
        if (BytesEqual(signatureAddress, kPatchedSignature,
                       sizeof(kPatchedSignature)))
          return true;

        AddLog(u8"[병력공방] 적용 상태 불일치: 외부 변경이 감지되었습니다.");
        return false;
      }

      if (!BytesEqual(signatureAddress, kOriginalSignature,
                      sizeof(kOriginalSignature))) {
        AddLog(u8"[병력공방] 적용 거부: 검증 바이트 불일치 (+1E2D9FF)");
        return false;
      }

      if (!WriteBytes(patchAddress, kEnabledPatch, sizeof(kEnabledPatch)) ||
          !BytesEqual(signatureAddress, kPatchedSignature,
                      sizeof(kPatchedSignature))) {
        WriteBytes(patchAddress, kOriginalPatch, sizeof(kOriginalPatch));
        AddLog(u8"[병력공방] 오리지널식 공방 계산 분기 적용 실패");
        return false;
      }

      g_applied = true;
      AddLog(u8"[병력공방] 병력수 공방 반영 활성화");
      return true;
    }

    if (!g_applied)
      return true;

    if (!BytesEqual(signatureAddress, kPatchedSignature,
                    sizeof(kPatchedSignature))) {
      AddLog(u8"[병력공방] 해제 거부: 패치 지점에 외부 변경이 감지되었습니다.");
      return false;
    }

    if (!WriteBytes(patchAddress, kOriginalPatch, sizeof(kOriginalPatch)) ||
        !BytesEqual(signatureAddress, kOriginalSignature,
                    sizeof(kOriginalSignature))) {
      AddLog(u8"[병력공방] 원래 계산 분기 복구 실패");
      return false;
    }

    g_applied = false;
    AddLog(u8"[병력공방] 병력수 공방 반영 해제 - 원래 계산 분기 복구");
    return true;
  }
} // namespace DX11Base
