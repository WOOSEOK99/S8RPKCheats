#include "pch.h"
#include "MainColumns.h"
#include "MenuSections.h"

namespace DX11Base {
  namespace MainColumns {
    void DrawColumn1(uintptr_t p1, uintptr_t gameBase, float scale) {
      MenuSections::DrawDomesticSection(p1, gameBase, scale);
      MenuSections::DrawScenarioSection(p1, scale);
      MenuSections::DrawOfficerDetailSection(
          p1, ImGui::GetWindowPos(), ImGui::GetWindowSize(), scale);
    }
  } // namespace MainColumns
} // namespace DX11Base
