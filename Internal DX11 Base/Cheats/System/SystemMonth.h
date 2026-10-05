#pragma once
#include <cstdint>

namespace DX11Base {
    // 호환 API. 현재 월의 단일 기준은 MonthCapture의 시나리오 날짜 경로입니다.
    // 별도 SystemMonth 후크는 설치하지 않습니다.
    void InstallSystemMonthHook();
    uint8_t GetSystemMonthValue();
}
