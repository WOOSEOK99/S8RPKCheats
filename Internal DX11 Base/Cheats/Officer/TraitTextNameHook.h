#pragma once

#include <string>

namespace DX11Base {

// CT 87200 Step 2:
// SAN8RPK.exe+170E2D0 / +170E2D6 이름 getter 경로만 후킹합니다.
// 설명 문자열 후킹과 편집 UI는 이후 단계에서 별도로 추가합니다.
bool ApplyTraitTextNameHook(std::string* error = nullptr);
bool RemoveTraitTextNameHook(std::string* error = nullptr);
bool IsTraitTextNameHookApplied();

} // namespace DX11Base
