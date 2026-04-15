#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace DX11Base {

    struct RosterStats {
        bool valid;
        unsigned short id_08;
        unsigned short id_2e;
        unsigned int gold;
        unsigned short merit;
        unsigned short repI;
        unsigned char sp;
    };

    struct TalentEffect {
        uint16_t effectId;
        uint16_t val1;
        uint16_t val2;
    };

    struct TalentInfo {
        uint16_t id;
        TalentEffect effects[6];
    };

    struct EffectDef {
        int type;
        std::string templateStr;
        std::unordered_map<int, std::string> valueMap;
        std::unordered_map<int, std::string> paramMap;
    };

    // Shared data structures
    extern std::unordered_map<int, std::string> g_officerNames;
    extern bool g_namesLoaded;
    extern std::unordered_map<int, EffectDef> g_effectDefs;
    extern bool g_effectsLoaded;

    // Data reading functions
    RosterStats SafeReadRosterStats(uintptr_t targetBase);
    bool GetOfficerTalentDetailed(uintptr_t officerBase, int slot, TalentInfo& outInfo);

    // Data loading functions
    void LoadOfficerNames();
    void LoadEffectDefinitions();
    // S8RPK_cheat_char.json 에 id 항목 추가/이름 수정(UTF-8). 빈 문자열이면 맵에서 제거·JSON에는 name "" 로 기록.
    bool SaveOfficerNameToJson(int id, const std::string& nameUtf8);

    // String mapping and normalization functions
    std::string AnsiToUtf8(const std::string& str);
    std::string NormalizeUtf8(const std::string& str);
    const char* GetTalentName(uint16_t id);
    const char* GetOffsetLabel(int offset);
    std::string GetFormattedEffectDescription(const TalentEffect& effect);

}
