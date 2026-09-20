#pragma once

namespace DX11Base {
  void SetInfiniteGift(bool enable);

  // 디버그: 현재 기증 +0x320 코드 위치를 기준으로
  // 옛 CT의 상대거리에서 대련/토론 후보 주변 바이트만 읽습니다.
  void LogDuelDebateNearGiftCandidates();

} // namespace DX11Base
