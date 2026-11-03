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

        thread_local uintptr_t validatedInstance = 0;

        // 시나리오 포인터는 매번 읽고, 같은 주소의 필드 범위는 스레드별로 재사용합니다.
        uintptr_t ResolveScenarioDataCenter() {
            static const uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
            uintptr_t inst = 0;
            __try {
                inst = *(const uintptr_t*)(moduleBase + kScenarioInstanceStaticOffset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                validatedInstance = 0;
                return 0;
            }

            if (inst < 0x10000) {
                validatedInstance = 0;
                return 0;
            }

            if (inst == validatedInstance)
                return inst;

            // 연회/중개 플래그부터 연·월까지 한 번에 확인합니다.
            validatedInstance = 0;
            if (!IsValidPtr(inst + 0x71E4, kScenarioMonthOffset + 1 - 0x71E4))
                return 0;

            validatedInstance = inst;
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

        __try {
            constexpr uintptr_t kBanquetUsedFlagOffset = 0x71E4;
            const uintptr_t addr = dataCenter + kBanquetUsedFlagOffset;
            const uint8_t before = *(const uint8_t*)addr;
            if ((before & 0x02u) == 0)
                return true;

            DWORD oldProt = 0;
            if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt))
                return false;

            __try {
                *(uint8_t*)addr = (uint8_t)(before & (uint8_t)~0x02u);
            } __finally {
                DWORD tmp = 0;
                VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);
            }
            return ((*(const uint8_t*)addr & 0x02u) == 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return false;
        }
    }

    bool TickInfiniteMediation() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        __try {
            constexpr uintptr_t kMediationUsedFlagOffset = 0x71E5;
            constexpr uint8_t kMediationUsedBit = 0x40u;

            const uintptr_t addr = dataCenter + kMediationUsedFlagOffset;
            const uint8_t before = *(const uint8_t*)addr;
            if ((before & kMediationUsedBit) == 0)
                return true;

            DWORD oldProt = 0;
            if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt))
                return false;

            __try {
                *(uint8_t*)addr = (uint8_t)(before & (uint8_t)~kMediationUsedBit);
            } __finally {
                DWORD tmp = 0;
                VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);
            }
            return ((*(const uint8_t*)addr & kMediationUsedBit) == 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return false;
        }
    }

    void UpdateYear(unsigned short targetYear) {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;

        __try {
            const uintptr_t yearAddr = dataCenter + kScenarioYearOffset;
            DWORD oldProt = 0;
            if (!VirtualProtect((LPVOID)yearAddr, 2, PAGE_READWRITE, &oldProt))
                return;

            __try {
                *(unsigned short*)yearAddr = targetYear;
            } __finally {
                DWORD tmp = 0;
                VirtualProtect((LPVOID)yearAddr, 2, oldProt, &tmp);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return;
        }
    }

    void UpdateMonth(uint8_t targetMonth) {
        if (targetMonth < 1 || targetMonth > 12)
            return;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;

        __try {
            const uintptr_t monthAddr = dataCenter + kScenarioMonthOffset;
            DWORD oldProt = 0;
            if (!VirtualProtect((LPVOID)monthAddr, 1, PAGE_READWRITE, &oldProt))
                return;

            __try {
                *(uint8_t*)monthAddr = targetMonth;
            } __finally {
                DWORD tmp = 0;
                VirtualProtect((LPVOID)monthAddr, 1, oldProt, &tmp);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return;
        }
    }

    bool ReadScenarioYear(unsigned short* outYear) {
        if (!outYear)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        __try {
            *outYear = *(unsigned short*)(dataCenter + kScenarioYearOffset);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return false;
        }
    }

    bool ReadScenarioMonth(uint8_t* outMonth) {
        if (!outMonth)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        __try {
            *outMonth = *(uint8_t*)(dataCenter + kScenarioMonthOffset);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return false;
        }
    }

    bool ReadScenarioDate(unsigned short* outYear, uint8_t* outMonth) {
        if (!outYear && !outMonth)
            return false;

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        __try {
            if (outYear)
                *outYear = *(unsigned short*)(dataCenter + kScenarioYearOffset);
            if (outMonth)
                *outMonth = *(uint8_t*)(dataCenter + kScenarioMonthOffset);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            validatedInstance = 0;
            return false;
        }
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
