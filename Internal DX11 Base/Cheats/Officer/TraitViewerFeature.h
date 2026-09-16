#pragma once

namespace DX11Base {
// 기재 화면 보이기 전체 토글. 각 하위 패치를 순차 적용하며 실패 시 롤백합니다.
bool SetTraitViewerFeature(bool enable);
bool IsTraitViewerFeatureApplied();
} // namespace DX11Base
