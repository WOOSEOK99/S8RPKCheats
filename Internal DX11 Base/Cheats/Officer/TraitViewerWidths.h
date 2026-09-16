#pragma once

namespace DX11Base {
// CT 91200 + 91210: 무장 목록의 부대/신분 열 너비 보정.
// ON : UNIT 188 -> 132 -> 160, STATUS 104 -> 76
// OFF: UNIT 160 -> 132 -> 188, STATUS 76 -> 104
bool SetTraitViewerWidths(bool enable);
bool IsTraitViewerWidthsApplied();
} // namespace DX11Base
