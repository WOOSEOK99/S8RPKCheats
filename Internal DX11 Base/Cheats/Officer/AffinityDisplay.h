#pragma once

namespace DX11Base {

extern bool bAffinityDisplay;

// CT 91930: 게임 원래 무장/도감 UI에 상성 항목을 노출합니다.
// 적용 전 모든 callsite/descriptor/관계 탭 배열을 검증하며,
// 하나라도 예상 상태와 다르면 메모리를 변경하지 않습니다.
bool SetAffinityDisplay(bool enable);
bool IsAffinityDisplayApplied();

} // namespace DX11Base
