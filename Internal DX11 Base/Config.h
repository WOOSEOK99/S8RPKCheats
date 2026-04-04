#pragma once
#include "pch.h"

namespace DX11Base {
    void SaveConfig();
    void LoadConfig();
    void ResetAppliedStates();
    void ApplyStoredConfigs(uintptr_t p1, uintptr_t gameBase);
    bool IsConfigReady();
}
