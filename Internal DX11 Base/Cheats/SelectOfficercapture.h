#pragma once
#include "Framework/imgui.h"
#include "OfficerData.h"
#include <cstdint>

namespace DX11Base {
  extern uintptr_t g_capturedOfficerBase;
  extern bool g_officerCaptureRunning;
  void SetOfficerCapture(bool enable);
  void DrawSelectedOfficerWindow(ImVec2 mPos, ImVec2 mSize, float scale, bool asChild = false);

  void DrawOfficerListWindow(uintptr_t p1, float scale);
  void DrawOfficerTalents(uintptr_t pBase, float scale);
  void DrawOfficerHeader(uintptr_t pGame, float scale, uintptr_t pViewSnap = 0);
} // namespace DX11Base