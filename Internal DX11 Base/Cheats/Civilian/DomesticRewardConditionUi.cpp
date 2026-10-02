#include "../../pch.h"

#include "../../NotificationManager.h"
#include "DomesticRewardCondition.h"

namespace DX11Base {
  void DrawDomesticRewardConditionUi(float scale) {
    const char *modeNames[] = {
        u8"OFF",
        u8"실적 200% 이상 + 3위 이내",
        u8"실적 200% 이상 + 등수 무관",
    };

    int modeIndex = static_cast<int>(GetDomesticRewardMode());
    ImGui::SetNextItemWidth(220.0f * scale);
    if (ImGui::Combo(u8"내정 포상 조건", &modeIndex,
                     modeNames, IM_ARRAYSIZE(modeNames))) {
      const DomesticRewardMode requested =
          static_cast<DomesticRewardMode>(modeIndex);
      if (SetDomesticRewardMode(requested)) {
        AddNotification(requested == DomesticRewardMode::Off
                            ? u8"내정 포상 조건 완화 OFF"
                            : u8"내정 포상 조건 완화 적용 완료");
      } else {
        AddNotification(u8"내정 포상 조건 완화 변경 실패");
      }
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(u8"실적 200% 이상인 대상의 포상 수령 조건을 완화합니다.");
      ImGui::TextUnformatted(u8"Top3 모드는 3위 이내만, 등수 무관 모드는 순위 제한 없이 적용합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ CT는 계절 실적 결과 창이 열린 동안 사용하도록 안내합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 상시 활성 안전성은 아직 런타임 검증되지 않았으므로 사용 후 OFF를 권장합니다.");
      ImGui::EndTooltip();
    }
  }
} // namespace DX11Base
