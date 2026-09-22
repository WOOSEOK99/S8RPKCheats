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
#include <string>
#include <vector>

namespace DX11Base {

    // Menu::Loops() 백그라운드 스레드에서 호출: 주소 자동 확보 + 재야 감시
    void RoninMonitor_Tick(uintptr_t p1);

    // Engine.cpp 렌더링 루프에서 호출: 알림창 그리기 (ImGui)
    void RoninMonitor_Draw();

    // 외부(치트 메뉴 등)에서 수동으로 상태 변경 시 알림 중복 방지를 위한 동기화
    void RoninMonitor_UpdatePrevStatus(int officerID, uint8_t newStatus);

    // 연말 자동 특수능력 부여 결과 팝업.
    // 재야 알림 체크 여부와 무관하게 표시되며, 재야 알림이 실제 표시 중이면 뒤에서 대기합니다.
    void RoninMonitor_QueueSpecialAbilityNotice(const std::vector<std::string>& lines);

    // 기존 RoninMonitor 팝업을 다른 기능에서도 공용으로 사용합니다.
    // 재야 알림이 실제 표시 중이면 뒤에서 대기하며, 별도의 ImGui 창은 만들지 않습니다.
    void RoninMonitor_QueueSharedNotice(
        const std::string& title,
        const std::vector<std::string>& lines);

} // namespace DX11Base
