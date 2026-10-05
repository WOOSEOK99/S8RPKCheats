#include "pch.h"
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/War/SpecialAbility.h"
#include "NotificationManager.h"
#include "MenuT04Maintenance.h"
#include "PerformanceDiagnostics.h"
#include "showlog.h"

namespace DX11Base {
  namespace {
    constexpr ULONGLONG kBattleMonitorNotReadyIntervalMs = 500;
    constexpr ULONGLONG kSpecialFallbackIntervalMs = 100;

    uintptr_t ResolveBattleFallbackChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
      uintptr_t current = base;
      for (uintptr_t offset : offsets) {
        if (!current || !IsValidPtr(current, sizeof(uintptr_t)))
          return 0;
        uintptr_t next = 0;
        __try {
          next = *reinterpret_cast<uintptr_t *>(current);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          return 0;
        }
        if (next < 0x10000)
          return 0;
        current = next + offset;
      }
      return current;
    }

    bool ShouldRunFullBattleMonitor() {
      static ULONGLONG s_lastNotReadyRun = 0;

      // Once the native runtime has passed readiness, preserve the original
      // monitor cadence. The expensive readiness scan is only throttled while
      // the runtime is still not ready.
      if (IsBattleRuntimeReady()) {
        s_lastNotReadyRun = 0;
        return true;
      }

      const ULONGLONG now = GetTickCount64();
      if (s_lastNotReadyRun == 0 || now - s_lastNotReadyRun >= kBattleMonitorNotReadyIntervalMs) {
        s_lastNotReadyRun = now;
        return true;
      }
      return false;
    }

    void RunSpecialAbilityReadinessFallback() {
      static bool s_fallbackActive = false;
      static uintptr_t s_cachedUnitList = 0;
      static int s_cachedUnitCount = 0;
      static ULONGLONG s_lastFallbackTick = 0;

      if (IsBattleRuntimeReady()) {
        s_fallbackActive = false;
        s_cachedUnitList = 0;
        s_cachedUnitCount = 0;
        s_lastFallbackTick = 0;
        return;
      }

      // The menu/background loop is faster than the intended battle monitor
      // cadence. Keep the recovery path responsive without re-running all
      // pointer chains and special-ability checks every loop iteration.
      const ULONGLONG now = GetTickCount64();
      if (s_lastFallbackTick != 0 && now - s_lastFallbackTick < kSpecialFallbackIntervalMs)
        return;
      s_lastFallbackTick = now;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return;

      const uintptr_t unitListBase = ResolveBattleFallbackChain(
          exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});
      const uintptr_t dayBaseAddr = ResolveBattleFallbackChain(
          exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0});

      int unitCount = 0;
      int currentDay = -1;
      bool validBattle = false;
      __try {
        if (unitListBase > 0x10000 && dayBaseAddr > 0x10000 &&
            IsValidPtr(unitListBase - 0x08, 1) && IsValidPtr(dayBaseAddr + 0x28, 1)) {
          unitCount = static_cast<int>(*reinterpret_cast<unsigned char *>(unitListBase - 0x08));
          currentDay = static_cast<int>(*reinterpret_cast<unsigned char *>(dayBaseAddr + 0x28));
          validBattle = unitCount > 0 && unitCount <= 60 && currentDay >= 1 && currentDay <= 30;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        validBattle = false;
      }

      if (!validBattle) {
        if (s_fallbackActive) {
          UpdateSpecialAbilities(0, 0, exeBase);
          ClearBattleCache();
          AddLog(u8"[BattleFallback] readiness fallback 종료");
        }
        s_fallbackActive = false;
        s_cachedUnitList = 0;
        s_cachedUnitCount = 0;
        return;
      }

      if (!s_fallbackActive || s_cachedUnitList != unitListBase || s_cachedUnitCount != unitCount) {
        InitializeBattleCache(unitCount, unitListBase, exeBase);
        s_cachedUnitList = unitListBase;
        s_cachedUnitCount = unitCount;
        s_fallbackActive = true;
        AddLog(u8"[BattleFallback] readiness 미완료 상태에서 특수능력 캐시 복구: Units=%d Day=%d",
               unitCount, currentDay);
      }

      UpdateSpecialAbilities(unitCount, unitListBase, exeBase);
    }
  } // namespace

  static void ProfiledMonitorBattleStatus() {
    if (ShouldRunFullBattleMonitor()) {
      if (!PerfDiagnosticsEnabled()) {
        MonitorBattleStatus();
      } else {
        PerfScope perf(PerfMetric::BattleMonitorTotal);
        MonitorBattleStatus();
      }
    }

    RunSpecialAbilityReadinessFallback();
  }
} // namespace DX11Base

// Keep declarations intact, then suppress only the preserved Menu.cpp
// per-frame maintenance calls. MenuT04Maintenance.h was included above, so it
// still sees the real bZeroInfamy variable before the temporary macro below.
#define RunCityRevoltAlwaysZero() ((void)0)
#define bZeroInfamy false

// Menu::Render calls DrawMarqueeNotifications exactly once per render pass.
// Piggyback the 200ms maintenance gate there so the work stays on the render/UI
// thread but is no longer tied to DrawMenu/FPS or main-window visibility.
#define DrawMarqueeNotifications(scale) \
  (DX11Base::RunT04RenderMaintenance(), DrawMarqueeNotifications(scale))

#define MonitorBattleStatus ProfiledMonitorBattleStatus
#include "Menu_impl.inc"
#undef MonitorBattleStatus

#undef DrawMarqueeNotifications
#undef bZeroInfamy
#undef RunCityRevoltAlwaysZero
