#include "../../pch.h"
#include "MonthCapture.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"

namespace DX11Base {

    namespace {
        // 현재 날짜의 단일 기준: 시나리오 데이터 블록.
        constexpr uintptr_t kScenarioInstanceStaticOffset = 0x2E98BC8;
        constexpr uintptr_t kScenarioYearOffset = 0x72D0;
        constexpr uintptr_t kScenarioMonthOffset = 0x72D2;

        // 연·월 필드만 검사 (넓은 범위 IsValidPtr는 VirtualQuery 비용이 큼)
        uintptr_t ResolveScenarioDataCenter() {
            const uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
            if (!moduleBase)
                return 0;

            const uintptr_t ptrAddr = moduleBase + kScenarioInstanceStaticOffset;
            if (!IsValidPtr(ptrAddr, sizeof(uintptr_t)))
                return 0;

            uintptr_t inst = 0;
            __try {
                inst = *(uintptr_t*)ptrAddr;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }

            if (!inst || inst < 0x10000)
                return 0;

            const uintptr_t yearAddr = inst + kScenarioYearOffset;
            const uintptr_t monthAddr = inst + kScenarioMonthOffset;
            if (!IsValidPtr(yearAddr, 2) || !IsValidPtr(monthAddr, 1))
                return 0;

            return inst;
        }
    } // namespace

    uintptr_t GetScenarioDataCenterAddress() {
        return ResolveScenarioDataCenter();
    }

    bool TickInfiniteBanquet() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        constexpr uintptr_t kBanquetUsedFlagOffset = 0x71E4;
        const uintptr_t addr = dataCenter + kBanquetUsedFlagOffset;
        if (!IsValidPtr(addr, 1))
            return false;

        const uint8_t before = *(const uint8_t*)addr;
        if ((before & 0x02u) == 0)
            return true;

        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt))
            return false;

        *(uint8_t*)addr = (uint8_t)(before & (uint8_t)~0x02u);

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);
        return ((*(const uint8_t*)addr & 0x02u) == 0);
    }

    bool TickInfiniteMediation() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        constexpr uintptr_t kMediationUsedFlagOffset = 0x71E5;
        constexpr uint8_t kMediationUsedBit = 0x40u;

        const uintptr_t addr = dataCenter + kMediationUsedFlagOffset;
        if (!IsValidPtr(addr, 1))
            return false;

        const uint8_t before = *(const uint8_t*)addr;
        if ((before & kMediationUsedBit) == 0)
            return true;

        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt))
            return false;

        *(uint8_t*)addr = (uint8_t)(before & (uint8_t)~kMediationUsedBit);

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);
        return ((*(const uint8_t*)addr & kMediationUsedBit) == 0);
    }

    void UpdateYear(unsigned short targetYear) {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;

        const uintptr_t yearAddr = dataCenter + kScenarioYearOffset;
        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)yearAddr, 2, PAGE_READWRITE, &oldProt))
            return;

        *(unsigned short*)yearAddr = targetYear;

        DWORD tmp = 0;
        VirtualProtect((LPVOID)yearAddr, 2, oldProt, &tmp);
    }

    void UpdateMonth(uint8_t targetMonth) {
        if (targetMonth < 1 || targetMonth > 12)
            return;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;

        const uintptr_t monthAddr = dataCenter + kScenarioMonthOffset;
        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)monthAddr, 1, PAGE_READWRITE, &oldProt))
            return;

        *(uint8_t*)monthAddr = targetMonth;

        DWORD tmp = 0;
        VirtualProtect((LPVOID)monthAddr, 1, oldProt, &tmp);
    }

    bool ReadScenarioYear(unsigned short* outYear) {
        if (!outYear)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        *outYear = *(unsigned short*)(dataCenter + kScenarioYearOffset);
        return true;
    }

    bool ReadScenarioMonth(uint8_t* outMonth) {
        if (!outMonth)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        *outMonth = *(uint8_t*)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    bool ReadScenarioDate(unsigned short* outYear, uint8_t* outMonth) {
        if (!outYear && !outMonth)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        if (outYear)
            *outYear = *(unsigned short*)(dataCenter + kScenarioYearOffset);
        if (outMonth)
            *outMonth = *(uint8_t*)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    bool bMonthCapture = true;
    bool s_appMonthCapture = false;
    static bool g_monthDirectEnabled = false;

    void SetMonthCapture(bool enable) {
        if (enable) {
            if (g_monthDirectEnabled)
                return;
            g_monthDirectEnabled = true;
            AddLog(u8"[MonthCapture] 시나리오 월 직접 조회 활성화: base=+0x2E98BC8 month=+0x72D2");
        } else {
            g_monthDirectEnabled = false;
        }
    }

    uint8_t GetCurrentMonth() {
        if (!g_monthDirectEnabled)
            return 0;

        uint8_t month = 0;
        if (!ReadScenarioMonth(&month))
            return 0;
        return month;
    }

} // namespace DX11Base
