#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "ReinforcementArrivalAction.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kDispatchTableOffset = 0x01D86D58;
    constexpr uintptr_t kAnchorTurnOffset = 0x01D86585;
    constexpr uintptr_t kAnchorArrivalCallOffset = 0x01D86C51;
    constexpr uintptr_t kAnchorOrderCallOffset = 0x01D86C60;

    const uint8_t kDispatchOriginal[8] =
        {0x13, 0x6C, 0xD8, 0x01, 0x5B, 0x6C, 0xD8, 0x01};
    const uint8_t kDispatchEnabled[8] =
        {0x5B, 0x6C, 0xD8, 0x01, 0x13, 0x6C, 0xD8, 0x01};

    const uint8_t kAnchorTurnOriginal[5] =
        {0xFF, 0xC8, 0x83, 0xF8, 0x0E};
    const uint8_t kAnchorArrivalCallOriginal[5] =
        {0xE8, 0x5A, 0xAA, 0x0B, 0x00};
    const uint8_t kAnchorOrderCallOriginal[5] =
        {0xE8, 0x6B, 0x66, 0x0B, 0x00};

    bool g_applied = false;

    bool ReadBytes(uintptr_t address, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(address, size))
        return false;

      __try {
        memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t *expected, size_t size) {
      uint8_t current[8]{};
      if (!expected || size > sizeof(current))
        return false;
      return ReadBytes(address, current, size) &&
             memcmp(current, expected, size) == 0;
    }

    bool WriteBytes(uintptr_t address, const uint8_t *bytes, size_t size) {
      if (!bytes || !size || !IsValidPtr(address, size))
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

    bool ValidateVersionAnchors(uintptr_t exeBase) {
      struct Anchor {
        uintptr_t offset;
        const uint8_t *bytes;
        size_t size;
        const char *name;
      };

      const Anchor anchors[] = {
          {kAnchorTurnOffset, kAnchorTurnOriginal,
           sizeof(kAnchorTurnOriginal), "turn"},
          {kAnchorArrivalCallOffset, kAnchorArrivalCallOriginal,
           sizeof(kAnchorArrivalCallOriginal), "arrival"},
          {kAnchorOrderCallOffset, kAnchorOrderCallOriginal,
           sizeof(kAnchorOrderCallOriginal), "order"},
      };

      for (const auto &anchor : anchors) {
        if (!BytesEqual(exeBase + anchor.offset, anchor.bytes, anchor.size)) {
          AddLog(u8"[원군즉시행동] 적용 거부: %s 검증 바이트 불일치 (+%llX)",
                 anchor.name,
                 static_cast<unsigned long long>(anchor.offset));
          return false;
        }
      }
      return true;
    }
  } // namespace

  bool IsReinforcementArrivalActionApplied() {
    return g_applied;
  }

  bool SetReinforcementArrivalAction(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[원군즉시행동] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t tableAddress = exeBase + kDispatchTableOffset;
    const bool isOriginal =
        BytesEqual(tableAddress, kDispatchOriginal, sizeof(kDispatchOriginal));
    const bool isEnabled =
        BytesEqual(tableAddress, kDispatchEnabled, sizeof(kDispatchEnabled));

    if (!isOriginal && !isEnabled) {
      AddLog(u8"[원군즉시행동] 적용 거부: 디스패치 테이블 값 불일치 (+1D86D58)");
      return false;
    }

    if (enable) {
      if (g_applied && isEnabled)
        return true;

      if (!ValidateVersionAnchors(exeBase))
        return false;

      if (!isEnabled &&
          !WriteBytes(tableAddress, kDispatchEnabled, sizeof(kDispatchEnabled))) {
        AddLog(u8"[원군즉시행동] 디스패치 순서 변경 실패");
        return false;
      }

      g_applied = true;
      AddLog(u8"[원군즉시행동] 활성화 - 원군 도착 처리를 명령 처리보다 먼저 실행");
      return true;
    }

    if (!g_applied && isOriginal)
      return true;

    if (!isEnabled) {
      AddLog(u8"[원군즉시행동] 해제 거부: 활성화된 테이블 상태가 아닙니다.");
      return false;
    }

    if (!WriteBytes(tableAddress, kDispatchOriginal, sizeof(kDispatchOriginal))) {
      AddLog(u8"[원군즉시행동] 원본 디스패치 순서 복구 실패");
      return false;
    }

    g_applied = false;
    AddLog(u8"[원군즉시행동] 해제 - 원본 디스패치 순서 복구");
    return true;
  }
} // namespace DX11Base
