#pragma once
#include "pch.h"
#include <atomic>


namespace DX11Base {
    void SetBigCityConvert(bool enable);
    extern std::atomic_bool g_bigCityThreadRunning;
}