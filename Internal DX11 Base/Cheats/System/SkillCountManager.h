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

    // 즉시 저장이 필요한 명시적 호출용. 실제 파일 I/O는 맵 잠금 밖에서 수행합니다.
    void SaveSkillCounts();
    void LoadSkillCounts();

    // 연속 편집을 하나의 JSON 저장으로 합치기 위한 debounce flush.
    // force=true이면 대기 시간을 무시하고 현재 dirty 상태를 즉시 저장합니다.
    void FlushPendingSkillCounts(bool force = false);
    
    // 특정 무장의 특정 전법에 대한 목표 횟수를 가져옵니다. 설정이 없으면 -1 반환.
    int GetTargetSkillCount(int officerID, uintptr_t offset);
    void SetTargetSkillCount(int officerID, uintptr_t offset, int count);
}
