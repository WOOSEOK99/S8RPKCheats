#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "MissionAppointmentLimit.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kValueOffset = 0x03BB2148;
    constexpr int32_t kDefaultValue = 10;

    struct CodeAssert {
      uintptr_t offset;
      const uint8_t *bytes;
      size_t size;
    };

    constexpr uint8_t kAssert0[] = {0x48, 0x8D, 0x05, 0xA9, 0x8D, 0x52, 0x03};
    constexpr uint8_t kAssert1[] = {0x44, 0x8B, 0xB8, 0x38, 0x3B, 0x00, 0x00};
    constexpr uint8_t kAssert2[] = {0x3B, 0xA8, 0x38, 0x3B, 0x00, 0x00};
    constexpr uint8_t kAssert3[] = {0x48, 0x63, 0x88, 0x38, 0x3B, 0x00, 0x00};
    constexpr uint8_t kAssert4[] = {0x44, 0x8B, 0xB8, 0x38, 0x3B, 0x00, 0x00};
    constexpr uint8_t kAssert5[] = {0x44, 0x3B, 0xA0, 0x38, 0x3B, 0x00, 0x00};
    constexpr uint8_t kAssert6[] = {0x44, 0x8B, 0xA0, 0x38, 0x3B, 0x00, 0x00};

    constexpr CodeAssert kCodeAsserts[] = {
        {0x00685860, kAssert0, sizeof(kAssert0)},
        {0x01357341, kAssert1, sizeof(kAssert1)},
        {0x01EA0883, kAssert2, sizeof(kAssert2)},
        {0x01E9F0DA, kAssert3, sizeof(kAssert3)},
        {0x01E9EBDE, kAssert4, sizeof(kAssert4)},
        {0x01E89E25, kAssert5, sizeof(kAssert5)},
        {0x01452460, kAssert6, sizeof(kAssert6)},
    };

    bool g_managed = false;
    bool g_originalCaptured = false;
    int32_t g_originalValue = kDefaultValue;
    int32_t g_lastAppliedValue = kDefaultValue;

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

    bool VerifyCodeReferences(uintptr_t exeBase) {
      for (const CodeAssert &assertion : kCodeAsserts) {
        uint8_t current[7]{};
        if (assertion.size > sizeof(current) ||
            !ReadBytes(exeBase + assertion.offset, current, assertion.size) ||
            std::memcmp(current, assertion.bytes, assertion.size) != 0) {
          AddLog(u8"[임무한도] 코드 참조 검증 실패: SAN8RPK.exe+%08llX",
                 static_cast<unsigned long long>(assertion.offset));
          return false;
        }
      }
      return true;
    }

    bool ReadValue(uintptr_t address, int32_t &value) {
      if (!IsValidPtr(address, sizeof(value)))
        return false;

      __try {
        value = *reinterpret_cast<const int32_t *>(address);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool WriteValue(uintptr_t address, int32_t value) {
      if (!IsValidPtr(address, sizeof(value)))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address),
                          sizeof(value), PAGE_READWRITE, &oldProtect)) {
        return false;
      }

      bool success = false;
      __try {
        *reinterpret_cast<int32_t *>(address) = value;
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address),
                     sizeof(value), oldProtect, &ignored);
      return success;
    }

    bool GetValueAddress(uintptr_t &address) {
      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
      if (!exeBase)
        return false;

      address = exeBase + kValueOffset;
      return true;
    }
  } // namespace

  bool ReadMissionAppointmentLimit(int &value) {
    uintptr_t address = 0;
    if (!GetValueAddress(address))
      return false;

    int32_t current = 0;
    if (!ReadValue(address, current))
      return false;

    value = static_cast<int>(current);
    return true;
  }

  bool IsMissionAppointmentLimitManaged() {
    return g_managed;
  }

  bool SetMissionAppointmentLimit(int value) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[임무한도] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t address = exeBase + kValueOffset;
    int32_t current = 0;
    if (!ReadValue(address, current)) {
      AddLog(u8"[임무한도] 현재값 읽기 실패: SAN8RPK.exe+03BB2148");
      return false;
    }

    const int32_t requested = static_cast<int32_t>(value);

    if (!g_managed) {
      if (!VerifyCodeReferences(exeBase))
        return false;

      if (current != kDefaultValue && current != requested) {
        AddLog(u8"[임무한도] 최초 적용 거부: 현재=%d / 예상 기본값=10 / 요청=%d",
               current, requested);
        return false;
      }

      g_originalValue = current;
      g_originalCaptured = true;
    } else if (current != g_lastAppliedValue && current != requested) {
      AddLog(u8"[임무한도] 값 변경 거부: 외부 변경 감지 (현재=%d / 마지막 적용=%d / 요청=%d)",
             current, g_lastAppliedValue, requested);
      return false;
    }

    if (current != requested) {
      if (!WriteValue(address, requested)) {
        AddLog(u8"[임무한도] 값 쓰기 실패: %d", requested);
        return false;
      }

      int32_t verify = 0;
      if (!ReadValue(address, verify) || verify != requested) {
        AddLog(u8"[임무한도] 값 적용 후 검증 실패: 요청=%d", requested);
        return false;
      }
    }

    g_lastAppliedValue = requested;
    g_managed = true;
    AddLog(u8"[임무한도] 총 임명 한도 적용: %d", requested);
    return true;
  }

  bool RestoreMissionAppointmentLimit() {
    if (!g_managed)
      return true;

    if (!g_originalCaptured) {
      AddLog(u8"[임무한도] 원본값이 없어 원복을 중단했습니다.");
      return false;
    }

    uintptr_t address = 0;
    if (!GetValueAddress(address)) {
      AddLog(u8"[임무한도] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    int32_t current = 0;
    if (!ReadValue(address, current)) {
      AddLog(u8"[임무한도] 원복 전 현재값 읽기 실패");
      return false;
    }

    if (current != g_lastAppliedValue) {
      AddLog(u8"[임무한도] 원복 거부: 외부 변경 감지 (현재=%d / 마지막 적용=%d)",
             current, g_lastAppliedValue);
      return false;
    }

    if (current != g_originalValue) {
      if (!WriteValue(address, g_originalValue)) {
        AddLog(u8"[임무한도] 원본값 복구 실패: %d", g_originalValue);
        return false;
      }

      int32_t verify = 0;
      if (!ReadValue(address, verify) || verify != g_originalValue) {
        AddLog(u8"[임무한도] 원본값 복구 후 검증 실패: %d", g_originalValue);
        return false;
      }
    }

    AddLog(u8"[임무한도] 총 임명 한도 원복: %d", g_originalValue);
    g_managed = false;
    g_originalCaptured = false;
    g_originalValue = kDefaultValue;
    g_lastAppliedValue = kDefaultValue;
    return true;
  }
} // namespace DX11Base
