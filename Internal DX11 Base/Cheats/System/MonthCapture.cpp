#include "../../pch.h"
#include "MonthCapture.h"
#include "SystemMonth.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"

namespace DX11Base {

    namespace {
        // 기존 시나리오 데이터 블록. 연회/중개 및 기존 날짜 API는 이 경로를 유지합니다.
        constexpr uintptr_t kScenarioInstanceStaticOffset = 0x2E98BC8;
        constexpr uintptr_t kScenarioYearOffset = 0x72D0;
        constexpr uintptr_t kScenarioMonthOffset = 0x72D2;

        // 사용자 제공 CT의 현재 날짜 포인터 체인:
        // SAN8RPK.exe+034C8630 -> +3D20 -> +8 -> +10 -> +0 -> +E8 -> +E0 -> +7332(월)
        constexpr uintptr_t kCtDateRootStaticOffset = 0x34C8630;
        constexpr uintptr_t kCtCurrentMonthOffset = 0x7332;

        uint64_t s_lastMonthCheckTick = 0;
        uint8_t s_lastCtMonth = 0xFF;
        uint8_t s_lastScenarioMonth = 0xFF;
        uint8_t s_lastSystemMonth = 0xFF;

        bool ReadPointerChecked(uintptr_t address, uintptr_t* outValue) {
            if (!outValue || address < 0x10000 || !IsValidPtr(address, sizeof(uintptr_t)))
                return false;

            __try {
                const uintptr_t value = *(uintptr_t*)address;
                if (value < 0x10000)
                    return false;
                *outValue = value;
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        uintptr_t ResolveCtCurrentMonthAddress() {
            const uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
            if (!moduleBase)
                return 0;

            // Cheat Engine pointer record semantics:
            // base itself is not dereferenced first. Apply +3D20, dereference,
            // then continue the remaining offsets in order.
            uintptr_t current = moduleBase + kCtDateRootStaticOffset;

            constexpr uintptr_t kPointerOffsets[] = {
                0x3D20, 0x8, 0x10, 0x0, 0xE8, 0xE0
            };

            for (const uintptr_t offset : kPointerOffsets) {
                uintptr_t next = 0;
                if (!ReadPointerChecked(current + offset, &next))
                    return 0;
                current = next;
            }

            const uintptr_t monthAddr = current + kCtCurrentMonthOffset;
            if (!IsValidPtr(monthAddr, 1))
                return 0;
            return monthAddr;
        }

        // 연·월 필드만 검사 (넓은 범위 IsValidPtr는 VirtualQuery 비용이 큼)
        uintptr_t ResolveScenarioDataCenter() {
            uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
            if (!moduleBase)
                return 0;
            uintptr_t ptrAddr = moduleBase + kScenarioInstanceStaticOffset;
            if (!IsValidPtr(ptrAddr, sizeof(uintptr_t)))
                return 0;
            uintptr_t inst = *(uintptr_t *)ptrAddr;
            if (!inst || inst < 0x10000)
                return 0;
            uintptr_t yearAddr = inst + kScenarioYearOffset;
            uintptr_t monthAddr = inst + kScenarioMonthOffset;
            if (!IsValidPtr(yearAddr, 2) || !IsValidPtr(monthAddr, 1))
                return 0;
            return inst;
        }

        void LogMonthCheckIfChanged(uint8_t ctMonth) {
            const uint64_t now = GetTickCount64();
            if (s_lastMonthCheckTick != 0 && now - s_lastMonthCheckTick < 500)
                return;
            s_lastMonthCheckTick = now;

            uint8_t scenarioMonth = 0;
            ReadScenarioMonth(&scenarioMonth);
            const uint8_t systemMonth = GetSystemMonthValue();

            if (ctMonth == s_lastCtMonth &&
                scenarioMonth == s_lastScenarioMonth &&
                systemMonth == s_lastSystemMonth)
                return;

            s_lastCtMonth = ctMonth;
            s_lastScenarioMonth = scenarioMonth;
            s_lastSystemMonth = systemMonth;
            AddLog(u8"[MonthCheck] CT=%u Scenario=%u System=%u",
                   (unsigned)ctMonth,
                   (unsigned)scenarioMonth,
                   (unsigned)systemMonth);
        }
    } // namespace

    uintptr_t GetScenarioDataCenterAddress() {
        return ResolveScenarioDataCenter();
    }

    bool TickInfiniteBanquet() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        // 실게임 검증: 연회 실행 전/후 +0x71E4가 0x15 -> 0x17로 변하며
        // bit 1(0x02)만 연회 사용 완료 상태를 나타냄.
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

        // 실게임 검증:
        // 중개 실행 전/후 +0x71E5가 0x00 -> 0x40으로 변했고,
        // bit6(0x40)만 해제하면 중개가 즉시 다시 활성화됨.
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

        *(uint8_t*)addr =
            (uint8_t)(before & (uint8_t)~kMediationUsedBit);

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);

        return ((*(const uint8_t*)addr & kMediationUsedBit) == 0);
    }

    void UpdateYear(unsigned short targetYear) {
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;
        uintptr_t yearAddr = dataCenter + kScenarioYearOffset;
        DWORD oldProt, tmp;
        if (!VirtualProtect((LPVOID)yearAddr, 2, PAGE_READWRITE, &oldProt))
            return;
        *(unsigned short *)yearAddr = targetYear;
        VirtualProtect((LPVOID)yearAddr, 2, oldProt, &tmp);
    }

    void UpdateMonth(uint8_t targetMonth) {
        if (targetMonth < 1 || targetMonth > 12)
            return;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;
        uintptr_t monthAddr = dataCenter + kScenarioMonthOffset;
        DWORD oldProt, tmp;
        if (!VirtualProtect((LPVOID)monthAddr, 1, PAGE_READWRITE, &oldProt))
            return;
        *(uint8_t *)monthAddr = targetMonth;
        VirtualProtect((LPVOID)monthAddr, 1, oldProt, &tmp);
    }

    bool ReadScenarioYear(unsigned short *outYear) {
        if (!outYear)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        *outYear = *(unsigned short *)(dataCenter + kScenarioYearOffset);
        return true;
    }

    bool ReadScenarioMonth(uint8_t *outMonth) {
        if (!outMonth)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        *outMonth = *(uint8_t *)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    bool ReadScenarioDate(unsigned short *outYear, uint8_t *outMonth) {
        if (!outYear && !outMonth)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        if (outYear)
            *outYear = *(unsigned short *)(dataCenter + kScenarioYearOffset);
        if (outMonth)
            *outMonth = *(uint8_t *)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    // ---------------------------------------------------------------------------
    // 글로벌 상태 및 설정
    // ---------------------------------------------------------------------------
    bool bMonthCapture = true;       // 상설 기능화 (기본값 true)
    bool s_appMonthCapture = false;  // 현재 적용 여부

    // CT 포인터 체인을 필요할 때마다 직접 해석합니다.
    // 세이브 로드로 포인터 세대가 바뀌어도 오래된 주소를 유지하지 않습니다.
    static bool g_monthDirectEnabled = false;
    static uintptr_t g_realMonthAddr = 0;

    void SetMonthCapture(bool enable) {
        if (enable) {
            if (g_monthDirectEnabled)
                return;
            g_monthDirectEnabled = true;
            g_realMonthAddr = ResolveCtCurrentMonthAddress();
            s_lastMonthCheckTick = 0;
            s_lastCtMonth = 0xFF;
            s_lastScenarioMonth = 0xFF;
            s_lastSystemMonth = 0xFF;
            AddLog(u8"[MonthCapture] CT 포인터 체인 직접 조회 활성화: root=+0x34C8630 month=+0x7332 addr=%p",
                   (void*)g_realMonthAddr);
        } else {
            g_monthDirectEnabled = false;
            g_realMonthAddr = 0;
            s_lastMonthCheckTick = 0;
            s_lastCtMonth = 0xFF;
            s_lastScenarioMonth = 0xFF;
            s_lastSystemMonth = 0xFF;
        }
    }

    uint8_t GetCurrentMonth() {
        if (!g_monthDirectEnabled)
            return 0;

        g_realMonthAddr = ResolveCtCurrentMonthAddress();
        if (!g_realMonthAddr)
            return 0;

        __try {
            const uint8_t month = *(uint8_t*)g_realMonthAddr;
            LogMonthCheckIfChanged(month);
            return month;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_realMonthAddr = 0;
            return 0;
        }
    }

} // namespace DX11Base
