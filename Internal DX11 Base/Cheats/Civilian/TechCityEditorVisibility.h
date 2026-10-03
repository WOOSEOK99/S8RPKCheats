#pragma once

namespace DX11Base {
  extern bool bTechCityEditorVisible;

  bool SetTechCityEditorVisible(bool enable);
  bool IsTechCityEditorVisibleApplied();
  void DrawTechCityEditorVisibilityUi();
} // namespace DX11Base
