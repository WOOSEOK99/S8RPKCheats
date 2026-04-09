#pragma once
#include "pch.h"
#include <atomic>

namespace DX11Base {
    void SetSangeopCity(bool enable);
    extern std::atomic_bool g_sagCityThreadRunning;
}
