#include "pch.h"
#include "TechZero.h"
#include "Cheats.h"
#include "showlog.h"
#include <psapi.h>


namespace DX11Base {

  void AddLog(const char *fmt, ...);
  bool IsValidPtr(uintptr_t addr, SIZE_T size);

  // ───────────────────────────────────────────────
  //  세력 기술 초기화 (분기월 자동 실행)
  //  매 200ms 체크 → 1/4/7/10월 + elapsed==0 시 실행
  //  rootBase + 0xC6A4D 기준, stride=0x998, 100개 세력
  // ───────────────────────────────────────────────

  bool g_techZeroRunning = false;

  static bool g_techZeroEnabled = false;
  static bool g_techZeroDone = false;
  static bool g_techZeroPending = false;
  static DWORD g_techZeroTriggerTick = 0;
  static HANDLE g_techZeroThread = nullptr;

  static const int k_techCount = 100;
  static const int k_techStride = 0x998;

  static uintptr_t ResolveRootBase() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    uintptr_t p = *(uintptr_t *)(exeBase + 0x034C8630);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x8);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x10);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;

    return p;
  }

  static bool IsQuarterMonth(uint8_t month) { return month == 1 || month == 4 || month == 7 || month == 10; }

  static void ZeroAllFactionTech(uintptr_t rootBase) {
    uintptr_t techBase = rootBase + 0xC6A4D;

    for (int n = 0; n < k_techCount; ++n) {
      uintptr_t addr = techBase + n * k_techStride;
      if (!IsValidPtr(addr, 5))
        continue;

      DWORD old, tmp;
      VirtualProtect((LPVOID)addr, 5, PAGE_READWRITE, &old);
      *(uint8_t *)(addr + 0) = 0;
      *(uint8_t *)(addr + 1) = 0;
      *(uint8_t *)(addr + 2) = 0;
      *(uint8_t *)(addr + 3) = 0;
      *(uint8_t *)(addr + 4) = 0;
      VirtualProtect((LPVOID)addr, 5, old, &tmp);
    }

    AddLog(u8"[기술초기화] %d개 세력 기술 초기화 완료", k_techCount);
  }

  // 타이머 스레드 (200ms 루프)
  static DWORD WINAPI TechZeroTimerThread(LPVOID) {
    while (g_techZeroEnabled) {
      Sleep(200);

      if (!g_techZeroEnabled)
        break;

      uintptr_t rootBase = ResolveRootBase();
      if (!rootBase)
        continue;

      uintptr_t monthAddr = rootBase + 0x49EC94;
      uintptr_t elapsedAddr = rootBase + 0x8142;

      if (!IsValidPtr(monthAddr, 1) || !IsValidPtr(elapsedAddr, 2))
        continue;

      uint8_t month = *(uint8_t *)monthAddr;
      uint16_t elapsed = *(uint16_t *)elapsedAddr;

      // 메인 메뉴 복귀 감지 → 리셋
      if (month == 0 && elapsed == 0) {
        g_techZeroDone = false;
        g_techZeroPending = false;
        g_techZeroTriggerTick = 0;
        continue;
      }

      // 대기 상태가 아닐 때
      if (!g_techZeroPending) {
        if (IsQuarterMonth(month) && elapsed == 0 && !g_techZeroDone) {
          g_techZeroPending = true;
          g_techZeroTriggerTick = GetTickCount();
        }
        continue;
      }

      // 200ms 대기 후 실행
      if (GetTickCount() - g_techZeroTriggerTick >= 200) {
        ZeroAllFactionTech(rootBase);
        g_techZeroDone = true;
        g_techZeroPending = false;
        g_techZeroTriggerTick = 0;
      }
    }

    g_techZeroRunning = false;
    return 0;
  }

  void SetTechZero(bool enable) {
    if (enable) {
      if (g_techZeroEnabled)
        return;

      g_techZeroEnabled = true;
      g_techZeroDone = false;
      g_techZeroPending = false;
      g_techZeroTriggerTick = 0;

      g_techZeroThread = CreateThread(nullptr, 0, TechZeroTimerThread, nullptr, 0, nullptr);

      if (g_techZeroThread) {
        g_techZeroRunning = true;
        AddLog(u8"[기술초기화] 활성화 - 분기월 자동 초기화 시작");
      }

    } else {
      g_techZeroEnabled = false;

      if (g_techZeroThread) {
        WaitForSingleObject(g_techZeroThread, 1000);
        CloseHandle(g_techZeroThread);
        g_techZeroThread = nullptr;
      }

      g_techZeroDone = false;
      g_techZeroPending = false;
      g_techZeroTriggerTick = 0;

      AddLog(u8"[기술초기화] 비활성화");
    }
  }

} // namespace DX11Base