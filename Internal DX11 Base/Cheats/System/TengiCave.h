#pragma once
#include <cstdint>

namespace DX11Base {
    void SetTengiCapture(bool enable);
    uint8_t GetTengi();
    void SetTengi(uint8_t value);
    uintptr_t GetTengiHookAddr();
    uintptr_t GetTengiHookOffset();
    uintptr_t GetCapturedTengiAddr();
    void TengiCave_Tick();
    void TickTengiCycle(uintptr_t sessionP1);
    void TickTengiListDurations(bool immediate = false);
    int GetTengiListCooldownYears(int index); // -1=기본, 0=대기 없음
    void SetTengiListCooldownYears(int index, int years);
}
