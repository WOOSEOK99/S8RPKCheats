#pragma once

namespace DX11Base {

// san8r_traits_config.json의 기재 정의를 게임의 254개 기재 테이블에 적용합니다.
// Menu::Loops()에서 호출하며 내부에서 자체적으로 주기를 제한합니다.
void TickTraitConfigRuntime();

} // namespace DX11Base
