#pragma once
#include "pch.h"
#include <atomic>

namespace DX11Base {
    void SetNonggyeongCity(bool enable);
    extern std::atomic_bool g_nongCityThreadRunning;
}
