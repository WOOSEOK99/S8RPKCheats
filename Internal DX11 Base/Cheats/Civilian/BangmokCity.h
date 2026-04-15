#pragma once
#include "pch.h"

#include <atomic>

namespace DX11Base {
    void SetBangmokCity(bool enable);
    extern std::atomic_bool g_bangmokThreadRunning;
}
