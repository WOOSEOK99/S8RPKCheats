#pragma once

#include <string>

namespace DX11Base {

// san8r_traits_config.json의 기재 정의를 게임의 254개 기재 테이블에 적용합니다.
// Menu::Loops()에서 호출하며 내부에서 자체적으로 주기를 제한합니다.
void TickTraitConfigRuntime();

// trait_texts.ini의 기재 이름 편집은 별도 version.dll 후크를 만들지 않고,
// 이미 설치된 JSON 이름 getter 위에 런타임 오버레이로 적용합니다.
bool ApplyTraitTextNameEditorOverrides(std::string* error = nullptr);
bool RemoveTraitTextNameEditorOverrides(std::string* error = nullptr);
bool IsTraitTextNameEditorOverrideApplied();

} // namespace DX11Base
