#include "../../pch.h"
#include "SystemMonth.h"
#include "MonthCapture.h"
#include "../../showlog.h"

namespace DX11Base {

    void InstallSystemMonthHook() {
        static bool s_logged = false;
        if (s_logged)
            return;
        s_logged = true;
        AddLog(u8"[SystemMonth] 시나리오 월 경로 사용: 별도 캡처 후크 생략");
    }

    uint8_t GetSystemMonthValue() {
        return GetCurrentMonth();
    }

} // namespace DX11Base
