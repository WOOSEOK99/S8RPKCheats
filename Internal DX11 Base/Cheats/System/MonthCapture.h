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
    // 현재 버전에서 검증 중인 시나리오 데이터 인스턴스 주소를 반환합니다.
    // 다른 데이터 구조 진단 시 같은 베이스를 재사용하기 위한 읽기 전용 helper입니다.
    uintptr_t GetScenarioDataCenterAddress();

    // 옛 CT의 "현재년도 - 0x1C, bit 1" 관계를 현재 시나리오 데이터에 대입한
    // 연회 후보 값을 읽기 전용으로 로그에 출력합니다. 메모리 쓰기는 하지 않습니다.
    bool LogBanquetFlagCandidate();

    // ───────────────────────────────────────────────
    //  설정 및 상태 플래그
    // ───────────────────────────────────────────────
    extern bool bMonthCapture;       // 사용자가 체크박스로 조절
    extern bool s_appMonthCapture;   // 내부 활성화 추적용
}
