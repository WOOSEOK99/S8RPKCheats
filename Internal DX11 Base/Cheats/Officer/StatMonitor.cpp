#include "../../pch.h"
#include "StatMonitor.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../NotificationManager.h"
#include "../../showlog.h"
#include "../System/SkillCountManager.h"
#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include <string>
#include <map>

namespace DX11Base {

    namespace {
        struct ScanRegionCache {
            uintptr_t start = 0;
            uintptr_t end = 0; // exclusive
            bool readable = false;
        };

        // 5102개 슬롯을 순차 스캔할 때 동일 VirtualQuery 영역의 결과를 재사용합니다.
        // 범위가 캐시 영역을 벗어나는 경우에는 기존 IsValidPtr()로 폴백하여 검증 의미를 유지합니다.
        static bool IsValidPtrForSequentialScan(uintptr_t addr, SIZE_T size, ScanRegionCache& cache) {
            if (!addr || size == 0)
                return false;

            const uintptr_t endAddr = addr + size - 1;
            if (endAddr < addr)
                return false;

            if (cache.start != 0 && addr >= cache.start && endAddr < cache.end)
                return cache.readable;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                cache = {};
                return false;
            }

            const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
            const uintptr_t regionEnd = regionStart + mbi.RegionSize;
            const bool regionEndValid = (regionEnd >= regionStart);
            const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));

            cache.start = regionStart;
            cache.end = regionEndValid ? regionEnd : regionStart;
            cache.readable = readable && regionEndValid;

            if (cache.readable && endAddr < cache.end)
                return true;

            // 드물게 슬롯 검사 범위가 VirtualQuery 영역 경계를 넘는 경우 기존 검증을 그대로 사용합니다.
            return IsValidPtr(addr, size);
        }
    } // namespace

    void UpdateOfficerStats99To100() {
        if (!bAutoStatUp99) return;

        uintptr_t gameBase = GetGameBase();
        if (!gameBase) return;

        // 기준점 확보 (0x3B8은 무장 배열의 직접적인 포인터 컨테이너입니다)
        uintptr_t arrayBase = 0;
        uintptr_t pContainer = *(uintptr_t *)(gameBase + 0x3B8);
        if (pContainer && IsValidPtr(pContainer, 0x1000)) {
            arrayBase = pContainer;
        }
        
        if (!arrayBase) {
            // 0x3B8이 아직 활성화 안 된 경우, p1(0xE0) 기반으로 역산 시도
            uintptr_t p1 = *(uintptr_t *)(gameBase + 0xE0);
            if (p1 && p1 > 0x10000) {
                unsigned short heroID = *(unsigned short *)(p1 + 0x08);
                if (heroID > 0 && heroID <= 5102) {
                    arrayBase = p1 - (uintptr_t)(heroID - 1) * 0x3D0;
                }
            }
        }

        if (!arrayBase || !IsValidPtr(arrayBase, 0x1000)) {
            AddLog(u8"[StatMonitor] 무장 배열을 찾을 수 없습니다. (Base: 0x%llX)", (unsigned long long)arrayBase);
            return;
        }

        int upgradeCount = 0;
        int processedCount = 0;
        ScanRegionCache scanRegion{};
        
        // 통(AA), 무(AB), 지(AC), 정(AD), 매(AE)
        uintptr_t statOffsets[] = { 0xAA, 0xAB, 0xAC, 0xAD, 0xAE };

        for (int i = 0; i < 5102; i++) {
            uintptr_t targetBase = arrayBase + (i * 0x3D0);
            
            // 순차 슬롯이 같은 메모리 영역에 있으면 VirtualQuery 결과 재사용
            if (!IsValidPtrForSequentialScan(targetBase, 0x150, scanRegion)) continue;
            processedCount++;

            // 신분(0x10) 필터링: 군사(0x18), 일반(0x28), 두령(0x38), 동지(0x48), 태수(0xE8), 도독(0xD8), 군주(0xC8)만 보정
            uint8_t status = *(uint8_t*)(targetBase + 0x10);
            bool isValidStatus = (status == 0x18 || status == 0x28 || status == 0x38 || status == 0x48 || status == 0xE8 ||
                                  status == 0xD8 || status == 0xC8);
            if (!isValidStatus) continue;

            bool needsUpgrade = false;
            for (int s = 0; s < 5; s++) {
                if (*(unsigned char*)(targetBase + statOffsets[s]) == 99) {
                    needsUpgrade = true;
                    break;
                }
            }

            if (needsUpgrade) {
                // 한 명의 무장에 대해 단 한 번만 VirtualProtect 호출 (성능 최적화)
                DWORD old;
                if (VirtualProtect((LPVOID)(targetBase + 0xAA), 8, PAGE_READWRITE, &old)) {
                    for (int s = 0; s < 5; s++) {
                        if (*(unsigned char*)(targetBase + statOffsets[s]) == 99) {
                            *(unsigned char*)(targetBase + statOffsets[s]) = 100;
                        }
                    }
                    VirtualProtect((LPVOID)(targetBase + 0xAA), 8, old, &old);
                    upgradeCount++;
                }
            }
        }

        if (upgradeCount > 0) {
            AddLog(u8"[StatMonitor] 능력치 100 보정 완료: 총 %d명 (검색 대상: %d명)", upgradeCount, processedCount);
        } else {
            // 보정 대상이 없을 때도 동작 확인을 위해 하나 남김 (디버깅용)
            // AddLog(u8"[StatMonitor] 보정 완료: 대상 없음 (검색 대상: %d명)", processedCount);
        }
    }

    void MonitorAllAggressive() {
        static bool s_allAggressiveApplied = false;
        static uint64_t s_heroDetectedTime = 0;
        static uintptr_t s_lastHeroAddr = 0;
        
        bool currentEnable = bAllAggressive;

        // 주인공 주소가 바뀌었거나 체크박스가 꺼져있으면 상태 초기화
        if (g_savedHeroAddr != s_lastHeroAddr || !currentEnable) {
            if (s_allAggressiveApplied || s_heroDetectedTime != 0) {
                AddLog(u8"[자동화] 적극성 상태 리셋 (Hero:%p -> %p, Enable:%d)", (void*)s_lastHeroAddr, (void*)g_savedHeroAddr, currentEnable);
            }
            s_allAggressiveApplied = false;
            s_heroDetectedTime = 0;
            s_lastHeroAddr = g_savedHeroAddr;
            if (!currentEnable) return;
        }
        
        // 주인공 어드레스(g_savedHeroAddr)가 유효할 때 (게임 진입 상태)
        if (g_savedHeroAddr > 0x10000) {
            if (s_heroDetectedTime == 0) {
                s_heroDetectedTime = GetTickCount64();
                AddLog(u8"[자동화] 주인공 감지됨. 안정화 대기 시작...");
            }

            // 안정화 시간 체크 (3000ms)
            if (!s_allAggressiveApplied && (GetTickCount64() - s_heroDetectedTime >= 3000)) {
                uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
                uintptr_t arrayBase = 0;

                if (exe && DX11Base::TryResolveOfficerRosterArrayBase(exe, &arrayBase) && arrayBase > 0x10000) {
                } else if (g_savedHeroAddr > 0x10000 && IsValidPtr(g_savedHeroAddr + 0x08, sizeof(unsigned short))) {
                    unsigned short heroID = *(unsigned short*)(g_savedHeroAddr + 0x08);
                    if (heroID >= 1 && heroID <= 5102)
                        arrayBase = g_savedHeroAddr - (uintptr_t)(heroID - 1) * 0x3D0;
                }

                if (arrayBase > 0x10000 && IsValidPtr(arrayBase, 0x3D0)) {
                    AddLog(u8"[자동화] 무장 배열 베이스 주소 확보: %p", (void*)arrayBase);
                    int changeCount = 0;
                    int validObjCount = 0;
                    ScanRegionCache scanRegion{};
                    for (int i = 0; i < 5102; i++) {
                        uintptr_t pBase = arrayBase + (i * 0x3D0);
                        if (!IsValidPtrForSequentialScan(pBase, 0x3D0, scanRegion))
                            continue;

                        RosterStats rs = SafeReadRosterStats(pBase);
                        if (!rs.valid)
                            continue;

                        unsigned short id = rs.id_08;
                        if (id < 1 || id > 5102)
                            continue;
                        // 마스터 표: i번 슬롯은 통상 무장 ID (i+1) — 불일치면 다른 용도 메모리일 수 있음
                        if (id != (unsigned short)(i + 1))
                            continue;

                        uint8_t status = *(uint8_t*)(pBase + 0x10);
                        // SelectOfficercapture 무장 상태(0x10)와 동일(동지 0x48 포함). NPC(0x98)만 제외.
                        bool isValidObj =
                            (status == 0x18 || status == 0x28 || status == 0x38 || status == 0x48 || status == 0x58 ||
                             status == 0x68 || status == 0x78 || status == 0x88 || status == 0xE8 || status == 0xD8 ||
                             status == 0xC8);
                        if (!isValidObj)
                            continue;

                        validObjCount++;
                        uint8_t currentTend = *(uint8_t*)(pBase + 0x60);
                        if (currentTend == 4)
                            continue;

                        DWORD oldP = 0, tmp = 0;
                        uint8_t* pTend = (uint8_t*)(pBase + 0x60);
                        if (VirtualProtect((LPVOID)pTend, 1, PAGE_READWRITE, &oldP)) {
                            *pTend = 4; // 4: 적극 (SelectOfficercapture UI 기준)
                            VirtualProtect((LPVOID)pTend, 1, oldP, &tmp);
                            changeCount++;
                        }
                    }
                    AddLog(u8"[자동화] 적용 결과: 유효 무장 %d명 중 %d명 성향을 '적극'으로 변경했습니다.", validObjCount, changeCount);
                    s_allAggressiveApplied = true;
                } else {
                    AddLog(u8"[자동화] 무장 배열 베이스를 찾을 수 없습니다. (Exe: %p)", (void*)exe);
                }
            }
        }
    }

    void ApplyBatchOfficerEdit() {
        uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t arrayBase = 0;

        if (exe && DX11Base::TryResolveOfficerRosterArrayBase(exe, &arrayBase) && arrayBase > 0x10000) {
        } else if (g_savedHeroAddr > 0x10000 && IsValidPtr(g_savedHeroAddr + 0x08, sizeof(unsigned short))) {
            unsigned short heroID = *(unsigned short*)(g_savedHeroAddr + 0x08);
            if (heroID >= 1 && heroID <= 5102)
                arrayBase = g_savedHeroAddr - (uintptr_t)(heroID - 1) * 0x3D0;
        }

        if (arrayBase <= 0x10000 || !IsValidPtr(arrayBase, 0x3D0)) {
            AddLog(u8"[일괄변경] 무장 배열 베이스 주소를 찾을 수 없어 취소합니다.");
            return;
        }

        int changeCount = 0;
        int validObjCount = 0;

        const uintptr_t traitOffsets[] = {
            0x1D0, 0x1D1, 0x1D2, 0x1D3, 0x1D4, 0x1D5, // 임무
            0x1D6, 0x1D7, 0x1D8, 0x1D9, 0x1DA, 0x1DB, // 지모
            0x1DC, 0x1DD, 0x1DE, 0x1DF, 0x1E0, 0x1E1, // 병과
            0x1E2, 0x1E3, 0x1E4, 0x1E5, 0x1E6, 0x1E7  // 군사
        };

        const uintptr_t tacticOffsets[] = {
            0x139, 0x13A, 0x13B, 0x13C, 0x13D, // 보병
            0x13E, 0x13F, 0x140, 0x141, 0x142, // 기병
            0x143, 0x144, 0x145, 0x146, 0x147, // 궁병
            0x148, 0x149, 0x14A, 0x14B, 0x14C, // 함선
            0x14D, 0x14E, 0x14F, 0x150, 0x151, // 군략
            0x152, 0x153, 0x154, 0x155, 0x156, // 보조
            0x157, 0x158, 0x159, 0x15A, 0x15B  // 둔갑
        };

        for (int i = 0; i < 5102; i++) {
            uintptr_t pBase = arrayBase + (i * 0x3D0);
            if (!IsValidPtr(pBase, 0x3D0)) continue;

            RosterStats rs = SafeReadRosterStats(pBase);
            if (!rs.valid) continue;

            unsigned short id = rs.id_08;
            if (id != (unsigned short)(i + 1)) continue;

            uint8_t status = *(uint8_t*)(pBase + 0x10);
            bool isValidObj = (status == 0x18 || status == 0x28 || status == 0x38 || status == 0x48 || status == 0x58 ||
                               status == 0x68 || status == 0x78 || status == 0x88 || status == 0xE8 || status == 0xD8 ||
                               status == 0xC8);
            if (!isValidObj) continue;

            validObjCount++;
            bool modified = false;

            // 특기 변경
            for (int t = 0; t < 24; t++) {
                if (g_batchTraits[t].enabled) {
                    uint8_t* pVal = (uint8_t*)(pBase + traitOffsets[t]);
                    if (*pVal != (uint8_t)g_batchTraits[t].level) {
                        DWORD old;
                        if (VirtualProtect(pVal, 1, PAGE_READWRITE, &old)) {
                            *pVal = (uint8_t)g_batchTraits[t].level;
                            VirtualProtect(pVal, 1, old, &old);
                            modified = true;
                        }
                    }
                }
            }

            // 전법 변경
            for (int tc = 0; tc < 35; tc++) {
                if (g_batchTactics[tc].enabled) {
                    uint8_t* pVal = (uint8_t*)(pBase + tacticOffsets[tc]);
                    if (*pVal != (uint8_t)g_batchTactics[tc].level) {
                        DWORD old;
                        if (VirtualProtect(pVal, 1, PAGE_READWRITE, &old)) {
                            *pVal = (uint8_t)g_batchTactics[tc].level;
                            VirtualProtect(pVal, 1, old, &old);
                            modified = true;
                        }
                    }
                }
            }

            if (modified) changeCount++;
        }

        AddLog(u8"[일괄변경] 완료: 유효 무장 %d명 중 %d명의 데이터가 수정되었습니다.", validObjCount, changeCount);
    }
}
