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
    constexpr uint32_t kGaugeMax = 10000;

    static uintptr_t g_hookAddr = 0;
    static uintptr_t g_caveAddr = 0;
    static uint8_t g_original[7] = {};
    static bool g_hookApplied = false;

    static bool BuildGaugeSideCave(uintptr_t hookAddr) {
      g_caveAddr = AllocNear(hookAddr, 160);
      if (!g_caveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_caveAddr);
      int idx = 0;

      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit32 = [&](uint32_t v) {
        *reinterpret_cast<uint32_t *>(&cave[idx]) = v;
        idx += 4;
      };
      auto emit64 = [&](uintptr_t v) {
        *reinterpret_cast<uintptr_t *>(&cave[idx]) = v;
        idx += 8;
      };

      // Preserve RAX. The final original CMP restores the flags exactly as the
      // unpatched game code would have produced them.
      emit8(0x50); // push rax

      // cmp byte ptr [rbx-0xD8], 0  ; battle side: 0=attack
      emit8(0x80); emit8(0xBB); emit32(0xFFFFFF28u); emit8(0x00);
      emit8(0x75); const int jneDefense = idx++;

      // if (bMaxAttackStratagemGauge) [rbx+0x64] = 10000
      emit8(0x48); emit8(0xB8); emit64(reinterpret_cast<uintptr_t>(&bMaxAttackStratagemGauge));
      emit8(0x80); emit8(0x38); emit8(0x00);
      emit8(0x74); const int jeOriginalFromAttack = idx++;
      emit8(0xC7); emit8(0x43); emit8(0x64); emit32(kGaugeMax);
      emit8(0xEB); const int jmpOriginalFromAttack = idx++;

      const int checkDefense = idx;

      // cmp byte ptr [rbx-0xD8], 1  ; battle side: 1=defense
      emit8(0x80); emit8(0xBB); emit32(0xFFFFFF28u); emit8(0x01);
      emit8(0x75); const int jneOriginalFromDefense = idx++;

      // if (bMaxDefenseStratagemGauge) [rbx+0x64] = 10000
      emit8(0x48); emit8(0xB8); emit64(reinterpret_cast<uintptr_t>(&bMaxDefenseStratagemGauge));
      emit8(0x80); emit8(0x38); emit8(0x00);
      emit8(0x74); const int jeOriginalFromDefense = idx++;
      emit8(0xC7); emit8(0x43); emit8(0x64); emit32(kGaugeMax);

      const int runOriginal = idx;

      emit8(0x58); // pop rax

      // Original instruction: cmp dword ptr [rbx+64], 00002710
      emit8(0x81); emit8(0x7B); emit8(0x64); emit32(kGaugeMax);

      // jmp hookAddr+7
      const uintptr_t jmpFrom = g_caveAddr + static_cast<uintptr_t>(idx);
      const uintptr_t backTo = hookAddr + 7;
      const int64_t rel64 = static_cast<int64_t>(backTo) - static_cast<int64_t>(jmpFrom + 5);
      if (rel64 < INT32_MIN || rel64 > INT32_MAX) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);
        g_caveAddr = 0;
        return false;
      }
      emit8(0xE9);
      emit32(static_cast<uint32_t>(static_cast<int32_t>(rel64)));

      auto patchRel8 = [&](int dispIndex, int target) -> bool {
        const int rel = target - (dispIndex + 1);
        if (rel < -128 || rel > 127)
          return false;
        cave[dispIndex] = static_cast<uint8_t>(static_cast<int8_t>(rel));
        return true;
      };

      if (!patchRel8(jneDefense, checkDefense) ||
          !patchRel8(jeOriginalFromAttack, runOriginal) ||
          !patchRel8(jmpOriginalFromAttack, runOriginal) ||
          !patchRel8(jneOriginalFromDefense, runOriginal) ||
          !patchRel8(jeOriginalFromDefense, runOriginal)) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);
        g_caveAddr = 0;
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      return ApplyJmp(hookAddr, g_caveAddr, 7);
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

      // Legacy CT's actual gauge processing point:
      //   cmp dword ptr [rbx+64], 00002710
      // where rbx == battle-side object + 0xF0, so:
      //   [rbx-D8] == object+0x18 (0=attack, 1=defense)
      //   [rbx+64] == object+0x154 (stratagem gauge)
      const char *pattern = "81 7B 64 10 27 00 00 74 ? B0 ? 48 8B";
      const uintptr_t found = FindPattern(exeBase, imageEnd, pattern);
      if (!found) {
        AddLog(u8"[책략게이지] 진영별 게이지 처리 패턴을 찾지 못했습니다. (구 CT 패턴 불일치)");
        return false;
      }

      g_hookAddr = found;
      static const uint8_t expected[7] = {0x81, 0x7B, 0x64, 0x10, 0x27, 0x00, 0x00};
      if (!IsValidPtr(g_hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(g_hookAddr), expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략게이지] 게이지 처리 지점 검증 실패: hook=%p",
               reinterpret_cast<void *>(g_hookAddr));
        g_hookAddr = 0;
        return false;
      }

      std::memcpy(g_original, reinterpret_cast<const void *>(g_hookAddr), sizeof(g_original));
      if (!BuildGaugeSideCave(g_hookAddr)) {
        AddLog(u8"[책략게이지] 진영별 게이지 훅 설치 실패.");
        g_hookAddr = 0;
        g_caveAddr = 0;
        return false;
      }

      g_hookApplied = true;
      AddLog(u8"[책략게이지] 진영별 처리 훅 설치 완료: %p", reinterpret_cast<void *>(g_hookAddr));
      AddLog(u8"[책략게이지] 공격/수비 체크 상태를 각각 독립 적용합니다. 미체크 진영은 원본 로직을 그대로 둡니다.");
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
    std::memset(g_original, 0, sizeof(g_original));

    AddLog(u8"[책략게이지] 진영별 처리 훅 해제 완료.");
    return true;
  }

  void UpdateStratagemGaugeMax() {
    // The current implementation works at the game's own side-specific gauge
    // processing point, so BattleMonitor no longer needs to rewrite +0x154.
  }
} // namespace DX11Base
