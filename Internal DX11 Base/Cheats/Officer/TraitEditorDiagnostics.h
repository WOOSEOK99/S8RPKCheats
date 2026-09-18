#pragma once

namespace DX11Base {

// 진행 중 게임 원본 편집기의 기재 1/2/3 선택이 실제 무장 슬롯에
// 언제 반영되는지 확인하기 위한 임시 진단 기능입니다.
// 게임 동작/데이터는 변경하지 않고 읽기만 합니다.
void SetInProgressTraitDiagnostics(bool enable);
bool IsInProgressTraitDiagnosticsEnabled();
void TickInProgressTraitDiagnostics();

} // namespace DX11Base
