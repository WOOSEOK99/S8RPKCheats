#pragma once

namespace DX11Base {
// CT 99000의 기재 화면 표시 기능을 런타임 패치로 적용/복구합니다.
// 실패 시 false를 반환하고 bTraitViewer 상태를 변경하지 않습니다.
bool SetTraitViewer(bool enable);
bool IsTraitViewerApplied();
} // namespace DX11Base
