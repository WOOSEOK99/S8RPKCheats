#pragma once

namespace DX11Base {
  // CT ID 366 기반 테스트 버전. 등장년도만 현재년도 + N년으로 앞당깁니다.
  // 출생년도/사망년도는 수정하지 않습니다.
  void SetChildEarlyAppearance(bool enable);

  // 자녀 처리 훅에서 수집한 레코드 주소의 후보 ID/연도 필드를 로그로 출력합니다.
  // 같은 주소는 한 번만 출력합니다.
  void RunChildDebugLog();
}
