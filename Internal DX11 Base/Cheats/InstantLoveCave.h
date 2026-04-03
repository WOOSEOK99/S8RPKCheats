#pragma once
#include <cstdint>

namespace DX11Base {

    extern uintptr_t g_hook1Addr;
    extern bool g_cave1Applied;
    extern bool g_cave2Applied;
    extern uint32_t g_dynOffset;

    uintptr_t GetCapturedLoveAddr();
    uintptr_t GetCapturedAddr();

    enum class LoveMode { Normal, HateIgnore };
    void SetInstantLoveCave(bool enable, DX11Base::LoveMode mode = DX11Base::LoveMode::Normal);
    uintptr_t AllocNear(uintptr_t target, size_t size);
    bool ApplyJmp(uintptr_t hookAddr, uintptr_t caveAddr, size_t hookSize);
    void RestoreBytes(uintptr_t hookAddr, const uint8_t *original, size_t hookSize);
}