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

    struct OfficerRelationshipInfo {
        bool valid = false;
        std::vector<uint16_t> swornBrothers;
        std::vector<uint16_t> spouses;
        std::vector<uint16_t> synergetic;
        std::vector<uint16_t> antipathetic;
        std::vector<uint16_t> enemies;
        std::vector<uint16_t> rivals;
    };

    // Shared data structures
    extern std::unordered_map<int, std::string> g_officerNames;
    extern bool g_namesLoaded;
    extern std::unordered_map<int, EffectDef> g_effectDefs;
    extern bool g_effectsLoaded;

    // Data reading functions
    RosterStats SafeReadRosterStats(uintptr_t targetBase);
    bool GetOfficerTalentDetailed(uintptr_t officerBase, int slot, TalentInfo& outInfo);
    // 현재 PK에서 확인된 관계/숙명 테이블을 읽어 선택 무장의 관계를 정리합니다.
    // 읽기 전용이며, 숙명의 비활성(+0x19 != 0) 및 직접 관계 중복은 제외합니다.
    bool GetOfficerRelationshipInfo(uintptr_t officerBase, OfficerRelationshipInfo& outInfo);
    // 현재 PK에서 실측 확인된 친밀도 삼각 배열을 읽습니다.
    // 2026-09-20 추적: 1650 압축 인덱스 + ScenarioDataCenter+0x24206.
    // 읽기 전용이며 0~100 범위만 유효값으로 반환합니다.
    bool GetOfficerAffinity(uint16_t officerId1, uint16_t officerId2, uint8_t& outAffinity);
    // 여러 무장의 관계를 한 번의 테이블 스캔으로 읽습니다.
    // 자동배치처럼 다수 무장을 동시에 검사할 때 개별 반복 스캔으로 인한 프리징을 줄입니다.
    bool GetOfficerRelationshipInfoBatch(
        const std::vector<uintptr_t>& officerBases,
        std::vector<OfficerRelationshipInfo>& outInfos);

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
