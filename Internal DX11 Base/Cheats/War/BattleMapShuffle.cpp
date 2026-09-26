#include "../../pch.h"
#include "BattleMapShuffle.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
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

    // ───────────────────────────────────────────────
    //  포인터 체인 해석 (캐시 적용 – 매 루프 5단계 접근 부하 제거)
    // ───────────────────────────────────────────────
    static uintptr_t g_cachedMapBase = 0;           // 캐시된 베이스
    static DWORD     g_cacheTickLast = 0;           // 마지막 캐시 갱신 틱

    static uintptr_t ResolveBattleMapBase() {
        // 2초마다 한 번만 포인터 체인 재해석 (매 8ms 루프 방지)
        DWORD now = GetTickCount();
        if (now - g_cacheTickLast < 2000 && g_cachedMapBase != 0)
            return g_cachedMapBase;

        g_cacheTickLast = now;
        g_cachedMapBase = 0;  // 재확인 전 리셋

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

        g_cachedMapBase = p;
        return p;
    }

    static bool IsWritableMapPage(DWORD protect) {
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        const DWORD p = protect & 0xFF;
        return p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
               p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }

    static bool BeginBulkMapWrite(uintptr_t &rangeStart, SIZE_T &rangeSize,
                                  DWORD &oldProtect, bool &protectionChanged) {
        rangeStart = 0;
        uintptr_t rangeEnd = 0;
        oldProtect = 0;
        protectionChanged = false;

        for (const auto &rec : g_records) {
            if (!rec.valid)
                continue;
            if (rangeStart == 0 || rec.addr < rangeStart)
                rangeStart = rec.addr;
            const uintptr_t end = rec.addr + sizeof(uint64_t);
            if (end > rangeEnd)
                rangeEnd = end;
        }

        if (!rangeStart || rangeEnd <= rangeStart)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((LPCVOID)rangeStart, &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;

        const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
        const uintptr_t regionEnd = regionStart + mbi.RegionSize;
        if (regionEnd <= regionStart || rangeEnd > regionEnd)
            return false;

        rangeSize = (SIZE_T)(rangeEnd - rangeStart);
        if (IsWritableMapPage(mbi.Protect))
            return true;

        const DWORD p = mbi.Protect & 0xFF;
        const bool executable = p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
                                p == PAGE_EXECUTE_WRITECOPY;
        const DWORD writeProtect = executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        if (!VirtualProtect((LPVOID)rangeStart, rangeSize, writeProtect, &oldProtect))
            return false;

        protectionChanged = true;
        return true;
    }

    static void EndBulkMapWrite(uintptr_t rangeStart, SIZE_T rangeSize,
                                DWORD oldProtect, bool protectionChanged) {
        if (!protectionChanged)
            return;
        DWORD tmp = 0;
        VirtualProtect((LPVOID)rangeStart, rangeSize, oldProtect, &tmp);
    }

    // ───────────────────────────────────────────────
    //  원상복구
    // ───────────────────────────────────────────────
    static void RestoreOriginals() {
        if (!g_battleMapShuffleActive) return;

        uintptr_t rangeStart = 0;
        SIZE_T rangeSize = 0;
        DWORD oldProtect = 0;
        bool protectionChanged = false;

        if (BeginBulkMapWrite(rangeStart, rangeSize, oldProtect, protectionChanged)) {
            for (const auto &rec : g_records) {
                if (rec.valid)
                    *(uint64_t*)rec.addr = rec.origValue;
            }
            EndBulkMapWrite(rangeStart, rangeSize, oldProtect, protectionChanged);
        } else {
            // 구조가 예상과 달라 한 영역으로 묶을 수 없는 경우에만 기존 방식으로 폴백합니다.
            for (auto& rec : g_records) {
                if (!rec.valid) continue;
                if (!IsValidPtr(rec.addr, 8)) continue;

                DWORD old = 0, tmp = 0;
                if (!VirtualProtect((LPVOID)rec.addr, 8, PAGE_READWRITE, &old))
                    continue;
                *(uint64_t*)rec.addr = rec.origValue;
                VirtualProtect((LPVOID)rec.addr, 8, old, &tmp);
            }
        }

        g_battleMapShuffleActive = false;
        AddLog(u8"[전투맵셔플] 모든 전투맵 원상복구 완료.");
    }

    // ───────────────────────────────────────────────
    //  랜덤 셔플 실행
    // ───────────────────────────────────────────────
    static void DoRandomShuffle() {
        if (g_uniquePool.empty()) return;

        const ULONGLONG startTick = GetTickCount64();
        srand((unsigned)time(nullptr) ^ GetTickCount());

        uintptr_t rangeStart = 0;
        SIZE_T rangeSize = 0;
        DWORD oldProtect = 0;
        bool protectionChanged = false;

        if (BeginBulkMapWrite(rangeStart, rangeSize, oldProtect, protectionChanged)) {
            for (const auto &rec : g_records) {
                if (!rec.valid)
                    continue;
                *(uint64_t*)rec.addr = g_uniquePool[rand() % g_uniquePool.size()];
            }
            EndBulkMapWrite(rangeStart, rangeSize, oldProtect, protectionChanged);
        } else {
            for (auto& rec : g_records) {
                if (!rec.valid) continue;
                if (!IsValidPtr(rec.addr, 8)) continue;

                const uint64_t randVal = g_uniquePool[rand() % g_uniquePool.size()];
                DWORD old = 0, tmp = 0;
                if (!VirtualProtect((LPVOID)rec.addr, 8, PAGE_READWRITE, &old))
                    continue;
                *(uint64_t*)rec.addr = randVal;
                VirtualProtect((LPVOID)rec.addr, 8, old, &tmp);
            }
        }

        g_battleMapShuffleActive = true;
        AddLog(u8"[전투맵셔플] 평정 기간 진입: 전투맵 랜덤 셔플 완료! (%llums)",
               (unsigned long long)(GetTickCount64() - startTick));
    }

    // ───────────────────────────────────────────────
    //  초기화
    // ───────────────────────────────────────────────
    static bool InitRecords() {
        // 이미 셔플 적용 중이면 원상복구 후 원본값을 재읽기
        // (셔플된 값을 원본으로 오해하는 버그 방지)
        if (g_battleMapShuffleActive) {
            RestoreOriginals();
        }

        // 캐시 강제 갱신 (평정 새 시작마다 최신 베이스 사용)
        g_cachedMapBase = 0;
        g_cacheTickLast = 0;

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
        AddLog(u8"[전투맵셔플] 원본 맵 %d종 읽기 완료 (풀 크기: %d)",
               k_mapCount, (int)g_uniquePool.size());
        return !g_uniquePool.empty();
    }

    // ───────────────────────────────────────────────
    //  자동 업데이트 (BattleMonitor 호출)
    // ───────────────────────────────────────────────
    void UpdateBattleMapAuto(bool isCouncil) {
        if (!bBattleMapShuffle) {
            // 기능이 꺼졌는데 적용 중이면 복구
            if (g_battleMapShuffleActive) RestoreOriginals();
            // 상태 초기화 (다음 평정을 위해)
            return;
        }

        static bool s_lastCouncil = false;
        static bool s_appliedThisCouncil = false; // 이번 평정에서 이미 셔플했는지

        // [평정 종료 감지]
        if (!isCouncil && s_lastCouncil) {
            RestoreOriginals();
            s_appliedThisCouncil = false;
        }

        // [평정 중] 아직 셔플 안 했으면 초기화 + 셔플
        // - 평정 시작 시점에 기능이 켜져 있는 경우
        // - 평정 중간에 기능을 활성화한 경우 모두 처리
        if (isCouncil && !s_appliedThisCouncil) {
            if (InitRecords() && !g_uniquePool.empty()) {
                DoRandomShuffle();
                s_appliedThisCouncil = true;
            }
        }

        s_lastCouncil = isCouncil;

        // 게임 종료(베이스 사라짐) 대응 – 캐시 무효화만 (포인터 체인 5단계 재접근 방지)
        if (g_battleMapShuffleActive) {
            // 캐시가 만료됐을 때만 재확인 (ResolveBattleMapBase 내부 캐시 활용)
            if (ResolveBattleMapBase() == 0) {
                g_battleMapShuffleActive = false;
                g_cachedMapBase = 0;
            }
        }
    }

    // 수동 상태 변경 (UI에서 끌 때 즉시 복구)
    void SetBattleMapShuffle(bool enable) {
        if (!enable && g_battleMapShuffleActive) {
            RestoreOriginals();
        }
    }

} // namespace DX11Base