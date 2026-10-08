#pragma once
#include <cstdint>

namespace DX11Base {
    void SetTengiCapture(bool enable);
    uint8_t GetTengi();
    void SetTengi(uint8_t value);
    void CancelTengi();
    uintptr_t GetTengiHookAddr();
    uintptr_t GetTengiHookOffset();
    uintptr_t GetCapturedTengiAddr();
    void TengiCave_Tick();
    void TickTengiListDurations(bool immediate = false);
}
