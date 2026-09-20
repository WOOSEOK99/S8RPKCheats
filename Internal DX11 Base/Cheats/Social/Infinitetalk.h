#pragma once

namespace DX11Base {
  void SetInfiniteTalk(bool enable);
  void SetInfiniteDuel(bool enable);
  void SetInfiniteDebate(bool enable);
  // 디버그: 보주 하위 기능이 공통 교류 +0x320 상태를 쓰는지 추적
  bool StartJewelSubactionStateCapture();
  bool CaptureJewelMediationSnapshot();
  void CompareJewelMediationSnapshot();
  void ResetJewelMediationSnapshot();
  bool StartJewelSubactionWriteWatch();
  void LogJewelSubactionWriteWatchResults();
  void StopJewelSubactionWriteWatch();
} // namespace DX11Base
