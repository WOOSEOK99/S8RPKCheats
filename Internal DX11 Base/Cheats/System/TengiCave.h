#pragma once
#include <cstdint>

namespace DX11Base {
    void TengiCave_Tick();
    void TickTengiCycle(uintptr_t sessionP1);
    void TickTengiListDurations(bool immediate = false);
    int GetTengiListCooldownYears(int index); // -1=기본, 0=대기 없음
    void SetTengiListCooldownYears(int index, int years);
}
