#include "MemoryUtils.h"
#include "showlog.h"
#include <vector>

namespace DX11Base {

    void AddLog(const char* fmt, ...);

    uintptr_t AllocNear(uintptr_t target, size_t size) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uintptr_t pageSize = si.dwPageSize;

        uintptr_t high = target + 0x70000000;
        for (uintptr_t addr = (target + pageSize) & ~(pageSize - 1); addr < high; addr += pageSize) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
            if (mbi.State == MEM_FREE) {
                void* result = VirtualAlloc((LPVOID)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (result) return (uintptr_t)result;
            }
            addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize - pageSize;
        }

        uintptr_t low = (target > 0x70000000) ? target - 0x70000000 : 0x10000;
        for (uintptr_t addr = (target - pageSize) & ~(pageSize - 1); addr > low; addr -= pageSize) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
            if (mbi.State == MEM_FREE) {
                void* result = VirtualAlloc((LPVOID)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (result) return (uintptr_t)result;
            }
            if (addr < mbi.RegionSize + pageSize) break;
            addr = (uintptr_t)mbi.BaseAddress;
        }
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
