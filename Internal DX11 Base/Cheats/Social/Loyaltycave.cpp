#include "../../pch.h"
#include "Loyaltycave.h"
#include "InstantLoveCave.h"
#include "../../showlog.h"
#include "../../Cheats.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {

  // ───────────────────────────────────────────────
  //  충성도 Cave 훅
  //  원본: movzx eax, byte ptr [rsi+0x000000EC]  (7바이트)
  //  교류 탭 열 때 모든 장수 충성도 100으로 강제
  // ───────────────────────────────────────────────

  // 전역 상태 (InstantLoveCave.cpp 또는 별도 파일에 추가)
  static uintptr_t g_loyaltyHookAddr = 0;
  static uint8_t g_loyaltyOriginal[7] = {};
  static uintptr_t g_loyaltyCaveAddr = 0;
  static bool g_loyaltyCaveApplied = false;
  std::atomic_bool g_loyaltyThreadRunning{false};

  static bool InstallLoyaltyCave(uintptr_t hookAddr) {
    g_loyaltyCaveAddr = AllocNear(hookAddr, 128);
    if (!g_loyaltyCaveAddr)
      return false;

    uint8_t *cave = (uint8_t *)g_loyaltyCaveAddr;
    int idx = 0;

    // 1. [rsi+0xEC] 에 0x64 직접 써버림
    // mov byte ptr [rsi+0xEC], 0x64
    cave[idx++] = 0xC6;
    cave[idx++] = 0x86;
    cave[idx++] = 0xEC;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x64;

    // 2. 원본 명령어 실행 (위에서 0x64로 썼으니 항상 100 반환)
    // movzx eax, byte ptr [rsi+0x000000EC]
    uint8_t orig[] = {0x0F, 0xB6, 0x86, 0xEC, 0x00, 0x00, 0x00};
    memcpy(&cave[idx], orig, 7);
    idx += 7;

    // 3. 복귀 점프
    uintptr_t retAddr = hookAddr + 7;
    cave[idx++] = 0xFF;
    cave[idx++] = 0x25;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    *(uintptr_t *)&cave[idx] = retAddr;
    idx += 8;

    return ApplyJmp(hookAddr, g_loyaltyCaveAddr, 7);
  }

  void SetInstantLoyalty(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (enable) {
      if (g_loyaltyCaveApplied)
        return;
      if (g_loyaltyThreadRunning.exchange(true))
        return;

      HANDLE hThread = CreateThread(
          nullptr, 0,
          [](LPVOID) -> DWORD {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            uintptr_t searchEnd = exeBase + 0x3000000; // 게임의 .text(코드) 영역 크기 내외로 제한 (약 48MB)

            if (!g_loyaltyHookAddr) {
              uintptr_t found = FindPattern(exeBase, searchEnd, "0F B6 86 EC 00 00 00");
              if (found) g_loyaltyHookAddr = found;
            }

            if (g_loyaltyHookAddr && !g_loyaltyCaveApplied) {
              memcpy(g_loyaltyOriginal, (void *)g_loyaltyHookAddr, 7);
              if (InstallLoyaltyCave(g_loyaltyHookAddr))
                g_loyaltyCaveApplied = true;
            }

            g_loyaltyThreadRunning.store(false);
            return 0;
          },
          nullptr, 0, nullptr);

      if (hThread)
        CloseHandle(hThread);
      else
        g_loyaltyThreadRunning.store(false);

    } else {
      if (g_loyaltyCaveApplied) {
        RestoreBytes(g_loyaltyHookAddr, g_loyaltyOriginal, 7);
        VirtualFree((LPVOID)g_loyaltyCaveAddr, 0, MEM_RELEASE);
        g_loyaltyCaveAddr = 0;
        g_loyaltyCaveApplied = false;
      }
    }
  }

} // namespace DX11Base
