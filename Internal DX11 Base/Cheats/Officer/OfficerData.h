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
    bool SetOfficerAffinity(
        uint16_t officerId1, uint16_t officerId2,
        uint8_t value, uint8_t* outPreviousValue = nullptr);
    bool IncreaseOfficerAffinity(
        uint16_t officerId1, uint16_t officerId2,
        uint8_t amount,
        uint8_t* outPreviousValue = nullptr,
        uint8_t* outNewValue = nullptr);

    // 분기 평정월(1/4/7/10월) 시작 시 주인공을 제외한 AI 무장끼리 친밀도를 자동 성장시킵니다.
    // 같은 세력·같은 도시만 대상으로 하며, 상성/흥미/중시 일치도를 반영합니다.
    // 관계(부부/의형제/상생)는 직접 생성하지 않으며 설정은 JSON에 저장됩니다.
    extern bool g_autoAffinityGrowthEnabled;
    void ResetAutoAffinityGrowthState();
    void TickAutoAffinityGrowth(uintptr_t protagonistBase);
    // 친밀도 실제 저장 명령을 읽기 전용으로 추적하기 위한 디버그 검증 API.
    bool GetOfficerAffinityAddress(
        uint16_t officerId1, uint16_t officerId2,
        uintptr_t& outAddress, uint8_t* outCurrentValue = nullptr);
    // 디버그 검증용: 친밀도를 +1 썼다가 즉시 원복하고 두 단계 모두 재읽기 검증합니다.
    // 성공해도 최종 게임 값은 호출 전 값으로 복원됩니다.
    bool TestOfficerAffinityWriteRoundTrip(
        uint16_t officerId1, uint16_t officerId2,
        uintptr_t& outAddress,
        uint8_t& outOriginal,
        uint8_t& outTestValue,
        uint8_t& outRestored);
    bool ArmOfficerAffinityWriteProbe(
        uint16_t officerId1, uint16_t officerId2);
    bool ConsumeOfficerAffinityWriteProbe(
        uintptr_t& outExpectedAddress,
        uintptr_t& outCapturedAddress,
        uint8_t& outWrittenValue);
    void DisarmOfficerAffinityWriteProbe();
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
