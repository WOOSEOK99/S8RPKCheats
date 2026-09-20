#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include "../System/MonthCapture.h"
#include "../../Cheats.h"
#include "pch.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <psapi.h>
#include <iostream>
#include <winnls.h>
#include <algorithm>
#include <map>

namespace DX11Base {

    // Unicode Normalization Form C (NFC) 로 변환하는 유틸리티
    std::string NormalizeUtf8(const std::string& str) {
        if (str.empty()) return "";
        
        // UTF-8 -> Wide
        int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (wlen <= 0) return str;
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);

        // Normalize to NFC
        int nlen = NormalizeString(NormalizationC, wstr.c_str(), -1, NULL, 0);
        if (nlen <= 0) return str;
        std::wstring nstr(nlen, 0);
        NormalizeString(NormalizationC, wstr.c_str(), -1, &nstr[0], nlen);

        // Wide -> UTF-8
        int u8len = WideCharToMultiByte(CP_UTF8, 0, nstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (u8len <= 0) return str;
        std::string u8str(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, nstr.c_str(), -1, &u8str[0], u8len, NULL, NULL);
        
        size_t actualLen = strlen(u8str.c_str());
        u8str.resize(actualLen);
        return u8str;
    }

    extern HMODULE g_hModule;

    std::unordered_map<int, std::string> g_officerNames;
    bool g_namesLoaded = false;
    std::unordered_map<int, EffectDef> g_effectDefs;
    bool g_effectsLoaded = false;

    RosterStats SafeReadRosterStats(uintptr_t targetBase) {
        RosterStats stats = { false };
        __try {
            stats.id_08 = *(unsigned short*)(targetBase + 0x08);
            stats.id_2e = *(unsigned short*)(targetBase + 0x2E);
            stats.gold = *(unsigned int*)(targetBase + 0xE8);
            stats.merit = *(unsigned short*)(targetBase + 0x100);
            stats.repI = *(unsigned short*)(targetBase + 0x108);
            stats.sp = *(unsigned char*)(targetBase + 0xED);
            stats.valid = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            stats.valid = false;
        }
        return stats;
    }


    namespace {
        constexpr uintptr_t kCurrentSynergeticPtrOffset = 0x547330;
        constexpr uintptr_t kCurrentRelationshipPtrOffset = 0x57AE38;
        constexpr uintptr_t kLegacySynergeticPtrOffset = 0x433210;
        constexpr uintptr_t kLegacyRelationshipPtrOffset = 0x462BA8;

        bool SafeRelRead8(uintptr_t addr, uint8_t* out) {
            if (!out) return false;
            __try {
                *out = *(uint8_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool SafeRelRead16(uintptr_t addr, uint16_t* out) {
            if (!out) return false;
            __try {
                *out = *(uint16_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool SafeRelReadPtr(uintptr_t addr, uintptr_t* out) {
            if (!out) return false;
            __try {
                *out = *(uintptr_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool IsRosterOfficerPtr(uintptr_t ptr, uintptr_t rosterBase) {
            if (ptr < rosterBase)
                return false;
            const uintptr_t delta = ptr - rosterBase;
            if (delta >= (uintptr_t)5102 * 0x3D0)
                return false;
            return (delta % 0x3D0) == 0;
        }

        bool ReadOfficerIdFromRosterPtr(uintptr_t ptr, uintptr_t rosterBase, uint16_t* outId) {
            if (!outId || !IsRosterOfficerPtr(ptr, rosterBase))
                return false;
            uint16_t id = 0;
            if (!SafeRelRead16(ptr + 0x08, &id) || id < 1 || id > 5102)
                return false;
            *outId = id;
            return true;
        }

        int ScoreSynergeticTable(uintptr_t base, uintptr_t rosterBase) {
            if (base <= 0x10000 || rosterBase <= 0x10000)
                return -1;

            int valid = 0;
            int invalid = 0;
            for (int i = 0; i < 600; ++i) {
                const uintptr_t slot = base + (uintptr_t)i * 0x20;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x18, &relation))
                    return -1;
                if (relation == 0)
                    continue;
                if (relation != 1 && relation != 2) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }

                uintptr_t p1 = 0, p2 = 0;
                if (!SafeRelReadPtr(slot + 0x08, &p1) ||
                    !SafeRelReadPtr(slot + 0x10, &p2) ||
                    !IsRosterOfficerPtr(p1, rosterBase) ||
                    !IsRosterOfficerPtr(p2, rosterBase)) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }
                ++valid;
            }
            return valid;
        }

        int ScoreRelationshipTable(uintptr_t base, uintptr_t rosterBase) {
            if (base <= 0x10000 || rosterBase <= 0x10000)
                return -1;

            int valid = 0;
            int invalid = 0;
            for (int i = 0; i < 128; ++i) {
                const uintptr_t slot = base + (uintptr_t)i * 0x40;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x08, &relation))
                    return -1;
                if (relation == 0)
                    continue;
                if (relation < 1 || relation > 4) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }

                uintptr_t first = 0, second = 0;
                if (!SafeRelReadPtr(slot + 0x10, &first) ||
                    !SafeRelReadPtr(slot + 0x18, &second) ||
                    !IsRosterOfficerPtr(first, rosterBase) ||
                    !IsRosterOfficerPtr(second, rosterBase)) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }
                ++valid;
            }
            return valid;
        }

        bool IsReadableRelationshipScanRegion(const MEMORY_BASIC_INFORMATION& mbi) {
            if (mbi.State != MEM_COMMIT)
                return false;
            if (mbi.Protect & PAGE_GUARD)
                return false;
            return (mbi.Protect & 0xFF) != PAGE_NOACCESS;
        }

        bool TryResolveSynergeticTable(uintptr_t gameBase, uintptr_t rosterBase, uintptr_t* outBase) {
            if (!outBase) return false;
            *outBase = 0;

            static uintptr_t s_cachedGameBase = 0;
            static uintptr_t s_cachedRosterBase = 0;
            static uintptr_t s_cachedTableBase = 0;
            if (s_cachedGameBase == gameBase &&
                s_cachedRosterBase == rosterBase &&
                s_cachedTableBase > 0x10000 &&
                ScoreSynergeticTable(s_cachedTableBase, rosterBase) >= 1) {
                *outBase = s_cachedTableBase;
                return true;
            }

            const uintptr_t offsets[] = {
                kCurrentSynergeticPtrOffset,
                kLegacySynergeticPtrOffset
            };
            for (uintptr_t off : offsets) {
                uintptr_t ptr = 0;
                if (!SafeRelReadPtr(gameBase + off, &ptr) || ptr <= 0x10000)
                    continue;
                const uintptr_t candidate = ptr + 0xA0;
                if (ScoreSynergeticTable(candidate, rosterBase) >= 1) {
                    s_cachedGameBase = gameBase;
                    s_cachedRosterBase = rosterBase;
                    s_cachedTableBase = candidate;
                    *outBase = candidate;
                    return true;
                }
            }

            // 고정 오프셋이 바뀐 빌드/상태에서는 디버그에서 검증된 구조 스캔 방식으로 찾는다.
            const uintptr_t scanEnd = gameBase + 0x800000;
            uintptr_t cursor = gameBase;
            while (cursor < scanEnd) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) != sizeof(mbi))
                    break;

                uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
                uintptr_t regionEnd = regionStart + mbi.RegionSize;
                if (regionEnd <= cursor)
                    break;
                if (regionStart < gameBase)
                    regionStart = gameBase;
                if (regionEnd > scanEnd)
                    regionEnd = scanEnd;

                if (IsReadableRelationshipScanRegion(mbi)) {
                    uintptr_t addr = (regionStart + 7) & ~(uintptr_t)7;
                    for (; addr + 8 <= regionEnd; addr += 8) {
                        uintptr_t ptr = 0;
                        if (!SafeRelReadPtr(addr, &ptr) || ptr <= 0x10000)
                            continue;

                        const uintptr_t candidate = ptr + 0xA0;
                        if (ScoreSynergeticTable(candidate, rosterBase) < 1)
                            continue;

                        s_cachedGameBase = gameBase;
                        s_cachedRosterBase = rosterBase;
                        s_cachedTableBase = candidate;
                        *outBase = candidate;
                        return true;
                    }
                }
                cursor = regionEnd;
            }
            return false;
        }

        bool TryResolveRelationshipTable(uintptr_t gameBase, uintptr_t rosterBase, uintptr_t* outBase) {
            if (!outBase) return false;
            *outBase = 0;

            const uintptr_t offsets[] = {
                kCurrentRelationshipPtrOffset,
                kLegacyRelationshipPtrOffset
            };
            for (uintptr_t off : offsets) {
                uintptr_t ptr = 0;
                if (!SafeRelReadPtr(gameBase + off, &ptr) || ptr <= 0x10080)
                    continue;

                const uintptr_t biases[] = {0x80, 0xC0};
                for (uintptr_t bias : biases) {
                    const uintptr_t candidate = ptr - bias;
                    if (ScoreRelationshipTable(candidate, rosterBase) >= 1) {
                        *outBase = candidate;
                        return true;
                    }
                }
            }
            return false;
        }

        void AddUniqueRelationshipId(std::vector<uint16_t>& values, uint16_t id) {
            if (id == 0)
                return;
            if (std::find(values.begin(), values.end(), id) == values.end())
                values.push_back(id);
        }

        bool ContainsRelationshipId(const OfficerRelationshipInfo& info, uint16_t id) {
            const std::vector<uint16_t>* groups[] = {
                &info.swornBrothers,
                &info.spouses,
                &info.enemies,
                &info.rivals
            };
            for (const auto* group : groups) {
                if (std::find(group->begin(), group->end(), id) != group->end())
                    return true;
            }
            return false;
        }

        constexpr uintptr_t kCurrentAffinityBaseOffset = 0x24206;
        constexpr int kAffinityOfficerCount = 1650;

        int GetAffinityCompressedIndex(uint16_t id) {
            if (id >= 1 && id <= 1000)
                return (int)id;
            if (id >= 2001 && id <= 2100)
                return (int)id - 1000;
            if (id >= 4001 && id <= 4200)
                return (int)id - 2900;
            if (id >= 5001 && id <= 5100)
                return (int)id - 3700;
            if (id >= 3001 && id <= 3150)
                return (int)id - 1600;
            if (id >= 5101 && id <= 5200)
                return (int)id - 3550;
            return -1;
        }

        bool CalcAffinityPairOffset(
            int index1, int index2, uintptr_t* outOffset) {
            if (!outOffset ||
                index1 < 1 || index2 < 1 ||
                index1 > kAffinityOfficerCount ||
                index2 > kAffinityOfficerCount ||
                index1 == index2)
                return false;

            const int leftIndex = (std::min)(index1, index2);
            const int rightIndex = (std::max)(index1, index2);
            const uint64_t left = (uint64_t)(leftIndex - 1);
            const uint64_t width =
                (uint64_t)(kAffinityOfficerCount - 1);
            const uint64_t rowStart =
                left * (2ull * width - (left - 1ull)) / 2ull;
            *outOffset =
                (uintptr_t)(rowStart +
                (uint64_t)(rightIndex - leftIndex - 1));
            return true;
        }
    } // namespace


    bool GetOfficerAffinity(
        uint16_t officerId1,
        uint16_t officerId2,
        uint8_t& outAffinity) {
        outAffinity = 0;
        if (officerId1 == 0 || officerId2 == 0 ||
            officerId1 == officerId2)
            return false;

        const int index1 =
            GetAffinityCompressedIndex(officerId1);
        const int index2 =
            GetAffinityCompressedIndex(officerId2);
        uintptr_t pairOffset = 0;
        if (!CalcAffinityPairOffset(
                index1, index2, &pairOffset))
            return false;

        const uintptr_t dataCenter =
            GetScenarioDataCenterAddress();
        if (dataCenter <= 0x10000)
            return false;

        uint8_t value = 0;
        if (!SafeRelRead8(
                dataCenter +
                    kCurrentAffinityBaseOffset +
                    pairOffset,
                &value))
            return false;

        if (value > 100)
            return false;

        outAffinity = value;
        return true;
    }

    bool GetOfficerRelationshipInfoBatch(
        const std::vector<uintptr_t>& officerBases,
        std::vector<OfficerRelationshipInfo>& outInfos) {
        outInfos.assign(officerBases.size(), OfficerRelationshipInfo{});

        if (officerBases.empty())
            return false;

        const uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
        const uintptr_t gameBase = GetGameBase();
        uintptr_t rosterBase = 0;
        if (!exe || gameBase <= 0x10000 ||
            !TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
            rosterBase <= 0x10000)
            return false;

        // 요청된 무장 ID -> 결과 인덱스. 관계 테이블은 한 번만 훑고,
        // 슬롯에 요청 무장이 들어 있을 때 해당 결과에 바로 누적한다.
        std::unordered_map<uint16_t, size_t> selectedById;
        selectedById.reserve(officerBases.size());
        for (size_t i = 0; i < officerBases.size(); ++i) {
            const uintptr_t officerBase = officerBases[i];
            uint16_t id = 0;
            if (officerBase <= 0x10000 ||
                !SafeRelRead16(officerBase + 0x08, &id) ||
                id < 1 || id > 5102)
                continue;
            selectedById[id] = i;
        }

        if (selectedById.empty())
            return false;

        uintptr_t synerBase = 0;
        uintptr_t relationBase = 0;
        const bool hasSyner =
            TryResolveSynergeticTable(gameBase, rosterBase, &synerBase);
        const bool hasRelation =
            TryResolveRelationshipTable(gameBase, rosterBase, &relationBase);

        if (!hasSyner && !hasRelation)
            return false;

        // 직접 관계(의형제/배우자/혐오/경쟁)를 테이블 전체에서 한 번 수집한다.
        // 기존 단일 조회와 동일하게 한 슬롯의 최대 5명 관계를 서로 연결한다.
        if (hasRelation) {
            for (int i = 0; i < 3000; ++i) {
                const uintptr_t slot = relationBase + (uintptr_t)i * 0x40;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x08, &relation))
                    break;
                if (relation < 1 || relation > 4)
                    continue;

                uint16_t memberIds[5]{};
                bool readOk = true;
                for (int j = 0; j < 5; ++j) {
                    uintptr_t memberPtr = 0;
                    if (!SafeRelReadPtr(
                            slot + 0x10 + (uintptr_t)j * 8,
                            &memberPtr)) {
                        readOk = false;
                        break;
                    }
                    if (memberPtr == 0)
                        continue;

                    uint16_t id = 0;
                    if (ReadOfficerIdFromRosterPtr(
                            memberPtr, rosterBase, &id))
                        memberIds[j] = id;
                }

                if (!readOk)
                    break;

                for (int source = 0; source < 5; ++source) {
                    const uint16_t selectedId = memberIds[source];
                    if (selectedId == 0)
                        continue;

                    const auto selectedIt =
                        selectedById.find(selectedId);
                    if (selectedIt == selectedById.end())
                        continue;

                    OfficerRelationshipInfo& info =
                        outInfos[selectedIt->second];

                    for (int target = 0; target < 5; ++target) {
                        const uint16_t memberId = memberIds[target];
                        if (memberId == 0 || memberId == selectedId)
                            continue;

                        switch (relation) {
                        case 1:
                            AddUniqueRelationshipId(
                                info.swornBrothers, memberId);
                            break;
                        case 2:
                            AddUniqueRelationshipId(
                                info.spouses, memberId);
                            break;
                        case 3:
                            AddUniqueRelationshipId(
                                info.enemies, memberId);
                            break;
                        case 4:
                            AddUniqueRelationshipId(
                                info.rivals, memberId);
                            break;
                        }
                    }
                }
            }
        }

        // 숙명 관계도 전체 테이블을 한 번만 훑는다.
        // 직접 관계와 중복되는 항목은 기존 단일 조회와 동일하게 제외한다.
        if (hasSyner) {
            for (int i = 0; i < 5000; ++i) {
                const uintptr_t slot = synerBase + (uintptr_t)i * 0x20;
                uintptr_t p1 = 0, p2 = 0;
                uint8_t relation = 0, occurred = 0;
                if (!SafeRelReadPtr(slot + 0x08, &p1) ||
                    !SafeRelReadPtr(slot + 0x10, &p2) ||
                    !SafeRelRead8(slot + 0x18, &relation) ||
                    !SafeRelRead8(slot + 0x19, &occurred))
                    break;

                if ((relation != 1 && relation != 2) ||
                    occurred != 0)
                    continue;

                uint16_t id1 = 0, id2 = 0;
                const bool valid1 =
                    ReadOfficerIdFromRosterPtr(p1, rosterBase, &id1);
                const bool valid2 =
                    ReadOfficerIdFromRosterPtr(p2, rosterBase, &id2);
                if (!valid1 || !valid2)
                    continue;

                auto addSynerRelationship =
                    [&](uint16_t selectedId, uint16_t otherId) {
                    if (selectedId == 0 || otherId == 0 ||
                        selectedId == otherId)
                        return;

                    const auto selectedIt =
                        selectedById.find(selectedId);
                    if (selectedIt == selectedById.end())
                        return;

                    OfficerRelationshipInfo& info =
                        outInfos[selectedIt->second];
                    if (ContainsRelationshipId(info, otherId))
                        return;

                    if (relation == 1)
                        AddUniqueRelationshipId(
                            info.antipathetic, otherId);
                    else
                        AddUniqueRelationshipId(
                            info.synergetic, otherId);
                };

                addSynerRelationship(id1, id2);
                addSynerRelationship(id2, id1);
            }
        }

        for (const auto& entry : selectedById)
            outInfos[entry.second].valid = true;

        return true;
    }

    bool GetOfficerRelationshipInfo(
        uintptr_t officerBase,
        OfficerRelationshipInfo& outInfo) {
        std::vector<uintptr_t> officerBases = { officerBase };
        std::vector<OfficerRelationshipInfo> infos;
        if (!GetOfficerRelationshipInfoBatch(
                officerBases, infos) ||
            infos.empty() || !infos[0].valid) {
            outInfo = OfficerRelationshipInfo{};
            return false;
        }

        outInfo = infos[0];
        return true;
    }

    bool GetOfficerTalentDetailed(uintptr_t officerBase, int slot, TalentInfo& outInfo) {
        if (officerBase < 0x10000) return false;
        memset(&outInfo, 0, sizeof(TalentInfo));
        __try {
            uintptr_t pointerArrayAddr = officerBase + 0x88 + (slot * 8);
            uintptr_t realDataAddr = *(uintptr_t*)pointerArrayAddr;
            if (realDataAddr == 0 || realDataAddr < 0x10000) return false;

            outInfo.id = *(uint16_t*)(realDataAddr + 0x08);
            for (int i = 0; i < 6; i++) {
                uintptr_t effectAddr = realDataAddr + 0x0A + (i * 6);
                uint16_t eId = *(uint16_t*)(effectAddr);
                if (eId == 0) break;
                outInfo.effects[i].effectId = eId;
                outInfo.effects[i].val1 = *(uint16_t*)(effectAddr + 2);
                outInfo.effects[i].val2 = *(uint16_t*)(effectAddr + 4);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool IsUtf8(const std::string& str) {
        int i = 0;
        int n = (int)str.length();
        while (i < n) {
            unsigned char c = (unsigned char)str[i];
            if (c <= 0x7F) i++;
            else if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= n || (str[i + 1] & 0xC0) != 0x80) return false;
                i += 2;
            }
            else if ((c & 0xF0) == 0xE0) {
                if (i + 2 >= n || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80) return false;
                i += 3;
            }
            else if ((c & 0xF8) == 0xF0) {
                if (i + 3 >= n || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80 || (str[i + 3] & 0xC0) != 0x80) return false;
                i += 4;
            }
            else return false;
        }
        return true;
    }

    std::string AnsiToUtf8(const std::string& str) {
        if (str.empty()) return "";
        // [수정] 이미 UTF-8이면 변환하지 않음 (깨짐 방지)
        if (IsUtf8(str)) return str;

        int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
        if (len <= 0) return str;
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], len);
        int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (u8len <= 0) return str;
        std::string u8str(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, NULL, NULL);
        size_t actualLen = strlen(u8str.c_str());
        u8str.resize(actualLen);
        return u8str;
    }

    void LoadOfficerNames() {
        if (g_namesLoaded) return;
        g_namesLoaded = true;
        char path[MAX_PATH];
        if (GetModuleFileNameA(g_hModule, path, MAX_PATH)) {
            std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_cheat_char.json").string();
            std::ifstream file(jsonPath);
            if (file.is_open()) {
                std::string line;
                int currentId = -1;
                std::string currentName = "";
                std::string currentJa = "";
                while (std::getline(file, line)) {
                    if (line.find("{") != std::string::npos) { currentId = -1; currentName = ""; currentJa = ""; }
                    size_t idPos = line.find("\"id\"");
                    if (idPos != std::string::npos) {
                        size_t colon = line.find(":", idPos);
                        if (colon != std::string::npos) { try { currentId = std::stoi(line.substr(colon + 1)); } catch (...) { currentId = -1; } }
                    }
                    size_t namePos = line.find("\"name\"");
                    if (namePos != std::string::npos) {
                        size_t colon = line.find(":", namePos);
                        if (colon != std::string::npos) {
                            size_t firstQuote = line.find("\"", colon);
                            if (firstQuote != std::string::npos) {
                                size_t secondQuote = line.find("\"", firstQuote + 1);
                                if (secondQuote != std::string::npos) currentName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                            }
                        }
                    }
                    size_t jaPos = line.find("\"ja\"");
                    if (jaPos != std::string::npos) {
                        size_t colon = line.find(":", jaPos);
                        if (colon != std::string::npos) {
                            size_t firstQuote = line.find("\"", colon);
                            if (firstQuote != std::string::npos) {
                                size_t secondQuote = line.find("\"", firstQuote + 1);
                                if (secondQuote != std::string::npos) currentJa = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                            }
                        }
                    }
                    if (line.find("}") != std::string::npos && currentId != -1) {
                        if (!currentName.empty()) {
                            std::string u8Name = NormalizeUtf8(AnsiToUtf8(currentName));
                            std::string u8Ja = NormalizeUtf8(AnsiToUtf8(currentJa));
                            if (!u8Ja.empty()) g_officerNames[currentId] = u8Name + u8"(" + u8Ja + u8")";
                            else g_officerNames[currentId] = u8Name;
                        }
                    }
                }
                file.close();
            }
        }
    }

    namespace {

    struct CharJsonEntry {
        int id = 0;
        std::string name;
        std::string ja;
    };

    std::string JsonEscapeQuoted(const std::string &s) {
        std::string r;
        r.reserve(s.size() + 16);
        for (unsigned char c : s) {
            switch (c) {
            case '"':
                r += "\\\"";
                break;
            case '\\':
                r += "\\\\";
                break;
            case '\n':
                r += "\\n";
                break;
            case '\r':
                r += "\\r";
                break;
            case '\t':
                r += "\\t";
                break;
            default:
                r += static_cast<char>(c);
                break;
            }
        }
        return r;
    }

    bool ParseCharJsonFileToMap(const std::string &jsonPath, std::map<int, CharJsonEntry> &out) {
        std::ifstream file(jsonPath);
        if (!file.is_open())
            return false;
        std::string line;
        int currentId = -1;
        std::string currentName = "";
        std::string currentJa = "";
        while (std::getline(file, line)) {
            if (line.find("{") != std::string::npos) {
                currentId = -1;
                currentName = "";
                currentJa = "";
            }
            size_t idPos = line.find("\"id\"");
            if (idPos != std::string::npos) {
                size_t colon = line.find(":", idPos);
                if (colon != std::string::npos) {
                    try {
                        currentId = std::stoi(line.substr(colon + 1));
                    } catch (...) {
                        currentId = -1;
                    }
                }
            }
            size_t namePos = line.find("\"name\"");
            if (namePos != std::string::npos) {
                size_t colon = line.find(":", namePos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            size_t jaPos = line.find("\"ja\"");
            if (jaPos != std::string::npos) {
                size_t colon = line.find(":", jaPos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentJa = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            if (line.find("}") != std::string::npos && currentId != -1) {
                CharJsonEntry e;
                e.id = currentId;
                e.name = NormalizeUtf8(AnsiToUtf8(currentName));
                e.ja = NormalizeUtf8(AnsiToUtf8(currentJa));
                out[currentId] = std::move(e);
            }
        }
        file.close();
        return true;
    }

    bool WriteCharJsonMap(const std::string &jsonPath, const std::map<int, CharJsonEntry> &byId) {
        std::ofstream o(jsonPath, std::ios::binary | std::ios::trunc);
        if (!o.is_open())
            return false;
        o << "[\n";
        size_t i = 0;
        const size_t n = byId.size();
        for (const auto &kv : byId) {
            const CharJsonEntry &e = kv.second;
            o << "  {\n";
            o << "    \"id\": " << e.id << ",\n";
            o << "    \"name\": \"" << JsonEscapeQuoted(e.name) << "\",\n";
            o << "    \"ja\": \"" << JsonEscapeQuoted(e.ja) << "\"\n";
            o << "  }";
            if (++i < n)
                o << ",";
            o << "\n";
        }
        o << "]\n";
        return true;
    }

    } // namespace

    bool SaveOfficerNameToJson(int id, const std::string &nameUtf8) {
        char path[MAX_PATH];
        if (!GetModuleFileNameA(g_hModule, path, MAX_PATH))
            return false;
        std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_cheat_char.json").string();

        std::map<int, CharJsonEntry> byId;
        const bool parsed = ParseCharJsonFileToMap(jsonPath, byId);
        if (!parsed && std::filesystem::exists(jsonPath))
            return false;

        std::string normalized = NormalizeUtf8(nameUtf8);
        auto it = byId.find(id);
        if (it == byId.end()) {
            CharJsonEntry e;
            e.id = id;
            e.name = normalized;
            e.ja = "";
            byId[id] = std::move(e);
        } else {
            it->second.name = normalized;
        }

        if (!WriteCharJsonMap(jsonPath, byId))
            return false;

        auto it2 = byId.find(id);
        if (it2 == byId.end() || it2->second.name.empty())
            g_officerNames.erase(id);
        else if (!it2->second.ja.empty())
            g_officerNames[id] = it2->second.name + u8"(" + it2->second.ja + u8")";
        else
            g_officerNames[id] = it2->second.name;
        return true;
    }

    void LoadEffectDefinitions() {
        if (g_effectsLoaded) return;
        g_effectsLoaded = true;
        char path[MAX_PATH];
        if (GetModuleFileNameA(g_hModule, path, MAX_PATH)) {
            std::string jsonPath = std::filesystem::path(path).parent_path().append("effect_definitions.json").string();
            std::ifstream file(jsonPath);
            if (file.is_open()) {
                std::string line; 
                EffectDef currentDef = { -1 }; 
                std::string currentKey = "";
                while (std::getline(file, line)) {
                    if (line.find("{") != std::string::npos && line.find(":") == std::string::npos) { currentDef = { -1 }; currentKey = ""; }
                    size_t typePos = line.find("\"type\"");
                    if (typePos != std::string::npos) {
                        size_t colon = line.find(":", typePos);
                        if (colon != std::string::npos) {
                            try {
                                std::string valStr = line.substr(colon + 1);
                                size_t comma = valStr.find(",");
                                if (comma != std::string::npos) valStr = valStr.substr(0, comma);
                                currentDef.type = std::stoi(valStr);
                            } catch (...) {}
                        }
                    }
                    size_t tempPos = line.find("\"template\"");
                    if (tempPos != std::string::npos) {
                        size_t colon = line.find(":", tempPos);
                        if (colon != std::string::npos) {
                            size_t firstQuote = line.find("\"", colon);
                            if (firstQuote != std::string::npos) {
                                size_t secondQuote = line.find("\"", firstQuote + 1);
                                if (secondQuote != std::string::npos) currentDef.templateStr = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                            }
                        }
                    }
                    if (line.find("\"valueMap\"") != std::string::npos) currentKey = "v";
                    else if (line.find("\"paramMap\"") != std::string::npos) currentKey = "p";
                    if (currentKey != "") {
                        size_t firstQuote = line.find("\""), colon = line.find(":");
                        if (firstQuote != std::string::npos && colon != std::string::npos && firstQuote < colon) {
                            size_t secondQuote = line.find("\"", firstQuote + 1);
                            if (secondQuote != std::string::npos && secondQuote < colon) {
                                std::string keyStr = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                                size_t valFirstQuote = line.find("\"", colon);
                                if (valFirstQuote != std::string::npos) {
                                    size_t valSecondQuote = line.find("\"", valFirstQuote + 1);
                                    if (valSecondQuote != std::string::npos) {
                                        std::string valStr = line.substr(valFirstQuote + 1, valSecondQuote - valFirstQuote - 1);
                                        try {
                                            int k = std::stoi(keyStr);
                                            if (currentKey == "v") currentDef.valueMap[k] = valStr;
                                            else currentDef.paramMap[k] = valStr;
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                    }
                    if (line.find("}") != std::string::npos) {
                        if (currentKey != "") currentKey = "";
                        else if (currentDef.type != -1) { g_effectDefs[currentDef.type] = currentDef; currentDef.type = -1; }
                    }
                }
            }
        }
    }

    const char* GetTalentName(uint16_t id) {
        static const std::unordered_map<uint16_t, const char*> traitsMap = {
            { 1, u8"대덕" }, { 2, u8"의협" }, { 3, u8"만인적" }, { 4, u8"일신시담" }, { 5, u8"금마초" },
            { 6, u8"노당익장" }, { 7, u8"복룡" }, { 8, u8"봉추" }, { 9, u8"기린아" }, { 10, u8"초세지걸" },
            { 11, u8"왕좌" }, { 12, u8"불요불굴" }, { 13, u8"낭고" }, { 14, u8"병귀신속" }, { 15, u8"료래료래" },
            { 16, u8"금강불괴" }, { 17, u8"위서심공" }, { 18, u8"산도강습" }, { 19, u8"강동맹호" }, { 20, u8"소패왕" },
            { 21, u8"용재" }, { 22, u8"화신" }, { 23, u8"냉염" }, { 24, u8"괄목" }, { 25, u8"방울감녕" },
            { 26, u8"원모심려" }, { 27, u8"천하무쌍" }, { 28, u8"수화폐월" }, { 29, u8"명가위광" }, { 30, u8"악역무도" },
            { 31, u8"구심" }, { 32, u8"황천" }, { 33, u8"전장의꽃" }, { 34, u8"호위" }, { 35, u8"기습병" },
            { 36, u8"간파" }, { 37, u8"군규" }, { 38, u8"냉정" }, { 39, u8"기략" }, { 40, u8"진법" },
            { 41, u8"궤계" }, { 42, u8"맹공" }, { 43, u8"견수" }, { 44, u8"불굴" }, { 45, u8"산전" },
            { 46, u8"삼전" }, { 47, u8"강창" }, { 48, u8"조기통제" }, { 49, u8"북방마술" }, { 50, u8"질주궁" },
            { 51, u8"수신" }, { 52, u8"상조교" }, { 53, u8"재녀" }, { 54, u8"교화" }, { 55, u8"호랑지심" },
            { 56, u8"부가" }, { 57, u8"능리" }, { 58, u8"근면" }, { 59, u8"시재" }, { 60, u8"유심" },
            { 61, u8"경성" }, { 62, u8"직정" }, { 63, u8"자만" }, { 64, u8"반감" }, { 65, u8"소심" },
            { 66, u8"조급" }, { 67, u8"성걸" }, { 68, u8"이기적" }, { 69, u8"병약" }, { 70, u8"거만" },
            { 201, u8"괴물" }, { 202, u8"잔병첩보" }
        };
        auto it = traitsMap.find(id);
        return (it != traitsMap.end()) ? it->second : "Unknown";
    }

    const char* GetOffsetLabel(int offset) {
        switch (offset) {
            case 0x02C: return u8"소재지/좌표 (short)";
            case 0x02E: return u8"얼굴 번호 (short)";
            case 0x020: return u8"소속 군단 주소 (uintptr)";
            case 0x032: return u8"등장년도 (short)";
            case 0x034: return u8"생년 (short)";
            case 0x036: return u8"몰년 (short)";
            case 0x05C: return u8"소속 도시 (byte)";
            case 0x060: return u8"소재 도시 (byte)";
            case 0x88:  return u8"기재 포인터 배열 (uintptr*)";
            case 0xAA: return u8"통솔 (byte)";
            case 0xAB: return u8"무력 (byte)";
            case 0xAC: return u8"지력 (byte)";
            case 0xAD: return u8"정치 (byte)";
            case 0xAE: return u8"매력 (byte)";
            case 0xA5: return u8"모델 번호 (byte)";
            case 0xA6: return u8"모델 색상 (byte)";
            case 0xE8: return u8"봉록 (int?)";
            case 0xEC: return u8"충성 (byte)";
            case 0xED: return u8"전략포인트 (byte)";
            case 0xEE: return u8"행동력 (byte)";
            case 0x100: return u8"공적 (short)";
            case 0x104: return u8"문명 (short)";
            case 0x106: return u8"무명 (short)";
            case 0x108: return u8"악명 (short)";
            case 0x374: return u8"사망 플래그 (int)";
            default: return nullptr;
        }
    }

    std::string GetFormattedEffectDescription(const TalentEffect& effect) {
        LoadEffectDefinitions();
        auto it = g_effectDefs.find(effect.effectId);
        if (it == g_effectDefs.end()) return "";
        const EffectDef& def = it->second;
        std::string result = def.templateStr;
        size_t valPos = result.find("{value}");
        if (valPos != std::string::npos) {
            auto vIt = def.valueMap.find(effect.val1);
            result.replace(valPos, 7, (vIt != def.valueMap.end()) ? vIt->second : std::to_string(effect.val1));
        }
        size_t paramPos = result.find("{param}");
        if (paramPos != std::string::npos) {
            auto pIt = def.paramMap.find(effect.val2);
            result.replace(paramPos, 7, (pIt != def.paramMap.end()) ? pIt->second : std::to_string(effect.val2));
        }
        return result;
    }

}
