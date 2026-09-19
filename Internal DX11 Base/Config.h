#pragma once
#include "pch.h"

namespace DX11Base {
    std::string GetConfigPath();
    void SaveConfig();
    void LoadConfig();
    void LoadEarlyLogConfig();
    void ResetAppliedStates();
    void ApplyStoredConfigs(uintptr_t p1, uintptr_t gameBase);
    bool IsConfigReady();
}
