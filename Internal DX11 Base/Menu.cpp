#include "pch.h"
#include "BattleMonitor.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "NotificationManager.h"
#include "MenuT04Maintenance.h"
#include "PerformanceDiagnostics.h"

namespace DX11Base {
  static void ProfiledMonitorBattleStatus() {
    if (!PerfDiagnosticsEnabled()) {
      MonitorBattleStatus();
      return;
    }

    PerfScope perf(PerfMetric::BattleMonitorTotal);
    MonitorBattleStatus();
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