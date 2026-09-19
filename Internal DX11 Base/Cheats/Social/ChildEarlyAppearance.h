#pragma once

namespace DX11Base {
  // 자녀 처리 루틴을 감시하여 +0x08 무장 ID 기준으로 자녀 목록을 수집합니다.
  void EnsureChildManagerCapture();

  // 수집된 주소를 자녀 목록에 반영하고, 체크된 자녀의 임관 연도를 갱신합니다.
  void RunChildManagerUpdate();

  // 별도 자녀 관리 창.
  void DrawChildManagerWindow(float scale);
}
