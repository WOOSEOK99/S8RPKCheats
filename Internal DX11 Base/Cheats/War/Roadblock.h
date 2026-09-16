#pragma once

namespace DX11Base {
  extern bool g_roadBlockRunning;
  extern bool bAIWarImprove;
  void SetAIWarImprove(bool enable);
  void DrawAIWarImproveSection(float scale);
  void SetRoadBlock(bool enable);
  void SetRoadBlock2(bool enable);
} // namespace DX11Base
