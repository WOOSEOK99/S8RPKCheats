#pragma once
#include <cstdint>

namespace DX11Base {
    int8_t GetManbyeong();
    void SetManbyeong(int8_t value);
    void ApplySkillCondition(bool enable);

    void SetYumokgibeong(int8_t value);
    void ApplyYumokCondition(bool enable);

    void SetSangbyeong(int8_t value);
    void ApplySangbyeongCondition(bool enable);
}
