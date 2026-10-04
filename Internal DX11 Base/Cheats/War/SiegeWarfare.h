#pragma once
#include "../../pch.h"

namespace DX11Base {
    void ResetSiegeBattleRuntime();
    void SetSiegeWarfare(bool enable);
    void UpdateSiegeWarfare(uintptr_t dayAddr, int unitCount, uint8_t defenderForce, uintptr_t unitListBase);
    bool IsSiegeBattleActive();
    void SetSiegeWarfare2(bool enable);
    void UpdateSiegeWarfare2(uintptr_t dayAddr, int unitCount, uint8_t defenderForce, uintptr_t unitListBase);
}
