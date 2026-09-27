#pragma once
#include <cstdint>

namespace DX11Base {

    void debuging(uintptr_t gameBase, uintptr_t p1);
    void ShutdownDebugScannerT05();
    extern bool bShowDebug;
    extern bool bShowMemoryEditor;
}

