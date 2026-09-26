#include "pch.h"
#include "MainColumns.h"
#include "MenuSections.h"
#include "Cheats/Social/ChildDiagnosticsBundle.h"

namespace DX11Base {
  namespace MainColumns {
    void DrawColumn3(uintptr_t p1, uintptr_t gameBase, float scale) {
      ChildDiagnosticsBundle::Tick();
      MenuSections::DrawSocialSection(p1, gameBase, scale);
      MenuSections::DrawWarSection(p1, gameBase, scale);
    }
  } // namespace MainColumns
} // namespace DX11Base
