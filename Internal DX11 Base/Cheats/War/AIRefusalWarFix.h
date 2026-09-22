#pragma once

namespace DX11Base {
  // SAN8RPK AI V2.0:
  // 항복권고 실패 후 3개월 동안 같은 대상 재권고를 막고,
  // 해당 세력을 공격 후보로 평가할 때 우선도를 크게 높입니다.
  //
  // 기존 +144D24C 2-byte 패치를 대체하는 V2.0 공격 cave를 포함합니다.
  bool SetAIRefusalWarFix(bool enable);
  bool IsAIRefusalWarFixApplied();
} // namespace DX11Base
