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

    // 시나리오 데이터 +0x6000~+0x8000 영역을 연회 전/후 읽기 전용으로 비교합니다.
    bool CaptureBanquetDiffBaseline();
    bool CompareBanquetDiffAfter();

    // 실측된 연회 상태 +0x71E4의 bit1만 1회 해제합니다.
    // 다른 비트는 보존하며 readback으로 검증합니다.
    bool ClearBanquetUsedBitForTest();

    // 옛 CT의 실제 무제한 연회 필드(+0x6EA4)와 현재년도(+0x6F78)의
    // 상대차이 0xD4를 현재 구조에 대입한 후보 값을 읽기 전용으로 출력합니다.
    bool LogBanquetFlagCandidate();

    // 현재 담화/기증 AOB를 기준으로 옛 CT의 상대거리를 적용하고,
    // 예측 지점 주변의 bit 1 OR/AND 코드 후보를 읽기 전용으로 검색합니다.
    bool ScanBanquetCodeCandidates();

    // ───────────────────────────────────────────────
    //  설정 및 상태 플래그
    // ───────────────────────────────────────────────
    extern bool bMonthCapture;       // 사용자가 체크박스로 조절
    extern bool s_appMonthCapture;   // 내부 활성화 추적용
}
