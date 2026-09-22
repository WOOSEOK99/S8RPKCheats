#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "StratagemGaugeMax.h"

#include <psapi.h>
#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kSideOffset = 0x18;
    constexpr uintptr_t kGaugeOffset = 0x154;
    constexpr uint16_t kGaugeMax = 10000;

    static uintptr_t g_hookAddr = 0;
    static uintptr_t g_caveAddr = 0;
    static uint8_t g_original[5] = {};
    static bool g_hookApplied = false;

    // Written by the generated code cave. These are only consumed while
    // BattleMonitor reports an active battle.
    static volatile uintptr_t g_attackInfo = 0;
    static volatile uintptr_t g_defenseInfo = 0;

    static uintptr_t g_lastLoggedAttack = 0;
    static uintptr_t g_lastLoggedDefense = 0;

    static bool BuildCaptureCave(uintptr_t hookAddr) {
      g_caveAddr = AllocNear(hookAddr, 128);
      if (!g_caveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_caveAddr);
      int idx = 0;

      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit64 = [&](uintptr_t v) {
        *reinterpret_cast<uintptr_t *>(&cave[idx]) = v;
        idx += 8;
      };

      // Preserve RDX. The original tiny getter only changes EAX.
      emit8(0x52); // push rdx

      // cmp byte ptr [rax+18], 0
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x00);
      emit8(0x75); const int jneDefenseDisp = idx++; // jne checkDefense

      // attack: mov rdx, &g_attackInfo / mov [rdx], rax
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_attackInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);
      emit8(0xEB); const int jmpDoneFromAttackDisp = idx++;

      const int checkDefense = idx;

      // cmp byte ptr [rax+18], 1
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x01);
      emit8(0x75); const int jneDoneDisp = idx++; // jne done

      // defense: mov rdx, &g_defenseInfo / mov [rdx], rax
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_defenseInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);

      const int done = idx;

      // Restore RDX, then execute the original getter and its RET.
      emit8(0x5A);                         // pop rdx
      emit8(0x0F); emit8(0xB6); emit8(0x40); emit8(0x18); // movzx eax,byte ptr [rax+18]
      emit8(0xC3);                         // ret

      auto patchRel8 = [&](int dispIndex, int target) -> bool {
        const int rel = target - (dispIndex + 1);
        if (rel < -128 || rel > 127)
          return false;
        cave[dispIndex] = static_cast<uint8_t>(static_cast<int8_t>(rel));
        return true;
      };

      if (!patchRel8(jneDefenseDisp, checkDefense) ||
          !patchRel8(jmpDoneFromAttackDisp, done) ||
          !patchRel8(jneDoneDisp, done)) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);
        g_caveAddr = 0;
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      return ApplyJmp(hookAddr, g_caveAddr, 5);
    }

    static bool ValidateSideInfo(uintptr_t ptr, uint8_t expectedSide, uint16_t *outGauge) {
      if (!ptr)
        return false;
      if (!IsValidPtr(ptr + kSideOffset, 1) || !IsValidPtr(ptr + kGaugeOffset, sizeof(uint16_t)))
        return false;

      const uint8_t side = *reinterpret_cast<const uint8_t *>(ptr + kSideOffset);
      if (side != expectedSide)
        return false;

      const uint16_t gauge = *reinterpret_cast<const uint16_t *>(ptr + kGaugeOffset);
      // The old CT documents 10000 as the maximum. Values above that are treated
      // as a stale/wrong object instead of being overwritten blindly.
      if (gauge > kGaugeMax)
        return false;

      if (outGauge)
        *outGauge = gauge;
      return true;
    }

    static bool WriteGaugeMax(uintptr_t ptr, uint8_t expectedSide) {
      uint16_t gauge = 0;
      if (!ValidateSideInfo(ptr, expectedSide, &gauge))
        return false;
      if (gauge == kGaugeMax)
        return true;

      const uintptr_t addr = ptr + kGaugeOffset;
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(uint16_t), PAGE_READWRITE, &oldProtect))
        return false;

      *reinterpret_cast<uint16_t *>(addr) = kGaugeMax;
      VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(uint16_t), oldProtect, &tmpProtect);
      return *reinterpret_cast<const uint16_t *>(addr) == kGaugeMax;
    }
  }

  bool SetStratagemGaugeCapture(bool enable) {
    if (enable) {
      if (g_hookApplied)
        return true;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(exeBase), &mi, sizeof(mi)))
        return false;

      const uintptr_t imageEnd = exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // Old CT getter:
      // mov rax,[rcx+08]
      // mov rcx,[rax+30]
      // mov rax,[rcx]
      // movzx eax,byte ptr [rax+18]
      // ret
      const char *pattern = "48 8B 41 08 48 8B 48 30 48 8B 01 0F B6 40 18 C3";
      const uintptr_t found = FindPattern(exeBase, imageEnd, pattern);
      if (!found) {
        AddLog(u8"[책략게이지] 전장 진영 getter 패턴을 찾지 못했습니다. (구 CT 패턴 불일치)");
        return false;
      }

      g_hookAddr = found + 0x0B; // movzx eax,byte ptr [rax+18]
      static const uint8_t expected[5] = {0x0F, 0xB6, 0x40, 0x18, 0xC3};
      if (!IsValidPtr(g_hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(g_hookAddr), expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략게이지] getter 검증 실패: hook=%p", reinterpret_cast<void *>(g_hookAddr));
        g_hookAddr = 0;
        return false;
      }

      std::memcpy(g_original, reinterpret_cast<const void *>(g_hookAddr), sizeof(g_original));
      if (!BuildCaptureCave(g_hookAddr)) {
        AddLog(u8"[책략게이지] 전장 진영 포인터 캡처 훅 설치 실패.");
        g_hookAddr = 0;
        g_caveAddr = 0;
        return false;
      }

      g_hookApplied = true;
      g_attackInfo = 0;
      g_defenseInfo = 0;
      g_lastLoggedAttack = 0;
      g_lastLoggedDefense = 0;

      AddLog(u8"[책략게이지] 캡처 훅 설치 완료: getter=%p", reinterpret_cast<void *>(g_hookAddr));
      AddLog(u8"[책략게이지] CT 기준 구조: +18 진영(0=공격/1=수비), +154 게이지(uint16, max=10000)");
      return true;
    }

    if (!g_hookApplied)
      return true;

    RestoreBytes(g_hookAddr, g_original, sizeof(g_original));
    if (g_caveAddr)
      VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);

    g_hookAddr = 0;
    g_caveAddr = 0;
    g_hookApplied = false;
    g_attackInfo = 0;
    g_defenseInfo = 0;
    g_lastLoggedAttack = 0;
    g_lastLoggedDefense = 0;
    std::memset(g_original, 0, sizeof(g_original));

    AddLog(u8"[책략게이지] 캡처 훅 해제 완료.");
    return true;
  }

  void UpdateStratagemGaugeMax() {
    if (!g_hookApplied)
      return;

    if (bMaxAttackStratagemGauge) {
      const uintptr_t ptr = g_attackInfo;
      uint16_t gauge = 0;
      if (ptr && ValidateSideInfo(ptr, 0, &gauge)) {
        if (ptr != g_lastLoggedAttack) {
          AddLog(u8"[책략게이지] 공격측 포인터 확인: %p / 현재=%u",
                 reinterpret_cast<void *>(ptr), static_cast<unsigned>(gauge));
          g_lastLoggedAttack = ptr;
        }
        WriteGaugeMax(ptr, 0);
      }
    }

    if (bMaxDefenseStratagemGauge) {
      const uintptr_t ptr = g_defenseInfo;
      uint16_t gauge = 0;
      if (ptr && ValidateSideInfo(ptr, 1, &gauge)) {
        if (ptr != g_lastLoggedDefense) {
          AddLog(u8"[책략게이지] 수비측 포인터 확인: %p / 현재=%u",
                 reinterpret_cast<void *>(ptr), static_cast<unsigned>(gauge));
          g_lastLoggedDefense = ptr;
        }
        WriteGaugeMax(ptr, 1);
      }
    }
  }
} // namespace DX11Base
