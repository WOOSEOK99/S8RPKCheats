#pragma once

namespace DX11Base {
  void SetInfiniteGift(bool enable);

  // 디버그: 기증 원본 코드에서 공통 교류 상태 객체(RSI)를 1회 캡처한 뒤
  // 훅을 즉시 원복하고 +0x320 행동별 비트를 읽습니다.
  bool StartInteractionStateCaptureFromGift();
  void LogCapturedInteractionState();
  void CancelInteractionStateCapture();

} // namespace DX11Base
