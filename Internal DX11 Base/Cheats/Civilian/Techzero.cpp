#include "pch.h"
#include "TechZero.h"
#include "Cheats.h"
#include "showlog.h"
#include <atomic>
#include <mutex>
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

  static std::atomic<bool> g_techZeroEnabled{false};
  static bool g_techZeroDone = false;
  static bool g_techZeroPending = false;
  static DWORD g_techZeroTriggerTick = 0;
  static HANDLE g_techZeroThread = nullptr;
  static HANDLE g_techZeroStopEvent = nullptr;
  static std::mutex g_techZeroLifecycleMutex;

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
    static const uint8_t kZeros[5] = {};
    int changedCount = 0;

    for (int n = 0; n < k_techCount; ++n) {
      if (!g_techZeroEnabled.load(std::memory_order_acquire))
        return;

      uintptr_t addr = techBase + n * k_techStride;
      if (!IsValidPtr(addr, 5))
        continue;

      // 이미 모두 0이면 VirtualProtect/쓰기 자체를 생략합니다.
      if (memcmp((const void *)addr, kZeros, sizeof(kZeros)) == 0)
        continue;

      DWORD old = 0, tmp = 0;
      if (!VirtualProtect((LPVOID)addr, 5, PAGE_READWRITE, &old))
        continue;

      memcpy((void *)addr, kZeros, sizeof(kZeros));
      VirtualProtect((LPVOID)addr, 5, old, &tmp);
      ++changedCount;
    }

    if (g_techZeroEnabled.load(std::memory_order_acquire))
      AddLog(u8"[기술초기화] %d개 세력 기술 확인 / 실제 변경 %d개", k_techCount, changedCount);
  }

  // 타이머 스레드 (200ms 주기, stop event로 즉시 중단 가능)
  static DWORD WINAPI TechZeroTimerThread(LPVOID) {
    while (g_techZeroEnabled.load(std::memory_order_acquire)) {
      const DWORD waitResult = WaitForSingleObject(g_techZeroStopEvent, 200);
      if (waitResult == WAIT_OBJECT_0 ||
          !g_techZeroEnabled.load(std::memory_order_acquire)) {
        break;
      }
      if (waitResult == WAIT_FAILED) {
        AddLog(u8"[기술초기화] stop event 대기 실패. worker를 종료합니다.");
        break;
      }

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
        if (!g_techZeroEnabled.load(std::memory_order_acquire))
          break;

        ZeroAllFactionTech(rootBase);

        if (!g_techZeroEnabled.load(std::memory_order_acquire))
          break;

        g_techZeroDone = true;
        g_techZeroPending = false;
        g_techZeroTriggerTick = 0;
      }
    }

    return 0;
  }

  void SetTechZero(bool enable) {
    std::lock_guard<std::mutex> lifecycleLock(g_techZeroLifecycleMutex);

    if (enable) {
      if (g_techZeroEnabled.load(std::memory_order_acquire))
        return;

      // 이전 worker가 남아 있을 가능성을 먼저 정리합니다.
      if (g_techZeroThread) {
        WaitForSingleObject(g_techZeroThread, INFINITE);
        CloseHandle(g_techZeroThread);
        g_techZeroThread = nullptr;
      }
      if (g_techZeroStopEvent) {
        CloseHandle(g_techZeroStopEvent);
        g_techZeroStopEvent = nullptr;
      }

      g_techZeroStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!g_techZeroStopEvent) {
        g_techZeroRunning = false;
        AddLog(u8"[기술초기화] stop event 생성 실패");
        return;
      }

      g_techZeroDone = false;
      g_techZeroPending = false;
      g_techZeroTriggerTick = 0;
      g_techZeroEnabled.store(true, std::memory_order_release);

      g_techZeroThread = CreateThread(nullptr, 0, TechZeroTimerThread, nullptr, 0, nullptr);

      if (g_techZeroThread) {
        g_techZeroRunning = true;
        AddLog(u8"[기술초기화] 활성화 - 분기월 자동 초기화 시작");
      } else {
        g_techZeroEnabled.store(false, std::memory_order_release);
        CloseHandle(g_techZeroStopEvent);
        g_techZeroStopEvent = nullptr;
        g_techZeroRunning = false;
        AddLog(u8"[기술초기화] worker 생성 실패");
      }

    } else {
      if (!g_techZeroEnabled.load(std::memory_order_acquire) && !g_techZeroThread)
        return;

      g_techZeroEnabled.store(false, std::memory_order_release);

      // 기존 Sleep(200)을 기다리지 않고 즉시 worker를 깨웁니다.
      if (g_techZeroStopEvent)
        SetEvent(g_techZeroStopEvent);

      if (g_techZeroThread) {
        WaitForSingleObject(g_techZeroThread, INFINITE);
        CloseHandle(g_techZeroThread);
        g_techZeroThread = nullptr;
      }

      if (g_techZeroStopEvent) {
        CloseHandle(g_techZeroStopEvent);
        g_techZeroStopEvent = nullptr;
      }

      g_techZeroRunning = false;
      g_techZeroDone = false;
      g_techZeroPending = false;
      g_techZeroTriggerTick = 0;

      AddLog(u8"[기술초기화] 비활성화");
    }
  }

} // namespace DX11Base