#pragma once

namespace DX11Base {

bool SetTraitViewerKirase(bool enable);
bool IsTraitViewerKiraseApplied();

bool SetTraitViewerInProgressEditor(bool enable);
bool IsTraitViewerInProgressEditorApplied();

// 진행 중 무장 편집기의 상위 dirty 판정 진단.
// 게임 데이터는 수정하지 않고, 12B6BAB 호출 시점의 편집 객체/무장 객체 상태만 로그합니다.
void SetInProgressTraitDirtyDiagnostics(bool enable);
bool IsInProgressTraitDirtyDiagnosticsEnabled();
void TickInProgressTraitDirtyDiagnostics();
void CaptureInProgressTraitDirtyBaseline();
void CompareInProgressTraitDirtyState();

bool SetTraitViewerBaseEditor(bool enable);
bool IsTraitViewerBaseEditorApplied();

} // namespace DX11Base
