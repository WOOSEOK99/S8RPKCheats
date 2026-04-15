#pragma once
// =============================================================================
// RoninMonitor.h  –  재야 장수 자동 감시 모듈
// 2026-04-04
//
// [설계 원칙]
//   - SelectOfficercapture.cpp 를 일절 수정하지 않습니다.
//   - 주인공 포인터(p1) + 주인공 ID를 이용해 무장 배열 시작점을 자동 계산합니다.
//   - Config::IsConfigReady() (3초 지연)가 통과된 후부터만 작동합니다.
//   - 롤백 방법: 이 .h/.cpp 파일 제거 + Menu.cpp, Engine.cpp 호출 2줄 주석 처리
// =============================================================================

#include <cstdint>

namespace DX11Base {

    // Menu::Loops() 백그라운드 스레드에서 호출: 주소 자동 확보 + 재야 감시
    void RoninMonitor_Tick(uintptr_t p1);

    // Engine.cpp 렌더링 루프에서 호출: 알림창 그리기 (ImGui)
    void RoninMonitor_Draw();

    // 외부(치트 메뉴 등)에서 수동으로 상태 변경 시 알림 중복 방지를 위한 동기화
    void RoninMonitor_UpdatePrevStatus(int officerID, uint8_t newStatus);

} // namespace DX11Base
