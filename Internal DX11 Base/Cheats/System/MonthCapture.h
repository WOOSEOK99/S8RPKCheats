#pragma once
#include <cstdint>

namespace DX11Base {
    // 현재 날짜의 단일 기준은 시나리오 데이터입니다.
    // SAN8RPK.exe+0x2E98BC8 -> 인스턴스 +0x72D0(년), +0x72D2(월)

    // 현재 월 직접 조회 활성화/비활성화. 별도 후킹이나 캡처 스레드는 사용하지 않습니다.
    void SetMonthCapture(bool enable);

    // 시나리오 데이터의 현재 월 값. 0이면 아직 읽을 수 없는 상태입니다.
    uint8_t GetCurrentMonth();

    void UpdateYear(unsigned short targetYear);
    void UpdateMonth(uint8_t targetMonth);
    bool ReadScenarioYear(unsigned short *outYear);
    bool ReadScenarioMonth(uint8_t *outMonth);
    // 한 번의 포인터 해석으로 연·월 동시 읽기 (UI 폴링용)
    bool ReadScenarioDate(unsigned short *outYear, uint8_t *outMonth);
    // 현재 시나리오 데이터 인스턴스 주소를 반환하는 읽기 전용 helper입니다.
    uintptr_t GetScenarioDataCenterAddress();

    // 확인된 +0x71E4의 연회 사용 bit1을 필요한 경우에만 해제합니다.
    // 상시 루프에서 호출하며 다른 비트는 보존합니다.
    bool TickInfiniteBanquet();

    // 실게임에서 확인된 +0x71E5 bit6 중개 사용 완료 플래그를 지속 해제합니다.
    // 다른 비트는 보존합니다.
    bool TickInfiniteMediation();

    extern bool bMonthCapture;
    extern bool s_appMonthCapture;
}
