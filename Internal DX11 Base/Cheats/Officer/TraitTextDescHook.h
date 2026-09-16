#pragma once

#include <string>

namespace DX11Base {

// CT 87200 Step 3:
// SAN8RPK.exe+170ACE0 일반 설명 경로만 후킹합니다.
// 특수 설명(괴물/잔병첩보)의 +146E5 경로와 편집 UI는 이후 단계에서 별도로 추가합니다.
bool ApplyTraitTextDescHook(std::string* error = nullptr);
bool RemoveTraitTextDescHook(std::string* error = nullptr);
bool IsTraitTextDescHookApplied();

} // namespace DX11Base
