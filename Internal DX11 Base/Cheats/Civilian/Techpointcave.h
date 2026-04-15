#pragma once
#include <cstdint>
#include <atomic>

namespace DX11Base {
    extern uintptr_t g_capturedTechPAddr;
    extern std::atomic_bool g_techPThreadRunning;
    void SetTechPCapture(bool enable);
}