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

namespace {

uint8_t __fastcall SymmetricMovementCheck(uintptr_t arg1,
                                          uintptr_t destination,
                                          uintptr_t arg3,
                                          uintptr_t arg4,
                                          uint32_t arg5) {
  using namespace IsolatedTerritoryMovementDetail;

  const auto native =
      reinterpret_cast<MovementCheckFn>(gBase + kNativeMovementCheck);
  const uint8_t result = native(arg1, destination, arg3, arg4, arg5);
  if (!result || arg5 != 0 || !destination)
    return result;

  uintptr_t source = 0;
  if (!ReadValue(arg1 + 0x20, source) || !source)
    return result;

  const bool forward = AreConnected(source, destination);
  const bool reverse = AreConnected(destination, source);

  if (forward != reverse) {
    static volatile LONG s_logCount = 0;
    const LONG index = InterlockedIncrement(&s_logCount);
    if (index <= 8) {
      AddLog(u8"[영토단절DBG] 비대칭 연결: 정방향=%d 역방향=%d src=%p dst=%p",
             forward ? 1 : 0,
             reverse ? 1 : 0,
             reinterpret_cast<void*>(source),
             reinterpret_cast<void*>(destination));
    }
  }

  return (forward && reverse) ? 1 : 0;
}

bool RedirectMovementThunkToSymmetricCheck() {
  using namespace IsolatedTerritoryMovementDetail;

  if (!gHook78Thunk)
    return false;

  const uint8_t prefix[2] = {0x48, 0xB8};
  if (!ReadEq(gHook78Thunk, prefix, sizeof(prefix)))
    return false;

  const uintptr_t target = reinterpret_cast<uintptr_t>(&SymmetricMovementCheck);
  return WriteBytes(gHook78Thunk + 2, &target, sizeof(target));
}

} // namespace

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
      if (!SetIsolatedTerritoryMovement(true)) {
        AddLog(u8"[영토단절TEST] 실제 이동 제한 적용 실패 - 포로 관리만 계속 적용합니다.");
      } else if (!RedirectMovementThunkToSymmetricCheck()) {
        AddLog(u8"[영토단절TEST] 양방향 이동 판정 연결 실패");
      } else {
        AddLog(u8"[영토단절TEST] 양방향 이동 판정 적용 완료");
      }

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
