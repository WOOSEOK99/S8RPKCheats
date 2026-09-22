#pragma once

namespace DX11Base {
  // V2.0 포로 처리 개선 Step 1:
  // 총대장 네이티브 포획 성공 시 같은 부대의 부장 최대 2명에도
  // 동일한 포획 결과를 적용합니다.
  //
  // 별도 UI 토글 없이 기본 적용하며, 원본 바이트가 정확히 일치할 때만
  // 안전하게 hook을 설치합니다.
  bool InstallDeputyCaptureFix();
  bool UninstallDeputyCaptureFix();
  bool IsDeputyCaptureFixApplied();
} // namespace DX11Base
