#pragma once
#include <cstdint>

namespace DX11Base {
    // 2026-04-04 평정(Council) 상태 감지용 시스템 월 값 추출
    // 포인터 체인: "SAN8RPK.exe" + 034C8630 -> 0 -> 8 -> 10 -> 0 -> 49EC94
    uint8_t GetSystemMonthValue();

    // 2026-04-04 실시간 월 값 (AOB 스캔 및 후킹 방식)
    // 패턴: 88 86 D2 72 00 00 (mov [rsi+72D2], al)
    void InstallRealMonthHook();
    uint8_t GetRealMonthValue();
}
