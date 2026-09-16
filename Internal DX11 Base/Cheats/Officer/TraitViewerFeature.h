#pragma once

namespace DX11Base {
// 전체 기재 표시 기능을 CT의 의존 순서에 맞춰 적용/복구합니다.
// 실패 시 이미 변경된 하위 기능을 가능한 범위에서 역순 롤백합니다.
bool SetTraitViewerFeature(bool enable);
bool IsTraitViewerFeatureApplied();
} // namespace DX11Base
