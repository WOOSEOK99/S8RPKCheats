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

    // 확인된 +0x71E4의 연회 사용 bit1을 필요한 경우에만 해제합니다.
    // 상시 루프에서 호출하며 다른 비트는 보존합니다.
    bool TickInfiniteBanquet();

    // 디버그: 중개 사용 후보 +0x71E5 bit6만 1회 해제.
    // 실게임에서 재사용 가능 여부를 확인하기 전까지 정식 기능으로 취급하지 않습니다.
    bool ClearMediationUsedBitForTest();

    // ───────────────────────────────────────────────
    //  설정 및 상태 플래그
    // ───────────────────────────────────────────────
    extern bool bMonthCapture;       // 사용자가 체크박스로 조절
    extern bool s_appMonthCapture;   // 내부 활성화 추적용
}
