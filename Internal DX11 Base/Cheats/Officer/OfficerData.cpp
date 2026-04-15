#include "OfficerData.h"
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
            case 0xE8: return u8"금전 (int?)";
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
