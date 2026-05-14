#pragma once
#include <unordered_map>
#include <windows.h>
#include <mutex>

namespace DX11Base {

    // OfficerID -> (SkillID -> TargetCount)
    // SkillID는 오프셋을 기준으로 인덱싱하거나 정해진 ID를 사용합니다.
    // 여기서는 오프셋(0x139 등)을 키로 사용하는 것이 UI 연동 시 가장 간편합니다.
    extern std::unordered_map<int, std::unordered_map<uintptr_t, int>> g_customSkillCounts;
    extern std::mutex g_skillCountMutex;

    void SaveSkillCounts();
    void LoadSkillCounts();
    
    // 특정 무장의 특정 전법에 대한 목표 횟수를 가져옵니다. 설정이 없으면 -1 반환.
    int GetTargetSkillCount(int officerID, uintptr_t offset);
    void SetTargetSkillCount(int officerID, uintptr_t offset, int count);
}
