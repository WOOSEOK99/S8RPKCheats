#pragma once

namespace DX11Base {
  void SetInfiniteTalk(bool enable);

  // 디버그: +0x320 bit8/bit9 직접 쓰기 명령 후보를 읽기 전용으로 검색
  void ScanExactDuelDebateWriteCandidates();

} // namespace DX11Base