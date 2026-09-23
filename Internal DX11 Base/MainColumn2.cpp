#include "pch.h"
#include "MainColumns.h"
#include "MenuSections.h"

namespace DX11Base {
  namespace MainColumns {
    void DrawColumn2(uintptr_t p1, uintptr_t gameBase, float scale) {
      MenuSections::DrawCouncilSection(p1, gameBase, scale);
      MenuSections::DrawOfficerEditSection(p1, scale);
    }
  } // namespace MainColumns
} // namespace DX11Base
