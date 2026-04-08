#include "pch.h"
#include "StatMonitor.h"
#include "Cheats.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "showlog.h"
#include "OfficerData.h"
#include <string>
#include <map>

namespace DX11Base {

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
        
        // 통(AA), 무(AB), 지(AC), 정(AD), 매(AE)
        uintptr_t statOffsets[] = { 0xAA, 0xAB, 0xAC, 0xAD, 0xAE };

        for (int i = 0; i < 5102; i++) {
            uintptr_t targetBase = arrayBase + (i * 0x3D0);
            
            // 페이지 단위로 유효성 체크
            if (!IsValidPtr(targetBase, 0x150)) continue;
            processedCount++;

            // 신분(0x10) 필터링: 군사(0x18), 일반(0x28), 태수(0xE8), 도독(0xD8), 군주(0xC8)만 보정
            uint8_t status = *(uint8_t*)(targetBase + 0x10);
            bool isValidStatus = (status == 0x18 || status == 0x28 || status == 0xE8 || status == 0xD8 || status == 0xC8);
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
                            
                            // [알림 추가] 무장 이름 가져오기
                            unsigned short officerID = *(unsigned short*)(targetBase + 0x08);
                            std::string name("무명장수");
                            if (DX11Base::g_officerNames.count(officerID)) {
                                name = DX11Base::g_officerNames[officerID];
                            }
                            
                            const char* statName = "???";
                            switch(s) {
                                case 0: statName = "통솔"; break;
                                case 1: statName = "무력"; break;
                                case 2: statName = "지력"; break;
                                case 3: statName = "정치"; break;
                                case 4: statName = "매력"; break;
                            }
                            
                            char buf[128];
                            sprintf_s(buf, "%s 한계돌파!! %s 100", name.c_str(), statName);
                            DX11Base::AddNotification(std::string(buf));
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
            AddLog(u8"[StatMonitor] 보정 완료: 대상 없음 (검색 대상: %d명)", processedCount);
        }
    }
}
