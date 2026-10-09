#pragma once

namespace DX11Base {
  // 결전(TotalWarPoint)의 재발생 대기 입력값만 1년으로 override합니다.
  // 다른 native 결전 발생 조건은 그대로 유지합니다.
  bool SetTotalWarCycleShortening(bool enable);
  bool IsTotalWarCycleShorteningApplied();
} // namespace DX11Base
