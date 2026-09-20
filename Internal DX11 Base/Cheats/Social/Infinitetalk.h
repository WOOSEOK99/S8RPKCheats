#pragma once

namespace DX11Base {
  void SetInfiniteTalk(bool enable);

  // 디버그: 검증된 기증 코드로 교류 상태 객체를 캡처한 뒤,
  // +0x320에 대한 실제 CPU 쓰기 명령을 하드웨어 감시점으로 추적.
  bool StartInteractionStateCaptureForWatch();
  bool StartInteractionWriteWatch();
  void LogInteractionWriteWatchResults();
  void StopInteractionWriteWatch();

} // namespace DX11Base
