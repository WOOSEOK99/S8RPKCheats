#pragma once

namespace DX11Base {
  // SAN8RPK AI V2.0의 AI 포로 석방/처형 판정식을 적용합니다.
  // 상성, 의리, 군주 여부, 장수 관계, 세력 관계를 반영합니다.
  bool SetAIExecutionConditionChange(bool enable);
  bool IsAIExecutionConditionChangeApplied();
} // namespace DX11Base
