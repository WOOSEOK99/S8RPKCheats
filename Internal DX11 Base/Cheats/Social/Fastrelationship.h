#pragma once

// FastRelationship 구현을 복구할 때 아래 임시 UI 필터와 매크로를 제거하면
// 기존 MenuSections.cpp의 체크박스가 다시 표시된다.
#include "../../Framework/imgui.h"
#include <cstring>

namespace ImGui {
  inline bool S8RCheckboxFiltered(const char* label, bool* value) {
    if (label && std::strcmp(label, u8"경애 시 무조건 공명") == 0)
      return false;
    return Checkbox(label, value);
  }
}

// MenuSections.cpp는 이 헤더를 imgui.h보다 먼저 포함하므로,
// 이후 Checkbox 호출만 필터링한다. 다른 체크박스 동작은 그대로 전달한다.
#define Checkbox S8RCheckboxFiltered

namespace DX11Base {
  void SetFastRelationship(bool enable);

} // namespace DX11Base