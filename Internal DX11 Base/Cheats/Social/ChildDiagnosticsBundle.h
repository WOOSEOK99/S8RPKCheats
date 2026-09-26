#pragma once

#include "ChildLimitDiagnostics.h"
#include "ChildWriteProbeDiagnostics.h"
#include "ChildNameDiagnostics.h"

namespace DX11Base {
namespace ChildDiagnosticsBundle {

static void Tick() {
  ChildLimitDiagnostics::Tick();
  ChildWriteProbeDiagnostics::Tick();
  ChildNameDiagnostics::Tick();
}

} // namespace ChildDiagnosticsBundle
} // namespace DX11Base
