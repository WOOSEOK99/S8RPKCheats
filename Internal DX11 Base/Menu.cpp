#include "pch.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "NotificationManager.h"
#include "MenuT04Maintenance.h"

// Keep CityInfoWindow declarations intact, then suppress only Menu.cpp's
// per-frame maintenance call inside the preserved implementation.
#define RunCityRevoltAlwaysZero() ((void)0)

// Menu::Render calls DrawMarqueeNotifications exactly once per render pass.
// Piggyback the 200ms maintenance gate there so the work stays on the render/UI
// thread but is no longer tied to DrawMenu/FPS or main-window visibility.
#define DrawMarqueeNotifications(scale) \
  (DX11Base::RunT04RenderMaintenance(), DrawMarqueeNotifications(scale))

#include "Menu_impl.inc"

#undef DrawMarqueeNotifications
#undef RunCityRevoltAlwaysZero
