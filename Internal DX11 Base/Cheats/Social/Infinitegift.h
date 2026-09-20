#pragma once

namespace DX11Base {
  void SetInfiniteGift(bool enable);

  // 디버그: 실제 기증 코드의 RSI에서 교류 상태 객체를 캡처
  bool StartGiftInteractionCapture();
  void LogGiftInteractionCaptureResult();
  void StopGiftInteractionCapture();

} // namespace DX11Base