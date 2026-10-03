#include "../../pch.h"

#include "../../Config.h"
#include "../../NotificationManager.h"
#include "TechCityEditorVisibility.h"

namespace DX11Base {
  void DrawTechCityEditorVisibilityUi() {
    if (ImGui::Checkbox(u8"게임 내 도시 편집기에 기술도시 표시",
                        &bTechCityEditorVisible)) {
      const bool requested = bTechCityEditorVisible;
      if (!SetTechCityEditorVisible(requested))
        bTechCityEditorVisible = IsTechCityEditorVisibleApplied();
      AddNotification(bTechCityEditorVisible
                          ? u8"게임 내 도시 편집기 기술도시 표시 ON"
                          : u8"게임 내 도시 편집기 기술도시 표시 OFF");
      SaveConfig();
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(u8"게임 원본 도시 편집기에서 기술도시(feature ID 3)를 숨기는 분기만 제거합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 도시 편집기를 닫은 상태에서 전환한 뒤 다시 여세요.");
      ImGui::TextUnformatted(u8"자체 '도시 관리' 종합 편집기를 사용하는 경우에는 그쪽을 우선 사용하세요.");
      ImGui::TextUnformatted(u8"선택/저장/재로드 동작은 실제 게임에서 추가 검증이 필요합니다.");
      ImGui::EndTooltip();
    }
  }
} // namespace DX11Base
