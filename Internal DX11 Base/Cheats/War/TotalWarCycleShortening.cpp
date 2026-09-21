#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "TotalWarCycleShortening.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kHookAOffset = 0x01335961;
    constexpr uintptr_t kResumeAOffset = 0x01335971;
    constexpr uintptr_t kHookBOffset = 0x0132E4BD;
    constexpr uintptr_t kResumeBOffset = 0x0132E4CD;
    constexpr size_t kHookSize = 16;

    constexpr uint8_t kOriginalA[kHookSize] = {
        0x45, 0x0F, 0xB6, 0x59, 0x38,
        0x45, 0x0F, 0xB6, 0x51, 0x35,
        0x41, 0x8D, 0x43, 0xFF,
        0x3C, 0x0B};

    constexpr uint8_t kOriginalB[kHookSize] = {
        0x45, 0x0F, 0xB6, 0x51, 0x35,
        0x45, 0x0F, 0xB6, 0x59, 0x38,
        0x41, 0x8D, 0x43, 0xFF,
        0x3C, 0x0B};

    constexpr uint8_t kPayloadTemplate[] = {
        0x45, 0x0F, 0xB6, 0x59, 0x38,
        0x45, 0x0F, 0xB6, 0x51, 0x35,
        0x41, 0x80, 0x79, 0x08, 0x01,
        0x75, 0x06,
        0x41, 0xBA, 0x01, 0x00, 0x00, 0x00,
        0x41, 0x8D, 0x43, 0xFF,
        0x3C, 0x0B};

    bool g_applied = false;
    uintptr_t g_hookA = 0;
    uintptr_t g_hookB = 0;
    uintptr_t g_caveA = 0;
    uintptr_t g_caveB = 0;

    bool MatchesBytes(uintptr_t address, const uint8_t *expected, size_t size) {
      if (!address || !expected || !IsValidPtr(address, size))
        return false;

      __try {
        return std::memcmp(reinterpret_cast<const void *>(address), expected, size) == 0;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BuildExpectedHookPatch(uintptr_t hook, uintptr_t cave, uint8_t (&out)[kHookSize]) {
      if (!hook || !cave)
        return false;

      const int64_t rel64 =
          static_cast<int64_t>(cave) - static_cast<int64_t>(hook + 5);
      if (rel64 < INT32_MIN || rel64 > INT32_MAX)
        return false;

      std::memset(out, 0x90, sizeof(out));
      out[0] = 0xE9;
      const int32_t rel32 = static_cast<int32_t>(rel64);
      std::memcpy(&out[1], &rel32, sizeof(rel32));
      return true;
    }

    bool IsOurHook(uintptr_t hook, uintptr_t cave) {
      uint8_t expected[kHookSize] = {};
      return BuildExpectedHookPatch(hook, cave, expected) &&
             MatchesBytes(hook, expected, sizeof(expected));
    }

    bool BuildCave(uintptr_t cave, uintptr_t resume) {
      if (!cave || !resume)
        return false;

      uint8_t *out = reinterpret_cast<uint8_t *>(cave);
      size_t pos = 0;
      std::memcpy(out + pos, kPayloadTemplate, sizeof(kPayloadTemplate));
      pos += sizeof(kPayloadTemplate);

      out[pos++] = 0xFF;
      out[pos++] = 0x25;
      out[pos++] = 0x00;
      out[pos++] = 0x00;
      out[pos++] = 0x00;
      out[pos++] = 0x00;
      std::memcpy(out + pos, &resume, sizeof(resume));
      pos += sizeof(resume);

      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(cave), pos);
      return true;
    }

    void FreeCaves() {
      if (g_caveA) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveA), 0, MEM_RELEASE);
        g_caveA = 0;
      }
      if (g_caveB) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveB), 0, MEM_RELEASE);
        g_caveB = 0;
      }
    }

    void ResetAddresses() {
      g_hookA = 0;
      g_hookB = 0;
      FreeCaves();
    }
  } // namespace

  bool IsTotalWarCycleShorteningApplied() {
    return g_applied;
  }

  bool SetTotalWarCycleShortening(bool enable) {
    if (enable && g_applied)
      return true;
    if (!enable && !g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[결전주기] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    if (enable) {
      g_hookA = exeBase + kHookAOffset;
      g_hookB = exeBase + kHookBOffset;

      if (!MatchesBytes(g_hookA, kOriginalA, kHookSize) ||
          !MatchesBytes(g_hookB, kOriginalB, kHookSize)) {
        AddLog(u8"[결전주기] 적용 거부: 원본 바이트가 예상값과 다릅니다.");
        ResetAddresses();
        return false;
      }

      g_caveA = AllocNear(g_hookA, 128);
      g_caveB = AllocNear(g_hookB, 128);
      if (!g_caveA || !g_caveB) {
        AddLog(u8"[결전주기] 코드케이브 할당 실패");
        ResetAddresses();
        return false;
      }

      if (!BuildCave(g_caveA, exeBase + kResumeAOffset) ||
          !BuildCave(g_caveB, exeBase + kResumeBOffset)) {
        AddLog(u8"[결전주기] 코드케이브 생성 실패");
        ResetAddresses();
        return false;
      }

      if (!ApplyJmp(g_hookA, g_caveA, kHookSize)) {
        AddLog(u8"[결전주기] Hook A 적용 실패");
        ResetAddresses();
        return false;
      }

      if (!ApplyJmp(g_hookB, g_caveB, kHookSize)) {
        RestoreBytes(g_hookA, kOriginalA, kHookSize);
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(g_hookA), kHookSize);
        AddLog(u8"[결전주기] Hook B 적용 실패 - Hook A 원복");
        ResetAddresses();
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(g_hookA), kHookSize);
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(g_hookB), kHookSize);

      if (!IsOurHook(g_hookA, g_caveA) ||
          !IsOurHook(g_hookB, g_caveB)) {
        RestoreBytes(g_hookA, kOriginalA, kHookSize);
        RestoreBytes(g_hookB, kOriginalB, kHookSize);
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(g_hookA), kHookSize);
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(g_hookB), kHookSize);
        AddLog(u8"[결전주기] 적용 검증 실패 - 전체 원복");
        ResetAddresses();
        return false;
      }

      g_applied = true;
      AddLog(u8"[결전주기] 결전 발생 주기 단축 적용 완료 (재발생 대기 1년)");
      return true;
    }

    if (!IsOurHook(g_hookA, g_caveA) ||
        !IsOurHook(g_hookB, g_caveB)) {
      AddLog(u8"[결전주기] 해제 거부: 외부 변경 또는 hook 상태 불일치 감지");
      return false;
    }

    RestoreBytes(g_hookA, kOriginalA, kHookSize);
    RestoreBytes(g_hookB, kOriginalB, kHookSize);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_hookA), kHookSize);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_hookB), kHookSize);

    if (!MatchesBytes(g_hookA, kOriginalA, kHookSize) ||
        !MatchesBytes(g_hookB, kOriginalB, kHookSize)) {
      AddLog(u8"[결전주기] 원본 바이트 복구 검증 실패");
      return false;
    }

    g_applied = false;
    ResetAddresses();
    AddLog(u8"[결전주기] 결전 발생 주기 단축 해제 / 원본 복구 완료");
    return true;
  }
} // namespace DX11Base
