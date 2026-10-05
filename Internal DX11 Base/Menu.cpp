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
    constexpr ULONGLONG kSpecialFallbackActiveIntervalMs = 1000;
    constexpr ULONGLONG kSpecialFallbackResolveRefreshMs = 500;

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
      static uintptr_t s_cachedGameBase = 0;
      static uintptr_t s_cachedUnitList = 0;
      static uintptr_t s_cachedDayBase = 0;
      static int s_cachedUnitCount = 0;
      static ULONGLONG s_lastFallbackTick = 0;
      static ULONGLONG s_lastResolveTick = 0;

      if (IsBattleRuntimeReady()) {
        s_fallbackActive = false;
        s_cachedGameBase = 0;
        s_cachedUnitList = 0;
        s_cachedDayBase = 0;
        s_cachedUnitCount = 0;
        s_lastFallbackTick = 0;
        s_lastResolveTick = 0;
        return;
      }

      // Keep battle detection responsive before the fallback cache is ready.
      // Once active, UpdateSpecialAbilities can be expensive, so avoid running
      // it back-to-back while preserving at most ~1s state-change latency.
      const ULONGLONG now = GetTickCount64();
      const ULONGLONG fallbackInterval =
          s_fallbackActive ? kSpecialFallbackActiveIntervalMs : kSpecialFallbackIntervalMs;
      if (s_lastFallbackTick != 0 && now - s_lastFallbackTick < fallbackInterval)
        return;
      s_lastFallbackTick = now;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return;

      const uintptr_t gameBase = DX11Base::GetGameBase();
      if (!gameBase) {
        if (s_fallbackActive) {
          UpdateSpecialAbilities(0, 0, exeBase);
          ClearBattleCache();
        }
        s_fallbackActive = false;
        s_cachedGameBase = 0;
        s_cachedUnitList = 0;
        s_cachedDayBase = 0;
        s_cachedUnitCount = 0;
        s_lastResolveTick = 0;
        return;
      }

      // A new save generation gets a different gameBase/P1. Drop all fallback
      // addresses immediately so no old-save pointer survives into the new one.
      if (s_cachedGameBase != gameBase) {
        if (s_fallbackActive) {
          UpdateSpecialAbilities(0, 0, exeBase);
          ClearBattleCache();
        }
        s_fallbackActive = false;
        s_cachedGameBase = gameBase;
        s_cachedUnitList = 0;
        s_cachedDayBase = 0;
        s_cachedUnitCount = 0;
        s_lastResolveTick = 0;
      }

      uintptr_t unitListBase = s_cachedUnitList;
      uintptr_t dayBaseAddr = s_cachedDayBase;

      bool cachedPointersReadable = false;
      const bool cachedPointersPresent = unitListBase > 0x10000 && dayBaseAddr > 0x10000;
      if (cachedPointersPresent) {
        __try {
          cachedPointersReadable = IsValidPtr(unitListBase - 0x08, 1) &&
                                   IsValidPtr(dayBaseAddr + 0x28, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          cachedPointersReadable = false;
        }
      }

      // Full chain refresh is periodic, or immediate only when a previously
      // usable cached address becomes unreadable. In normal battle runtime this
      // cuts the two long chain walks from ~10/sec to ~2/sec.
      const bool refreshPointers =
          s_lastResolveTick == 0 ||
          now - s_lastResolveTick >= kSpecialFallbackResolveRefreshMs ||
          (cachedPointersPresent && !cachedPointersReadable);

      if (refreshPointers) {
        const uintptr_t resolvedUnitList = ResolveBattleFallbackChain(
            exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});
        const uintptr_t resolvedDayBase = ResolveBattleFallbackChain(
            exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0});
        s_lastResolveTick = now;

        if (resolvedUnitList != s_cachedUnitList || resolvedDayBase != s_cachedDayBase) {
          if (s_fallbackActive) {
            UpdateSpecialAbilities(0, 0, exeBase);
            ClearBattleCache();
          }
          s_fallbackActive = false;
          s_cachedUnitCount = 0;
          s_cachedUnitList = resolvedUnitList;
          s_cachedDayBase = resolvedDayBase;
        }

        unitListBase = s_cachedUnitList;
        dayBaseAddr = s_cachedDayBase;
      }

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
        s_cachedUnitCount = 0;
        return;
      }

      if (!s_fallbackActive || s_cachedUnitCount != unitCount) {
        InitializeBattleCache(unitCount, unitListBase, exeBase);
        s_cachedUnitCount = unitCount;
        s_fallbackActive = true;
        AddLog(u8"[BattleFallback] readiness 미완료 상태에서 특수능력 캐시 복구: Units=%d Day=%d",
               unitCount, currentDay);
      }

      UpdateSpecialAbilities(unitCount, unitListBase, exeBase);
    }

    void ProfiledSpecialAbilityReadinessFallback() {
      if (!PerfDiagnosticsEnabled()) {
        RunSpecialAbilityReadinessFallback();
        return;
      }

      static uint64_t s_calls = 0;
      static uint64_t s_total100ns = 0;
      static uint64_t s_max100ns = 0;
      static uint64_t s_nextReport100ns = 0;

      const uint64_t start = PerfRealNow100ns();
      RunSpecialAbilityReadinessFallback();
      const uint64_t end = PerfRealNow100ns();
      const uint64_t elapsed = end >= start ? end - start : 0;

      ++s_calls;
      s_total100ns += elapsed;
      if (elapsed > s_max100ns)
        s_max100ns = elapsed;

      constexpr uint64_t kReportInterval100ns = 5ull * 1000ull * 1000ull * 10ull;
      if (s_nextReport100ns == 0) {
        s_nextReport100ns = end + kReportInterval100ns;
        return;
      }
      if (end < s_nextReport100ns)
        return;

      const double totalMs = static_cast<double>(s_total100ns) / 10000.0;
      const double avgUs = s_calls ? (static_cast<double>(s_total100ns) / static_cast<double>(s_calls)) / 10.0 : 0.0;
      const double maxUs = static_cast<double>(s_max100ns) / 10.0;
      AddLog("[Perf:T04] BattleFallback calls=%llu total=%.3fms avg=%.2fus max=%.2fus",
             static_cast<unsigned long long>(s_calls), totalMs, avgUs, maxUs);

      s_calls = 0;
      s_total100ns = 0;
      s_max100ns = 0;
      s_nextReport100ns = end + kReportInterval100ns;
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

    ProfiledSpecialAbilityReadinessFallback();
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
