#pragma once

namespace DX11Base {
  // SAN8RPK AI V2.0:
  // AI 항복권고 처리에서 의리가 높은 군주의 항복 거부를 강화합니다.
  // 원본 16바이트가 정확히 일치할 때만 hook을 설치합니다.
  bool SetAIHighHonorSurrenderFix(bool enable);
  bool IsAIHighHonorSurrenderFixApplied();
} // namespace DX11Base
