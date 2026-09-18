#pragma once

namespace DX11Base {
  // CT ID 321: 관계 갱신 중인 교류 대상의 친밀도를 100으로 처리합니다.
  // 관계 메뉴 조회 시 목록 전체를 건드리는 CT ID 343은 포함하지 않습니다.
  void SetFastRelationship(bool enable);
} // namespace DX11Base
