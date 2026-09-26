#pragma once

#include "ChildLimitDiagnostics.h"
#include "ChildWriteProbeDiagnostics.h"

namespace DX11Base {
namespace ChildDiagnosticsBundle {

static void Tick() {
  ChildLimitDiagnostics::Tick();
  ChildWriteProbeDiagnostics::Tick();
}

} // namespace ChildDiagnosticsBundle
} // namespace DX11Base
