#pragma once

#include <cstddef>

namespace DX11Base {

// 모든 유효 무장의 실제 전법/특기/능력치를 읽어
// 조건에 맞는 특수 능력(0x1000~0x1009)을 추가로 부여합니다.
// 기존 수동 특수 능력 설정은 삭제하지 않습니다.
// 호출 시 작업만 시작하며 실제 처리는 TickSpecialAbilityAutoAssign()에서 분할 실행합니다.
bool AutoAssignSpecialAbilities();
void TickSpecialAbilityAutoAssign();
// 능력치 한계돌파와 동일한 평정 진입 타이밍에서 12월 연말 자동 판정을 시작합니다.
bool AutoAssignSpecialAbilitiesFromCouncil();
void CancelSpecialAbilityAutoAssign();

bool IsSpecialAbilityAutoAssignRunning();
float GetSpecialAbilityAutoAssignProgress();
const char *GetSpecialAbilityAutoAssignStatus();
std::size_t GetSpecialAbilityAutoAssignResultCount();
const char *GetSpecialAbilityAutoAssignResultLine(std::size_t index);

} // namespace DX11Base
