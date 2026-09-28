#pragma once

#include "Framework/imgui.h"

namespace DX11Base {
  namespace MenuSections {

    // ── 섹션 테두리 헬퍼 ──────────────────────────────────────────
    // 사용법: BeginSection() → 위젯들 → EndSection(padding)
    inline void BeginSection() {
      ImGui::Spacing();
      ImGui::BeginGroup();
      // 컬럼 너비 끝까지 채워서 테두리 오른쪽을 컬럼 경계에 맞춤
      ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));
    }

    inline void EndSection(float pad = 6.0f) {
      ImGui::EndGroup();
      ImVec2 min = ImGui::GetItemRectMin();
      ImVec2 max = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                          IM_COL32(255, 165, 0, 140), // 주황 계열 반투명
                                          8.0f,                       // 둥근 반경
                                          0,                          // flags
                                          1.2f                        // 두께
      );
      ImGui::Spacing();
    }
  } // namespace MenuSections
} // namespace DX11Base
