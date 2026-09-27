#include "pch.h"
#include "MainColumns.h"
#include "MenuSections.h"
#include "CouncilCompactSection.h"
#include "Cheats/War/CouncilContinueAfterMove.h"

namespace DX11Base {
  namespace MainColumns {
    void DrawColumn2(uintptr_t p1, uintptr_t gameBase, float scale) {
      DrawCompactCouncilSection(p1, gameBase, scale);
      DrawCouncilContinueAfterMoveSection(scale);
      MenuSections::DrawOfficerEditSection(p1, scale);
    }
  } // namespace MainColumns
} // namespace DX11Base
