#include "../../pch.h"
#include "FactionLordBonus.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include <atomic>
#include <psapi.h>
#include <unordered_map>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  세력 군주 보너스 자동 배정
    //  5초마다 실행
    //  관작 등급에 따라 보너스 포인터 배정
    // ───────────────────────────────────────────────

    bool   g_factionLordBonusEnabled = false;
    static std::atomic<bool> g_factionLordBonusRunning{false};
    static std::atomic<bool> g_factionLordBonusStopRequested{false};
    static HANDLE g_factionLordBonusThread  = nullptr;
    static HANDLE g_factionLordBonusStopEvent = nullptr;
    static std::mutex g_factionLordLifecycleMutex;

    static std::unordered_map<uintptr_t, bool> g_prevAssigned;
    static bool g_titleNamesApplied = false;
    // [크래시 방지] FactionLordBonusThread(별도 스레드)와 메인 스레드가 g_prevAssigned를
    // 동시에 읽고 쓸 수 있으므로 mutex로 보호
    static std::mutex g_prevAssignedMutex;

    // 상수
    static const int      FACTION_COUNT = 120;
    static const uintptr_t FACTION_SIZE = 0x998;

    static const uintptr_t OFF_FACTION_LORD_PTR = 0xC6908;
    static const uintptr_t OFF_FACTION_EXIST    = 0xC690D;
    static const uintptr_t OFF_FACTION_TITLEPTR = 0xC6A20;
    static const uintptr_t OFF_FACTION_WANDER   = 0xC6A28;

    static const uintptr_t OFF_LORD_BONUS  = 0x330;
    static const uintptr_t OFF_TITLE_RANK  = 0x0E;

    static const uintptr_t OFF_EMPEROR = 0x4EC6D8;
    static const uintptr_t OFF_KING    = 0x4EC6F0;
    static const uintptr_t OFF_GONG    = 0x4EC708;
    static const uintptr_t OFF_JUMOK   = 0x4EC720;
    static const uintptr_t OFF_GUNJU   = 0x4EC738;

    static const uintptr_t OFF_TITLE_NAME_PATCH = 0x197A774;

    static uintptr_t ResolveRoot() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return 0;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
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

    // 동일 값이면 VirtualProtect/쓰기 자체를 생략합니다.
    static void WriteBytes(uintptr_t addr, const uint8_t* data, size_t size) {
        if (!IsValidPtr(addr, size)) return;
        if (memcmp((const void*)addr, data, size) == 0) return;

        DWORD old, tmp;
        if (!VirtualProtect((LPVOID)addr, size, PAGE_READWRITE, &old)) return;
        memcpy((void*)addr, data, size);
        VirtualProtect((LPVOID)addr, size, old, &tmp);
    }

    static void WriteQword(uintptr_t addr, uint64_t val) {
        if (!IsValidPtr(addr, 8)) return;
        if (*(uint64_t*)addr == val) return;

        DWORD old, tmp;
        if (!VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old)) return;
        *(uint64_t*)addr = val;
        VirtualProtect((LPVOID)addr, 8, old, &tmp);
    }

    static void ApplyTitleNames(uintptr_t root) {
        // 스크립트의 writeBytes(root+197A774, ...) 동일
        static const uint8_t kNames[48] = {
            0x69,0xD6,0x1C,0xC8,0x00,0x00,0x00,0x00,
            0x55,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,
            0xF5,0xAC,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFC,0xC8,0xA9,0xBA,0x00,0x00,0x00,0x00,
            0x70,0xAD,0xFC,0xC8,0x00,0x00,0x00,0x00,
            0x9C,0xCC,0xF5,0xAC,0xA5,0xC7,0x70,0xAD
        };
        WriteBytes(root + OFF_TITLE_NAME_PATCH, kNames, sizeof(kNames));
    }

    static void ClearTitleNames(uintptr_t root) {
        static const uint8_t kZeros[48] = {};
        WriteBytes(root + OFF_TITLE_NAME_PATCH, kZeros, sizeof(kZeros));
    }

    static void ApplyBonusTable(uintptr_t root) {
        uintptr_t base   = root + OFF_EMPEROR;
        uintptr_t stride = 24;

        static const uint8_t patches[5][15] = {
            // 치트엔진 스크립트 patches[] 동일 (OFF_EMPEROR 기준 +9에 15바이트)
            {0x54,0x00,0x14,0x88,0x13,0x05,0x05,0x05,0x05,0x05,0x00,0x01,0x03,0x01,0x00},
            {0x56,0x00,0x14,0xB8,0x0B,0x04,0x04,0x04,0x04,0x04,0x00,0x01,0x03,0x01,0x00},
            {0x58,0x00,0x14,0xD0,0x07,0x03,0x03,0x03,0x03,0x03,0x00,0x01,0x03,0x01,0x00},
            {0x5A,0x00,0x14,0xE8,0x03,0x02,0x02,0x02,0x02,0x02,0x00,0x01,0x03,0x01,0x00},
            {0x5C,0x00,0x14,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x01,0x03,0x01,0x00},
        };

        for (int i = 0; i < 5; i++)
            WriteBytes(base + i * stride + 9, patches[i], 15);
    }

    static void ClearBonusTable(uintptr_t root) {
        uintptr_t base   = root + OFF_EMPEROR;
        uintptr_t stride = 24;
        static const uint8_t zeros[15] = {};

        for (int i = 0; i < 5; i++)
            WriteBytes(base + i * stride + 9, zeros, 15);
    }

    static void RunOnce() {
        // 아직 메모리 쓰기를 시작하지 않은 시점에서만 취소합니다.
        // 한 번 쓰기를 시작한 뒤에는 120개 세력 순회와 g_prevAssigned 갱신까지
        // 끝내야 OFF 정리가 이번 pass에서 새로 배정한 군주까지 정확히 복구할 수 있습니다.
        if (g_factionLordBonusStopRequested.load(std::memory_order_acquire)) return;

        uintptr_t root = ResolveRoot();
        if (!root) return;
        if (g_factionLordBonusStopRequested.load(std::memory_order_acquire)) return;

        // 관작 이름 패치 1회 적용 (root가 준비된 시점)
        if (!g_titleNamesApplied) {
            ApplyTitleNames(root);
            g_titleNamesApplied = true;
        }

        // 1. 보너스 테이블 적용. WriteBytes 내부에서 동일 값은 실제 쓰기를 생략합니다.
        ApplyBonusTable(root);

        uintptr_t ptrEmperor = root + OFF_EMPEROR;
        uintptr_t ptrKing    = root + OFF_KING;
        uintptr_t ptrGong    = root + OFF_GONG;
        uintptr_t ptrJumok   = root + OFF_JUMOK;
        uintptr_t ptrGunju   = root + OFF_GUNJU;

        std::unordered_map<uintptr_t, bool> bonusSet = {
            {ptrEmperor, true},
            {ptrKing,    true},
            {ptrGong,    true},
            {ptrJumok,   true},
            {ptrGunju,   true},
        };

        std::unordered_map<uintptr_t, bool> currentAssigned;

        // 2. 세력 순회. pass 중에는 중간 취소하지 않습니다.
        for (int i = 0; i < FACTION_COUNT; i++) {
            uintptr_t existAddr    = root + OFF_FACTION_EXIST    + i * FACTION_SIZE;
            uintptr_t wanderAddr   = root + OFF_FACTION_WANDER   + i * FACTION_SIZE;
            uintptr_t lordPtrAddr  = root + OFF_FACTION_LORD_PTR + i * FACTION_SIZE;
            uintptr_t titlePtrAddr = root + OFF_FACTION_TITLEPTR + i * FACTION_SIZE;

            if (!IsValidPtr(existAddr, 1)) continue;

            uint8_t  exist   = *(uint8_t*)existAddr;
            uint8_t  wander  = IsValidPtr(wanderAddr, 1) ? *(uint8_t*)wanderAddr : 1;
            uint64_t lordPtr = IsValidPtr(lordPtrAddr, 8) ? *(uint64_t*)lordPtrAddr : 0;

            if (exist < 1 || wander != 0 || lordPtr == 0) continue;
            if (!IsValidPtr(lordPtr, OFF_LORD_BONUS + 8)) continue;

            uintptr_t bonusAddr = lordPtr + OFF_LORD_BONUS;
            uint64_t  titlePtr  = IsValidPtr(titlePtrAddr, 8) ? *(uint64_t*)titlePtrAddr : 0;

            uintptr_t target = ptrGunju;

            if (titlePtr && IsValidPtr(titlePtr + OFF_TITLE_RANK, 1)) {
                uint8_t rank = *(uint8_t*)(titlePtr + OFF_TITLE_RANK);
                if      (rank == 1) target = ptrEmperor;
                else if (rank == 2) target = ptrKing;
                else if (rank == 3) target = ptrGong;
                else if (rank == 4) target = ptrJumok;
                else                target = ptrGunju;
            }

            currentAssigned[bonusAddr] = true;

            uint64_t current = IsValidPtr(bonusAddr, 8) ? *(uint64_t*)bonusAddr : 0;
            if (current != target)
                WriteQword(bonusAddr, target);
        }

        // 3. 이전에 배정됐지만 지금은 빠진 대상 정리 및 이번 pass 결과 게시.
        {
            std::lock_guard<std::mutex> lock(g_prevAssignedMutex);
            for (auto& kv : g_prevAssigned) {
                uintptr_t bonusAddr = kv.first;
                if (currentAssigned.count(bonusAddr)) continue;
                if (!IsValidPtr(bonusAddr, 8)) continue;
                uint64_t current = *(uint64_t*)bonusAddr;
                if (bonusSet.count(current))
                    WriteQword(bonusAddr, 0);
            }
            g_prevAssigned = currentAssigned;
        }
    }

    static void ClearAll(uintptr_t root) {
        uintptr_t ptrEmperor = root + OFF_EMPEROR;
        uintptr_t ptrKing    = root + OFF_KING;
        uintptr_t ptrGong    = root + OFF_GONG;
        uintptr_t ptrJumok   = root + OFF_JUMOK;
        uintptr_t ptrGunju   = root + OFF_GUNJU;

        uintptr_t bonusSet_copy[5] = {ptrEmperor, ptrKing, ptrGong, ptrJumok, ptrGunju};

        std::lock_guard<std::mutex> lock(g_prevAssignedMutex);
        for (auto& kv : g_prevAssigned) {
            uintptr_t bonusAddr = kv.first;
            if (!IsValidPtr(bonusAddr, 8)) continue;
            uint64_t current = *(uint64_t*)bonusAddr;
            // bonusSet은 포인터 비교용 — 위에서 계산한 5개 주소 중 하나면 복구
            for (auto target : bonusSet_copy) {
                if (current == target) { WriteQword(bonusAddr, 0); break; }
            }
        }

        g_prevAssigned.clear();
        ClearBonusTable(root);
        ClearTitleNames(root);
        g_titleNamesApplied = false;
    }

    static DWORD WINAPI FactionLordBonusThread(LPVOID) {
        RunOnce();

        while (!g_factionLordBonusStopRequested.load(std::memory_order_acquire)) {
            // 기존 Sleep(5000)은 OFF 요청을 최대 5초 동안 볼 수 없었습니다.
            // stop event를 사용하면 OFF 즉시 깨어나고, timeout일 때만 정기 재검사를 수행합니다.
            const DWORD waitResult = WaitForSingleObject(g_factionLordBonusStopEvent, 5000);
            if (waitResult == WAIT_OBJECT_0 ||
                g_factionLordBonusStopRequested.load(std::memory_order_acquire)) {
                break;
            }
            if (waitResult == WAIT_FAILED) {
                AddLog(u8"[군주보너스] stop event 대기 실패. worker를 종료합니다.");
                break;
            }

            RunOnce();
        }

        g_factionLordBonusRunning.store(false, std::memory_order_release);
        return 0;
    }

    void SetFactionLordBonus(bool enable) {
        std::lock_guard<std::mutex> lifecycleLock(g_factionLordLifecycleMutex);

        if (enable) {
            if (g_factionLordBonusEnabled &&
                g_factionLordBonusRunning.load(std::memory_order_acquire)) {
                return;
            }

            // 이전 worker의 handle이 남아 있으면 반드시 종료 확인 후 정리합니다.
            if (g_factionLordBonusThread) {
                WaitForSingleObject(g_factionLordBonusThread, INFINITE);
                CloseHandle(g_factionLordBonusThread);
                g_factionLordBonusThread = nullptr;
            }
            if (g_factionLordBonusStopEvent) {
                CloseHandle(g_factionLordBonusStopEvent);
                g_factionLordBonusStopEvent = nullptr;
            }

            g_factionLordBonusStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!g_factionLordBonusStopEvent) {
                g_factionLordBonusEnabled = false;
                g_factionLordBonusRunning.store(false, std::memory_order_release);
                AddLog(u8"[군주보너스] stop event 생성 실패");
                return;
            }

            g_factionLordBonusEnabled = true;
            g_factionLordBonusStopRequested.store(false, std::memory_order_release);
            g_factionLordBonusRunning.store(true, std::memory_order_release);
            g_titleNamesApplied = false;

            g_factionLordBonusThread = CreateThread(nullptr, 0,
                FactionLordBonusThread, nullptr, 0, nullptr);

            if (!g_factionLordBonusThread) {
                g_factionLordBonusEnabled = false;
                g_factionLordBonusStopRequested.store(true, std::memory_order_release);
                g_factionLordBonusRunning.store(false, std::memory_order_release);
                CloseHandle(g_factionLordBonusStopEvent);
                g_factionLordBonusStopEvent = nullptr;
                AddLog(u8"[군주보너스] 스레드 생성 실패");
            } else {
                AddLog(u8"[군주보너스] 활성화");
            }

        } else {
            if (!g_factionLordBonusEnabled && !g_factionLordBonusThread)
                return;

            g_factionLordBonusEnabled = false;
            g_factionLordBonusStopRequested.store(true, std::memory_order_release);

            // 5초 Sleep을 기다리지 않고 즉시 worker를 깨웁니다.
            if (g_factionLordBonusStopEvent)
                SetEvent(g_factionLordBonusStopEvent);

            if (g_factionLordBonusThread) {
                // stop event가 즉시 timed wait를 깨우므로, 여기서는 실제 RunOnce가 진행 중인
                // 짧은 구간만 기다립니다. 기존의 고정 5~6초 UI 정지는 발생하지 않습니다.
                WaitForSingleObject(g_factionLordBonusThread, INFINITE);
                CloseHandle(g_factionLordBonusThread);
                g_factionLordBonusThread = nullptr;
            }

            if (g_factionLordBonusStopEvent) {
                CloseHandle(g_factionLordBonusStopEvent);
                g_factionLordBonusStopEvent = nullptr;
            }

            uintptr_t root = ResolveRoot();
            if (root) ClearAll(root);
            else g_titleNamesApplied = false;

            AddLog(u8"[군주보너스] 비활성화");
        }
    }

} // namespace DX11Base
