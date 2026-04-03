#pragma once
#include <cstdint>

namespace DX11Base {
    extern uintptr_t g_capturedTechPAddr;
    extern bool g_techPThreadRunning;
    void SetTechPCapture(bool enable);
}