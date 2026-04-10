#pragma once
#include "pch.h"

namespace DX11Base {
    extern bool g_domesticsEnabled;

    void SetDomesticsMult(bool enable);
    void SetDomesticsMultiplier(float playerMult, float forceMult);
    void ResetDomesticsPlayer();
}
