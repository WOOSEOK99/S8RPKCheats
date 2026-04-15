#pragma once
#include <cstdint>

namespace DX11Base {
    // 2025-04-05 시스템 월 값 (신규 AOB 방식)
    // 패턴: 88 48 6C 41 C7 06 01 00 00 00 (mov [rax+6c], cl)
    void InstallSystemMonthHook();
    uint8_t GetSystemMonthValue();
}
