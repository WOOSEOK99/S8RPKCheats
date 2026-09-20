#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "ShortBattleCooldown.h"

#include <cstdint>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kShortBattleCooldownOffset = 0x03BB0214;
    constexpr int32_t kDefaultShortBattleCooldown = 10;

    bool g_shortBattleCooldownApplied = false;
    bool g_shortBattleCooldownOriginalCaptured = false;
    int32_t g_shortBattleCooldownOriginal = kDefaultShortBattleCooldown;
    int32_t g_shortBattleCooldownLastApplied = kDefaultShortBattleCooldown;

    bool ReadShortBattleCooldown(uintptr_t address, int32_t &value) {
      if (!IsValidPtr(address, sizeof(value)))
        return false;

      __try {
        value = *reinterpret_cast<const int32_t *>(address);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool WriteShortBattleCooldown(uintptr_t address, int32_t value) {
      if (!IsValidPtr(address, sizeof(value)))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(value), PAGE_READWRITE, &oldProtect))
        return false;

      bool success = false;
      __try {
        *reinterpret_cast<int32_t *>(address) = value;
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), sizeof(value), oldProtect, &ignored);
      return success;
    }
  } // namespace

  bool IsShortBattleCooldownApplied() {
    return g_shortBattleCooldownApplied;
  }

  bool SetShortBattleCooldown(bool enable, int days) {
    const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[단기접전] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t address = exeBase + kShortBattleCooldownOffset;
    int32_t current = 0;
    if (!ReadShortBattleCooldown(address, current)) {
      AddLog(u8"[단기접전] 쿨타임 주소 읽기 실패: SAN8RPK.exe+03BB0214");
      return false;
    }

    const int32_t requested = static_cast<int32_t>(days);

    if (enable) {
      if (!g_shortBattleCooldownApplied) {
        // CT에서 확인된 기본값(10) 또는 이미 사용자가 요청한 값일 때만 최초 적용합니다.
        // 다른 값이면 다른 버전/외부 패치 가능성이 있으므로 쓰지 않습니다.
        if (current != kDefaultShortBattleCooldown && current != requested) {
          AddLog(u8"[단기접전] 적용 거부: 현재값=%d / 예상 기본값=10 / 요청값=%d",
                 current, requested);
          return false;
        }

        g_shortBattleCooldownOriginal = current;
        g_shortBattleCooldownOriginalCaptured = true;
      } else if (current != g_shortBattleCooldownLastApplied && current != requested) {
        AddLog(u8"[단기접전] 값 변경 거부: 외부 변경 감지 (현재=%d / 마지막 적용=%d / 요청=%d)",
               current, g_shortBattleCooldownLastApplied, requested);
        return false;
      }

      if (current != requested && !WriteShortBattleCooldown(address, requested)) {
        AddLog(u8"[단기접전] 쿨타임 쓰기 실패: %d", requested);
        return false;
      }

      g_shortBattleCooldownLastApplied = requested;
      g_shortBattleCooldownApplied = true;
      AddLog(u8"[단기접전] 쿨타임 적용: %d일 (기본값 10일)", requested);
      return true;
    }

    if (!g_shortBattleCooldownApplied)
      return true;

    if (!g_shortBattleCooldownOriginalCaptured) {
      AddLog(u8"[단기접전] 원본값이 없어 해제를 중단했습니다.");
      return false;
    }

    // 활성화 뒤 다른 도구가 값을 바꿨다면 그 변경을 덮어쓰지 않습니다.
    if (current != g_shortBattleCooldownLastApplied) {
      AddLog(u8"[단기접전] 해제 거부: 외부 변경 감지 (현재=%d / 마지막 적용=%d)",
             current, g_shortBattleCooldownLastApplied);
      return false;
    }

    if (current != g_shortBattleCooldownOriginal &&
        !WriteShortBattleCooldown(address, g_shortBattleCooldownOriginal)) {
      AddLog(u8"[단기접전] 원본값 복구 실패: %d", g_shortBattleCooldownOriginal);
      return false;
    }

    AddLog(u8"[단기접전] 쿨타임 원복: %d일", g_shortBattleCooldownOriginal);
    g_shortBattleCooldownApplied = false;
    g_shortBattleCooldownOriginalCaptured = false;
    g_shortBattleCooldownLastApplied = kDefaultShortBattleCooldown;
    return true;
  }
} // namespace DX11Base
