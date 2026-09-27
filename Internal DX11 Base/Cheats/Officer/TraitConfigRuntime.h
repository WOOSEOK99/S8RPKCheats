#pragma once

namespace DX11Base {

// san8r_traits_config.json의 기재 정의를 게임의 254개 기재 테이블에 적용합니다.
// Menu::Loops()에서 호출하며 내부에서 자체적으로 주기를 제한합니다.
void TickTraitConfigRuntime();

// T05 진단: 커스텀 기재 호환 확장만 우회하고 게임 원본 기재 판정은 그대로 사용합니다.
// 이미 부여된 기재 데이터는 변경하지 않습니다.
void SetTraitCompatibilityBypass(bool bypass);
bool IsTraitCompatibilityBypass();

} // namespace DX11Base
