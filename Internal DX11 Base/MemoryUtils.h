#pragma once
#include <windows.h>
#include <cstdint>

namespace DX11Base {
    // Allocates memory near a target address (within 2GB) for JMP instructions
    uintptr_t AllocNear(uintptr_t target, size_t size);

    // Applies a 5-byte JMP (E9) from hookAddr to caveAddr
    bool ApplyJmp(uintptr_t hookAddr, uintptr_t caveAddr, size_t hookSize);

    // Restores original bytes at hookAddr
    void RestoreBytes(uintptr_t hookAddr, const uint8_t* original, size_t size);
}
