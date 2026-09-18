#pragma once

namespace DX11Base {
  // CT ID 300: 경애 상태의 친밀도 증가량을 0x32(50)로 가속합니다.
  void SetFastRelationship(bool enable);

  // CT ID 321: 관계 갱신 시 해당 대상의 친밀도를 100으로 설정합니다.
  // 선물 경로 및 관계 메뉴를 열 때 목록 전체를 건드리는 CT ID 343은 포함하지 않습니다.
  void SetFastIntimacy(bool enable);
} // namespace DX11Base
