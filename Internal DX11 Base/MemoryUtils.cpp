#include "MemoryUtils.h"
#include "showlog.h"
#include <vector>

namespace DX11Base {

    void AddLog(const char* fmt, ...);

    uintptr_t AllocNear(uintptr_t target, size_t size) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const uintptr_t granularity = si.dwAllocationGranularity;
        const uintptr_t maxDistance = 0x70000000ull;
        const uintptr_t low = (target > maxDistance) ? target - maxDistance : 0x10000ull;
        const uintptr_t high = (target <= UINTPTR_MAX - maxDistance) ? target + maxDistance : UINTPTR_MAX;
        const ULONGLONG started = GetTickCount64();
        unsigned queryCount = 0;
        unsigned allocAttempts = 0;

        auto tryRegion = [&](uintptr_t regionStart, uintptr_t regionEnd, bool preferHigh) -> uintptr_t {
            if (regionEnd <= regionStart || regionEnd - regionStart < size)
                return 0;

            uintptr_t candidate = 0;
            if (preferHigh) {
                uintptr_t latest = regionEnd - size;
                candidate = latest & ~(granularity - 1);
                if (candidate < regionStart)
                    return 0;
            } else {
                if (regionStart > UINTPTR_MAX - (granularity - 1))
                    return 0;
                candidate = (regionStart + granularity - 1) & ~(granularity - 1);
                if (candidate < regionStart || candidate > regionEnd - size)
                    return 0;
            }

            ++allocAttempts;
            void* result = VirtualAlloc((LPVOID)candidate, size,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
            return (uintptr_t)result;
        };

        uintptr_t cursor = target;
        while (cursor < high) {
            MEMORY_BASIC_INFORMATION mbi{};
            ++queryCount;
            if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) == 0)
                break;

            const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
            const uintptr_t regionEnd = regionStart + mbi.RegionSize;
            if (regionEnd <= cursor)
                break;

            if (mbi.State == MEM_FREE) {
                const uintptr_t clippedStart = (regionStart < low) ? low : regionStart;
                const uintptr_t clippedEnd = (regionEnd > high) ? high : regionEnd;
                const uintptr_t result = tryRegion(clippedStart, clippedEnd, false);
                if (result) {
                    const ULONGLONG elapsed = GetTickCount64() - started;
                    if (elapsed >= 50)
                        AddLog("[Perf:AllocNear] target=%p elapsed=%llums query=%u alloc=%u result=%p",
                               (void*)target, (unsigned long long)elapsed,
                               queryCount, allocAttempts, (void*)result);
                    return result;
                }
            }
            cursor = regionEnd;
        }

        cursor = target;
        while (cursor > low) {
            const uintptr_t probe = cursor - 1;
            MEMORY_BASIC_INFORMATION mbi{};
            ++queryCount;
            if (VirtualQuery((LPCVOID)probe, &mbi, sizeof(mbi)) == 0)
                break;

            const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
            const uintptr_t regionEnd = regionStart + mbi.RegionSize;
            if (regionEnd <= regionStart)
                break;

            if (mbi.State == MEM_FREE) {
                const uintptr_t clippedStart = (regionStart < low) ? low : regionStart;
                const uintptr_t clippedEnd = (regionEnd > high) ? high : regionEnd;
                const uintptr_t result = tryRegion(clippedStart, clippedEnd, true);
                if (result) {
                    const ULONGLONG elapsed = GetTickCount64() - started;
                    if (elapsed >= 50)
                        AddLog("[Perf:AllocNear] target=%p elapsed=%llums query=%u alloc=%u result=%p",
                               (void*)target, (unsigned long long)elapsed,
                               queryCount, allocAttempts, (void*)result);
                    return result;
                }
            }

            if (regionStart <= low)
                break;
            cursor = regionStart;
        }

        const ULONGLONG elapsed = GetTickCount64() - started;
        AddLog("[Perf:AllocNear] FAILED target=%p elapsed=%llums query=%u alloc=%u",
               (void*)target, (unsigned long long)elapsed, queryCount, allocAttempts);
        return 0;
    }

    bool ApplyJmp(uintptr_t hookAddr, uintptr_t caveAddr, size_t hookSize) {
        int64_t rel = (int64_t)caveAddr - (int64_t)(hookAddr + 5);
        
        if (rel < INT32_MIN || rel > INT32_MAX) {
            AddLog(u8"[ERROR] ApplyJmp: Relative jump distance exceeds 32-bit range.");
            return false;
        }

        DWORD old, tmp;
        if (!VirtualProtect((LPVOID)hookAddr, hookSize, PAGE_EXECUTE_READWRITE, &old)) {
            AddLog(u8"[ERROR] ApplyJmp: VirtualProtect failed.");
            return false;
        }

        uint8_t patch[16] = {};
        patch[0] = 0xE9; // JMP opcode
        *(int32_t*)&patch[1] = (int32_t)rel;

        // Fill remaining hookSize with NOPs
        for (size_t i = 5; i < hookSize; ++i) {
            patch[i] = 0x90; // NOP
        }

        memcpy((void*)hookAddr, patch, hookSize);
        VirtualProtect((LPVOID)hookAddr, hookSize, old, &tmp);
        return true;
    }

    void RestoreBytes(uintptr_t hookAddr, const uint8_t* original, size_t size) {
        DWORD old, tmp;
        if (VirtualProtect((LPVOID)hookAddr, size, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy((void*)hookAddr, original, size);
            VirtualProtect((LPVOID)hookAddr, size, old, &tmp);
        }
    }

}
