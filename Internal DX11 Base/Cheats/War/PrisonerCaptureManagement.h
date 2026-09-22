#pragma once

namespace DX11Base {
  // 부장 포획 + 고립도시 함락 포획 + AI 포로 처형조건 변경을
  // 하나의 사용자 옵션으로 관리합니다.
  bool SetPrisonerCaptureManagement(bool enable);
  bool IsPrisonerCaptureManagementApplied();
} // namespace DX11Base
