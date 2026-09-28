#include "RulerTransferProposal.h"
#include "../../MemoryUtils.h"
#include "../../Config.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace DX11Base {

int iRulerTransferProposalMode = 0;

namespace {
constexpr uintptr_t kCallsite = 0x19021F5;
constexpr uintptr_t kNative = 0x153E0;
constexpr uintptr_t kGlobalPtr = 0x2E98BC8;
constexpr uint8_t kOriginalCall[5] = {0xE8, 0xE6, 0x31, 0x71, 0xFE};
constexpr uint8_t kAssertPrefix[12] = {0x48,0x8B,0xCB,0xE8,0xE6,0x31,0x71,0xFE,0x84,0xC0,0x75,0xB3};
constexpr uint8_t kAssertPrev[12] = {0xE8,0xE5,0xF4,0xE0,0xFF,0x0F,0xB6,0xF8,0x3C,0x01,0x74,0x1E};
constexpr const char* kConfigKey = "bRulerTransferProposal";

uintptr_t gBase = 0;
uintptr_t gThunk = 0;
uintptr_t gCapturedRdi = 0;
uintptr_t gCapturedRsi = 0;
bool gApplied = false;

using NativeFn = uint8_t(__fastcall*)(uintptr_t);

bool ReadEq(uintptr_t addr, const void* expected, size_t size) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(addr), expected, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteBytes(uintptr_t addr, const void* data, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(reinterpret_cast<void*>(addr), data, size);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(addr), size);
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &ignored);
    return true;
}

bool WriteCall(uintptr_t site, uintptr_t target) {
    const int64_t rel = static_cast<int64_t>(target) - static_cast<int64_t>(site + 5);
    if (rel < INT32_MIN || rel > INT32_MAX)
        return false;
    uint8_t patch[5] = {0xE8,0,0,0,0};
    const int32_t rel32 = static_cast<int32_t>(rel);
    std::memcpy(&patch[1], &rel32, sizeof(rel32));
    return WriteBytes(site, patch, sizeof(patch));
}

uint8_t __fastcall RulerTransferProposalHook(uintptr_t rbxValue) {
    auto native = reinterpret_cast<NativeFn>(gBase + kNative);
    uint8_t result = native(rbxValue);
    if (result == 0 || iRulerTransferProposalMode == 0)
        return result;

    const uint8_t role = static_cast<uint8_t>(gCapturedRdi & 0xFF);
    if (iRulerTransferProposalMode == 1) {
        if (role != 4)
            return result;
    } else if (iRulerTransferProposalMode == 2) {
        if (role != 2 && role != 4)
            return result;
    } else {
        return result;
    }

    __try {
        const uintptr_t root = *reinterpret_cast<uintptr_t*>(gBase + kGlobalPtr);
        if (!root || *reinterpret_cast<uintptr_t*>(root + 0xE0) != gCapturedRsi)
            return result;

        const uintptr_t owner = *reinterpret_cast<uintptr_t*>(gCapturedRsi + 0x18);
        if (!owner || *reinterpret_cast<uintptr_t*>(owner + 0x10) != rbxValue)
            return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return result;
    }

    return 0;
}

uintptr_t BuildThunk(uintptr_t nearAddress) {
    const uintptr_t thunk = AllocNear(nearAddress, 128);
    if (!thunk)
        return 0;

    uint8_t* p = reinterpret_cast<uint8_t*>(thunk);
    size_t i = 0;
    auto movRaxImm = [&](uintptr_t value) {
        p[i++] = 0x48; p[i++] = 0xB8;
        std::memcpy(&p[i], &value, sizeof(value));
        i += sizeof(value);
    };

    movRaxImm(reinterpret_cast<uintptr_t>(&gCapturedRdi));
    p[i++] = 0x48; p[i++] = 0x89; p[i++] = 0x38;

    movRaxImm(reinterpret_cast<uintptr_t>(&gCapturedRsi));
    p[i++] = 0x48; p[i++] = 0x89; p[i++] = 0x30;

    movRaxImm(reinterpret_cast<uintptr_t>(&RulerTransferProposalHook));
    p[i++] = 0xFF; p[i++] = 0xE0;

    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(thunk), i);
    return thunk;
}

bool ValidateOriginal() {
    return ReadEq(gBase + 0x19021F2, kAssertPrefix, sizeof(kAssertPrefix)) &&
           ReadEq(gBase + 0x19021E6, kAssertPrev, sizeof(kAssertPrev));
}

bool ReadSavedPreference(bool& enabled) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
        return false;

    const std::string key = std::string("\"") + kConfigKey + "\"";
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(key) == std::string::npos)
            continue;
        enabled = line.find("true") != std::string::npos;
        return true;
    }
    return false;
}
}

bool SetRulerTransferProposalMode(int mode) {
    if (mode < 0 || mode > 2)
        mode = 0;

    gBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (!gBase)
        return false;

    if (mode == 0) {
        if (gApplied) {
            if (!WriteBytes(gBase + kCallsite, kOriginalCall, sizeof(kOriginalCall)))
                return false;
            gApplied = false;
        }
        iRulerTransferProposalMode = 0;
        SaveRulerTransferProposalPreference(false);
        return true;
    }

    if (!gApplied) {
        if (!ValidateOriginal())
            return false;
        if (!gThunk)
            gThunk = BuildThunk(gBase + kCallsite);
        if (!gThunk || !WriteCall(gBase + kCallsite, gThunk))
            return false;
        gApplied = true;
    }

    iRulerTransferProposalMode = mode;
    SaveRulerTransferProposalPreference(true);
    return true;
}

int GetRulerTransferProposalMode() {
    return iRulerTransferProposalMode;
}

bool IsRulerTransferProposalApplied() {
    return gApplied;
}

bool SaveRulerTransferProposalPreference(bool enabled) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open())
        return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string data = ss.str();

    const std::string key = std::string("\"") + kConfigKey + "\"";
    const size_t existing = data.find(key);
    if (existing != std::string::npos) {
        size_t lineStart = data.rfind('\n', existing);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        size_t lineEnd = data.find('\n', existing);
        lineEnd = lineEnd == std::string::npos ? data.size() : lineEnd + 1;
        data.erase(lineStart, lineEnd - lineStart);
    }

    const size_t configEnd = data.find("\"config_end\"");
    if (configEnd == std::string::npos)
        return false;

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = insertPos == std::string::npos ? configEnd : insertPos + 1;
    const std::string line = std::string("  \"") + kConfigKey + "\": " +
                             (enabled ? "true" : "false") + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << data;
    out.flush();
    return out.good();
}

bool LoadRulerTransferProposalPreference() {
    bool enabled = false;
    if (!ReadSavedPreference(enabled)) {
        SetRulerTransferProposalMode(0);
        return false;
    }
    return SetRulerTransferProposalMode(enabled ? 2 : 0);
}

} // namespace DX11Base
