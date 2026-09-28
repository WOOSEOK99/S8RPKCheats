#pragma once

#include "Framework/imgui.h"
#include "Config.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "Cheats/War/IsolatedTerritoryMovementFeature.h"
#include <cstring>

namespace ImGui {
  inline bool g_IsolatedTerritoryYumokHoverPending = false;
  inline bool g_IsolatedTerritoryYumokHovered = false;

  inline bool CouncilCheckboxWithIsolatedTerritory(const char *label, bool *value) {
    const bool changed = Checkbox(label, value);

    if (label && std::strcmp(label, u8"유목기병 습득 조건 해제") == 0) {
      g_IsolatedTerritoryYumokHovered = IsItemHovered();
      g_IsolatedTerritoryYumokHoverPending = true;

      SameLine(160.0f);
      if (Checkbox(u8"단절 영토 무장 이동 제한", &DX11Base::bIsolatedTerritoryMovement)) {
        const bool requested = DX11Base::bIsolatedTerritoryMovement;
        if (!DX11Base::SetIsolatedTerritoryMovementFeature(requested))
          DX11Base::bIsolatedTerritoryMovement = DX11Base::IsIsolatedTerritoryMovementFeatureApplied();
        DX11Base::NotifyFeatureToggle(u8"단절 영토 무장 이동 제한",
                                      DX11Base::bIsolatedTerritoryMovement);
        DX11Base::SaveConfig();
      }
      if (IsItemHovered()) {
        BeginTooltip();
        TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                    u8"서로 연결되지 않은 같은 세력 영토 사이의 무장 이동을 제한합니다.");
        TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                    u8"수송과 배정은 기존 동작을 유지합니다.");
        EndTooltip();
      }
    }

    return changed;
  }

  inline bool CouncilIsItemHoveredWithIsolatedTerritory(ImGuiHoveredFlags flags = 0) {
    if (g_IsolatedTerritoryYumokHoverPending) {
      g_IsolatedTerritoryYumokHoverPending = false;
      return g_IsolatedTerritoryYumokHovered;
    }
    return IsItemHovered(flags);
  }
}

#define Checkbox CouncilCheckboxWithIsolatedTerritory
#define IsItemHovered CouncilIsItemHoveredWithIsolatedTerritory
#include "CouncilCompactSectionBase.h"
#undef IsItemHovered
#undef Checkbox
