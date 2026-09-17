#pragma once

#include <string>

namespace DX11Base {

// CT 87200 Step 4:
// '괴물', '잔병첩보'의 특수 설명 경로 SAN8RPK.exe+146E5만 후킹합니다.
// 일반 설명(+170ACE0), 이름 후크, 편집 UI는 별도 단계입니다.
bool ApplyTraitTextSpecialDescHook(std::string* error = nullptr);
bool RemoveTraitTextSpecialDescHook(std::string* error = nullptr);
bool IsTraitTextSpecialDescHookApplied();

} // namespace DX11Base
