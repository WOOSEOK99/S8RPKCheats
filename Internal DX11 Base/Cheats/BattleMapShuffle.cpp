#include "../pch.h"
#include "BattleMapShuffle.h"
#include "../Cheats.h"
#include "../showlog.h"
#include "../MemoryUtils.h"
#include "../MenuState.h"
#include <psapi.h>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <ctime>

namespace DX11Base {

    struct MapEntry {
        const char* name;
        uintptr_t   lastOffset;
    };

    struct MapRecord {
        uintptr_t addr;
        uint64_t  origValue;
        bool      valid;
    };

    static const MapEntry k_mapEntries[] = {
        { "양평 도시 전투맵1",      0x58   },
        { "북평 도시 전투맵1",      0x2F8  },
        //{ "북평 도시 전투맵2",      0x300  },
        //{ "북평 도시 전투맵3",      0x308  },
        { "계 도시 전투맵1",        0x598  },
        { "계 도시 전투맵2",        0x5A0  },
        { "계 도시 전투맵3",        0x5A8  },
        { "남피 도시 전투맵1",      0x838  },
        { "남피 도시 전투맵2",      0x840  },
        { "남피 도시 전투맵3",      0x848  },
        { "남피 도시 전투맵4",      0x850  },
        { "업 도시 전투맵1",        0xAD8  },
        { "업 도시 전투맵2",        0xAE0  },
        { "업 도시 전투맵3",        0xAE8  },
        { "업 도시 전투맵4",        0xAF0  },
        { "업 도시 전투맵5",        0xAF8  },
        { "평원 도시 전투맵1",      0xD78  },
        { "평원 도시 전투맵2",      0xD80  },
        { "평원 도시 전투맵3",      0xD88  },
        { "평원 도시 전투맵4",      0xD90  },
        { "북해 도시 전투맵1",      0x1018 },
        { "북해 도시 전투맵2",      0x1020 },
        { "북해 도시 전투맵3",      0x1028 },
        { "제남 도시 전투맵1",      0x12B8 },
        { "제남 도시 전투맵2",      0x12C0 },
        { "제남 도시 전투맵3",      0x12C8 },
        { "진양 도시 전투맵1",      0x1558 },
        { "진양 도시 전투맵2",      0x1560 },
        //{ "상당 도시 전투맵1",      0x17F8 },
        { "상당 도시 전투맵2",      0x1800 },
        { "상당 도시 전투맵3",      0x1808 },
        { "하비 도시 전투맵1",      0x1A98 },
        { "하비 도시 전투맵2",      0x1AA0 },
        { "하비 도시 전투맵3",      0x1AA8 },
        { "소패 도시 전투맵1",      0x1D38 },
        { "소패 도시 전투맵2",      0x1D40 },
        { "소패 도시 전투맵3",      0x1D48 },
        { "소패 도시 전투맵4",      0x1D50 },
        { "복양 도시 전투맵1",      0x1FD8 },
        { "복양 도시 전투맵2",      0x1FE0 },
        { "복양 도시 전투맵3",      0x1FE8 },
        { "복양 도시 전투맵4",      0x1FF0 },
        { "진류 도시 전투맵1",      0x2278 },
        { "진류 도시 전투맵2",      0x2280 },
        { "진류 도시 전투맵3",      0x2288 },
        { "진류 도시 전투맵4",      0x2290 },
        { "진류 도시 전투맵5",      0x2298 },
        { "허창 도시 전투맵1",      0x2518 },
        { "허창 도시 전투맵2",      0x2520 },
        { "허창 도시 전투맵3",      0x2528 },
        { "허창 도시 전투맵4",      0x2530 },
        { "허창 도시 전투맵5",      0x2538 },
        { "초 도시 전투맵1",        0x27B8 },
        { "초 도시 전투맵2",        0x27C0 },
        { "초 도시 전투맵3",        0x27C8 },
        { "여남 도시 전투맵1",      0x2A58 },
        { "여남 도시 전투맵2",      0x2A60 },
        { "여남 도시 전투맵3",      0x2A68 },
        { "여남 도시 전투맵4",      0x2A70 },
        { "여남 도시 전투맵5",      0x2A78 },
        { "낙양 도시 전투맵1",      0x2CF8 },
        //{ "낙양 도시 전투맵2",      0x2D00 },
        //{ "낙양 도시 전투맵3",      0x2D08 },
        //{ "낙양 도시 전투맵4",      0x2D10 },
        { "홍농 도시 전투맵1",      0x2F98 },
        //{ "홍농 도시 전투맵2",      0x2FA0 },
        { "장안 도시 전투맵1",      0x3238 },
        { "장안 도시 전투맵2",      0x3240 },
        //{ "장안 도시 전투맵3",      0x3248 },
        //{ "장안 도시 전투맵4",      0x3250 },
        { "천수 도시 전투맵1",      0x34D8 },
        { "천수 도시 전투맵2",      0x34E0 },
        { "천수 도시 전투맵3",      0x34E8 },
        { "천수 도시 전투맵4",      0x34F0 },
        { "무위 도시 전투맵1",      0x3778 },
        { "무위 도시 전투맵2",      0x3780 },
        { "광릉 도시 전투맵1",      0x3A18 },
        { "광릉 도시 전투맵2",      0x3A20 },
        { "광릉 도시 전투맵3",      0x3A28 },
        { "서평 도시 전투맵1",      0x3CB8 },
        { "서평 도시 전투맵2",      0x3CC0 },
        //{ "한중 도시 전투맵1",      0x3F58 },
        { "한중 도시 전투맵2",      0x3F60 },
        { "한중 도시 전투맵3",      0x3F68 },
        { "한중 도시 전투맵4",      0x3F70 },
        { "무도 도시 전투맵1",      0x41F8 },
        { "무도 도시 전투맵2",      0x4200 },
        //{ "성도 도시 전투맵1",      0x4498 },
        { "성도 도시 전투맵2",      0x44A0 },
        { "성도 도시 전투맵3",      0x44A8 },
        { "성도 도시 전투맵4",      0x44B0 },
        { "영안 도시 전투맵1",      0x4738 },
        { "영안 도시 전투맵2",      0x4740 },
        { "영안 도시 전투맵3",      0x4748 },
        //{ "자동 도시 전투맵1",      0x49D8 },
        { "자동 도시 전투맵2",      0x49E0 },
        { "자동 도시 전투맵3",      0x49E8 },
        { "강주 도시 전투맵1",      0x4C78 },
        { "강주 도시 전투맵2",      0x4C80 },
        { "강주 도시 전투맵3",      0x4C88 },
        { "영창 도시 전투맵1",      0x4F18 },
        { "영창 도시 전투맵2",      0x4F20 },
        { "완 도시 전투맵1",        0x51B8 },
        { "완 도시 전투맵2",        0x51C0 },
        { "완 도시 전투맵3",        0x51C8 },
        { "완 도시 전투맵4",        0x51D0 },
        { "신야 도시 전투맵1",      0x5458 },
        { "신야 도시 전투맵2",      0x5460 },
        { "신야 도시 전투맵3",      0x5468 },
        { "양양 도시 전투맵1",      0x56F8 },
        { "양양 도시 전투맵2",      0x5700 },
        { "양양 도시 전투맵3",      0x5708 },
        { "양양 도시 전투맵4",      0x5710 },
        { "강하 도시 전투맵1",      0x5998 },
        { "강하 도시 전투맵2",      0x59A0 },
        { "강하 도시 전투맵3",      0x59A8 },
        { "강하 도시 전투맵4",      0x59B0 },
        { "상용 도시 전투맵1",      0x5C38 },
        { "상용 도시 전투맵2",      0x5C40 },
        { "강릉 도시 전투맵1",      0x5ED8 },
        { "강릉 도시 전투맵2",      0x5EE0 },
        { "강릉 도시 전투맵3",      0x5EE8 },
        { "강릉 도시 전투맵4",      0x5EF0 },
        { "강릉 도시 전투맵5",      0x5EF8 },
        { "장사 도시 전투맵1",      0x6178 },
        { "장사 도시 전투맵2",      0x6180 },
        { "장사 도시 전투맵3",      0x6188 },
        { "장사 도시 전투맵4",      0x6190 },
        { "영릉 도시 전투맵1",      0x6418 },
        { "영릉 도시 전투맵2",      0x6420 },
        { "영릉 도시 전투맵3",      0x6428 },
        { "무릉 도시 전투맵1",      0x66B8 },
        { "무릉 도시 전투맵2",      0x66C0 },
        { "무릉 도시 전투맵3",      0x66C8 },
        { "무릉 도시 전투맵4",      0x66D0 },
        { "계양 도시 전투맵1",      0x6958 },
        { "계양 도시 전투맵2",      0x6960 },
        { "계양 도시 전투맵3",      0x6968 },
        { "수춘 도시 전투맵1",      0x6BF8 },
        { "수춘 도시 전투맵2",      0x6C00 },
        { "수춘 도시 전투맵3",      0x6C08 },
        { "수춘 도시 전투맵4",      0x6C10 },
        { "여강 도시 전투맵1",      0x6E98 },
        { "여강 도시 전투맵2",      0x6EA0 },
        { "여강 도시 전투맵3",      0x6EA8 },
        { "여강 도시 전투맵4",      0x6EB0 },
        { "여강 도시 전투맵5",      0x6EB8 },
        { "말릉/건업 도시 전투맵1", 0x7138 },
        { "말릉/건업 도시 전투맵2", 0x7140 },
        { "말릉/건업 도시 전투맵3", 0x7148 },
        { "말릉/건업 도시 전투맵4", 0x7150 },
        { "말릉/건업 도시 전투맵5", 0x7158 },
        { "오 도시 전투맵1",        0x73D8 },
        { "오 도시 전투맵2",        0x73E0 },
        { "회계 도시 전투맵1",      0x7678 },
        { "회계 도시 전투맵2",      0x7680 },
        { "시상 도시 전투맵1",      0x7918 },
        { "시상 도시 전투맵2",      0x7920 },
        { "시상 도시 전투맵3",      0x7928 },
        { "시상 도시 전투맵4",      0x7930 },
        { "시상 도시 전투맵5",      0x7938 },
        { "파양 도시 전투맵1",      0x7BB8 },
        { "파양 도시 전투맵2",      0x7BC0 },
        { "파양 도시 전투맵3",      0x7BC8 },
        { "건녕 도시 전투맵1",      0x7E58 },
        { "건녕 도시 전투맵2",      0x7E60 },
        { "건녕 도시 전투맵3",      0x7E68 },
        { "운남 도시 전투맵1",      0x80F8 },
        { "운남 도시 전투맵2",      0x8100 },
        { "교지 도시 전투맵1",      0x8398 },
        { "교지 도시 전투맵2",      0x83A0 },
        { "교지 도시 전투맵3",      0x83A8 },
    };

    static const int k_mapCount = sizeof(k_mapEntries) / sizeof(k_mapEntries[0]);

    // 상태 관리
    static std::vector<MapRecord>  g_records;
    static std::vector<uint64_t>   g_uniquePool;
    static bool g_battleMapShuffleActive = false; // 현재 셔플 적용 중인지
    static bool g_isInitialized = false;

    // ───────────────────────────────────────────────
    //  포인터 체인 해석
    // ───────────────────────────────────────────────
    static uintptr_t ResolveBattleMapBase() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return 0;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x34C8630);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x8);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x10);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return 0;

        return p;
    }

    // ───────────────────────────────────────────────
    //  원상복구
    // ───────────────────────────────────────────────
    static void RestoreOriginals() {
        if (!g_battleMapShuffleActive) return;

        for (auto& rec : g_records) {
            if (!rec.valid) continue;
            if (!IsValidPtr(rec.addr, 8)) continue;
            
            DWORD old, tmp;
            VirtualProtect((LPVOID)rec.addr, 8, PAGE_READWRITE, &old);
            *(uint64_t*)rec.addr = rec.origValue;
            VirtualProtect((LPVOID)rec.addr, 8, old, &tmp);
        }
        g_battleMapShuffleActive = false;
        AddLog(u8"[전투맵셔플] 모든 전투맵 원상복구 완료.");
    }

    // ───────────────────────────────────────────────
    //  랜덤 셔플 실행
    // ───────────────────────────────────────────────
    static void DoRandomShuffle() {
        if (g_uniquePool.empty()) return;

        srand((unsigned)time(nullptr) ^ GetTickCount());

        for (auto& rec : g_records) {
            if (!rec.valid) continue;
            if (!IsValidPtr(rec.addr, 8)) continue;

            uint64_t randVal = g_uniquePool[rand() % g_uniquePool.size()];
            DWORD old, tmp;
            VirtualProtect((LPVOID)rec.addr, 8, PAGE_READWRITE, &old);
            *(uint64_t*)rec.addr = randVal;
            VirtualProtect((LPVOID)rec.addr, 8, old, &tmp);
        }

        g_battleMapShuffleActive = true;
        AddLog(u8"[전투맵셔플] 평정 기간 진입: 전투맵 랜덤 셔플 완료!");
    }

    // ───────────────────────────────────────────────
    //  초기화
    // ───────────────────────────────────────────────
    static bool InitRecords() {
        uintptr_t base = ResolveBattleMapBase();
        if (!base) return false;

        g_records.clear();
        g_uniquePool.clear();
        std::unordered_map<uint64_t, bool> uniqueMap;

        for (int i = 0; i < k_mapCount; i++) {
            uintptr_t addr = base + k_mapEntries[i].lastOffset;
            MapRecord rec = { addr, 0, false };

            if (IsValidPtr(addr, 8)) {
                rec.origValue = *(uint64_t*)addr;
                rec.valid = true;

                if (uniqueMap.find(rec.origValue) == uniqueMap.end()) {
                    uniqueMap[rec.origValue] = true;
                    g_uniquePool.push_back(rec.origValue);
                }
            }
            g_records.push_back(rec);
        }
        g_isInitialized = true;
        return !g_uniquePool.empty();
    }

    // ───────────────────────────────────────────────
    //  자동 업데이트 (BattleMonitor 호출)
    // ───────────────────────────────────────────────
    void UpdateBattleMapAuto(bool isCouncil) {
        if (!bBattleMapShuffle) {
            // 기능이 꺼졌는데 적용 중이면 복구
            if (g_battleMapShuffleActive) RestoreOriginals();
            return;
        }

        static bool s_lastCouncil = false;
        
        // 평정(Council) 상태 변화 감지
        if (isCouncil && !s_lastCouncil) {
            // 평정 시작
            if (!g_isInitialized) InitRecords();
            DoRandomShuffle();
        } 
        else if (!isCouncil && s_lastCouncil) {
            // 평정 종료
            RestoreOriginals();
        }

        s_lastCouncil = isCouncil;

        // 게임 종료(베이스 사라짐) 대응을 위한 추가 복구 로직 (이미지 베이스 유효성 체크 등)
        if (g_battleMapShuffleActive && !ResolveBattleMapBase()) {
            g_battleMapShuffleActive = false; // 강제 복구 상태로 플래그만 변경
        }
    }

    // 수동 상태 변경 (UI에서 끌 때 즉시 복구)
    void SetBattleMapShuffle(bool enable) {
        if (!enable && g_battleMapShuffleActive) {
            RestoreOriginals();
        }
    }

} // namespace DX11Base
