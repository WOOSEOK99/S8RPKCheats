#pragma once
// =============================================================================
// SpeedHack.h  –  게임 배속 조절 모듈
// 2026-04-04
//
// QueryPerformanceCounter를 후킹하여 게임이 인식하는 시간 흐름을 조절합니다.
// [롤백]: 이 .h/.cpp 삭제 + Source.cpp 훅 설치 2줄 + MenuSections UI 1줄 제거
// =============================================================================
#pragma once

namespace DX11Base {
    void SpeedHack_Install();   // 훅 설치 (최초 1회, Source.cpp::MainThread_Initialize 에서 호출)
    void SpeedHack_Update();    // 매 루프 상태 반영 (Menu::Loops 에서 호출)
} // namespace DX11Base
