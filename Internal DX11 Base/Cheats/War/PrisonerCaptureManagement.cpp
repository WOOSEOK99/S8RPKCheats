#include "../../pch.h"

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
      RunIsolatedTerritoryMovementDiagnostic();

      // CT 92011 실게임 검증용 임시 연결입니다.
      // 포로 관리 기능과 최종적으로 묶지 않고, 동작 확인 후 독립 옵션으로 분리합니다.
      if (!SetIsolatedTerritoryMovement(true))
        AddLog(u8"[영토단절TEST] 실제 이동 제한 적용 실패 - 포로 관리만 계속 적용합니다.");

      const bool deputyWasApplied = IsDeputyCaptureFixApplied();
      const bool isolatedWasApplied = IsIsolatedCityCaptureFixApplied();
      const bool executionWasApplied = IsAIExecutionConditionChangeApplied();

      if (!deputyWasApplied && !InstallDeputyCaptureFix()) {
        AddLog(u8"[포로관리] 적용 실패: 부장 포획 보정");
        return false;
      }

      if (!isolatedWasApplied && !InstallIsolatedCityCaptureFix()) {
        AddLog(u8"[포로관리] 적용 실패: 고립도시 포획 보정");

        if (!deputyWasApplied)
          UninstallDeputyCaptureFix();

        return false;
      }

      if (!executionWasApplied && !SetAIExecutionConditionChange(true)) {
        AddLog(u8"[포로관리] 적용 실패: AI 처형조건 변경");

        if (!isolatedWasApplied)
          UninstallIsolatedCityCaptureFix();
        if (!deputyWasApplied)
          UninstallDeputyCaptureFix();

        return false;
      }

      AddLog(u8"[포로관리] 활성화 완료 (부장/고립도시/AI처형)");
      return true;
    }

    // 실게임 검증용 임시 연결 해제.
    if (IsIsolatedTerritoryMovementApplied() &&
        !SetIsolatedTerritoryMovement(false)) {
      AddLog(u8"[영토단절TEST] 이동 제한 해제 실패 - 게임 재시작을 권장합니다.");
    }

    const bool deputyWasApplied = IsDeputyCaptureFixApplied();
    const bool isolatedWasApplied = IsIsolatedCityCaptureFixApplied();
    const bool executionWasApplied = IsAIExecutionConditionChangeApplied();

    bool executionOk = true;
    bool isolatedOk = true;
    bool deputyOk = true;

    // AI 처형 판정 -> 고립도시 -> 부장 순서로 해제합니다.
    if (executionWasApplied)
      executionOk = SetAIExecutionConditionChange(false);

    if (executionOk && isolatedWasApplied)
      isolatedOk = UninstallIsolatedCityCaptureFix();

    if (executionOk && isolatedOk && deputyWasApplied)
      deputyOk = UninstallDeputyCaptureFix();

    if (!executionOk || !isolatedOk || !deputyOk) {
      AddLog(u8"[포로관리] 해제 실패 - 기존 ON 상태 복구 시도");

      if (deputyWasApplied && !IsDeputyCaptureFixApplied())
        InstallDeputyCaptureFix();
      if (isolatedWasApplied && !IsIsolatedCityCaptureFixApplied())
        InstallIsolatedCityCaptureFix();
      if (executionWasApplied && !IsAIExecutionConditionChangeApplied())
        SetAIExecutionConditionChange(true);

      return false;
    }

    AddLog(u8"[포로관리] 비활성화 완료");
    return true;
  }

} // namespace DX11Base
