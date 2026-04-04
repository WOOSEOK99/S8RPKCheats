#pragma once
#include <cstdint>

namespace DX11Base {
    // ───────────────────────────────────────────────
    //  월 주소 실시간 캡처 모듈
    // ───────────────────────────────────────────────
    
    // 후킹 및 캡처 스레드 시작/정지
    void SetMonthCapture(bool enable);

    // 현재 월 값 읽기 (0이면 아직 캡처 전)
    uint8_t GetCurrentMonth();

    // ───────────────────────────────────────────────
    //  설정 및 상태 플래그
    // ───────────────────────────────────────────────
    extern bool bMonthCapture;       // 사용자가 체크박스로 조절
    extern bool s_appMonthCapture;   // 내부 활성화 추적용
}
