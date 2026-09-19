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

    // 시나리오 날짜(정적 포인터 → 인스턴스 + 오프셋). 월 캡처 후킹과 동일 베이스의 0x72D2 사용.
    void UpdateYear(unsigned short targetYear);
    void UpdateMonth(uint8_t targetMonth);
    bool ReadScenarioYear(unsigned short *outYear);
    bool ReadScenarioMonth(uint8_t *outMonth);
    // 한 번의 포인터 해석으로 연·월 동시 읽기 (UI 폴링용)
    bool ReadScenarioDate(unsigned short *outYear, uint8_t *outMonth);

    // 구 CT의 현재 연도 기준 구조:
    // +0x14 시나리오 ID, +0x16 시작 연도, +0x18 시작 월.
    // 값 범위까지 검사하며, 자동 기능의 세이브/시나리오 오적용 방지용으로 사용합니다.
    bool ReadScenarioIdentity(uint8_t *outScenarioId,
                              unsigned short *outStartYear,
                              uint8_t *outStartMonth);

    // ───────────────────────────────────────────────
    //  설정 및 상태 플래그
    // ───────────────────────────────────────────────
    extern bool bMonthCapture;       // 사용자가 체크박스로 조절
    extern bool s_appMonthCapture;   // 내부 활성화 추적용
}
