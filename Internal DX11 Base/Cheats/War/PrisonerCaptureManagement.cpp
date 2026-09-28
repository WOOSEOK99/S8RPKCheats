#include "../../pch.h"

#ifdef max
#undef max
#endif

#include "../../showlog.h"
#include "AIExecutionConditionChange.h"
#include "DeputyCaptureFix.h"
#include "IsolatedCityCaptureFix.h"
#include "IsolatedTerritoryMovement.h"
#include "IsolatedTerritoryMovementDiagnostic.h"
#include "PrisonerCaptureManagement.h"

namespace DX11Base {

  bool IsPrisonerCaptureManagementApplied() {
    return IsDeputyCaptureFixApplied() &&
           IsIsolatedCityCaptureFixApplied() &&
           IsAIExecutionConditionChangeApplied();
  }

  bool SetPrisonerCaptureManagement(bool enable) {
    if (enable) {
      RunIsolatedTerritory