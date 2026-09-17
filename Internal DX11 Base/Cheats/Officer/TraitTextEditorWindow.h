#pragma once

namespace DX11Base {

void OpenTraitTextEditorWindow();
void DrawTraitTextEditorWindow(float scale);
bool IsTraitTextEditorWindowOpen();
// Step 6: 저장된 trait_texts.ini를 게임 준비 후 한 번 자동 적용합니다.
void TickTraitTextEditorAutoApply();

} // namespace DX11Base
