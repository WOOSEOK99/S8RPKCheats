#pragma once

namespace DX11Base {
  // V2.0 포로 처리 개선 Step 2:
  // 함락 도시와 인접한 패배 세력 소유 도시가 하나도 없으면,
  // 그 도시에 남아 있는 패배 세력 장수를 기존 native 포로 목록에 추가합니다.
  //
  // 별도 UI 토글 없이 기본 적용하며, 원본 바이트가 정확히 일치할 때만
  // 안전하게 hook을 설치합니다.
  bool InstallIsolatedCityCaptureFix();
  bool UninstallIsolatedCityCaptureFix();
  bool IsIsolatedCityCaptureFixApplied();
} // namespace DX11Base
