#pragma once

namespace DX11Base {
  enum class DomesticRewardMode : int {
    Off = 0,
    Achievement200Top3 = 1,
    Achievement200AnyRank = 2,
  };

  bool SetDomesticRewardMode(DomesticRewardMode mode);
  DomesticRewardMode GetDomesticRewardMode();
  bool IsDomesticRewardHookApplied();
  void DrawDomesticRewardConditionUi(float scale);
} // namespace DX11Base
