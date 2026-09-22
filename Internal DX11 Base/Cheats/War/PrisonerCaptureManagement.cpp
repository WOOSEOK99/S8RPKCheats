#include "../../pch.h"

#include "../../showlog.h"
#include "DeputyCaptureFix.h"
#include "IsolatedCityCaptureFix.h"
#include "PrisonerCaptureManagement.h"

namespace DX11Base {

  bool IsPrisonerCaptureManagementApplied() {
    return IsDeputyCaptureFixApplied() &&
           IsIsolatedCityCaptureFixApplied();
  }

  bool SetPrisonerCaptureManagement(bool enable) {
    if (enable) {
      const bool deputyWasApplied = IsDeputyCaptureFixApplied();
      const bool isolatedWasApplied = IsIsolatedCityCaptureFixApplied();

      if (!deputyWasApplied && !InstallDeputyCaptureFix()) {
        AddLog(u8"[포로관리] 적용 실패: 부장 포획 보정");
        return false;
      }

      if (!isolatedWasApplied && !InstallIsolatedCityCaptureFix()) {
        AddLog(u8"[포로관리] 적용 실패: 고립도시 포획 보정");

        // 이번 요청에서 새로 켠 부장 보정만 원상복구합니다.
        if (!deputyWasApplied)
          UninstallDeputyCaptureFix();

        return false;
      }

      AddLog(u8"[포로관리] 활성화 완료");
      return true;
    }

    const bool deputyWasApplied = IsDeputyCaptureFixApplied();
    const bool isolatedWasApplied = IsIsolatedCityCaptureFixApplied();

    bool isolatedOk = true;
    bool deputyOk = true;

    // 고립도시 hook부터 제거한 뒤 부장 hook을 제거합니다.
    if (isolatedWasApplied)
      isolatedOk = UninstallIsolatedCityCaptureFix();

    if (deputyWasApplied)
      deputyOk = UninstallDeputyCaptureFix();

    if (!isolatedOk || !deputyOk) {
      AddLog(u8"[포로관리] 해제 실패 - 기존 ON 상태 복구 시도");

      if (deputyWasApplied && !IsDeputyCaptureFixApplied())
        InstallDeputyCaptureFix();
      if (isolatedWasApplied && !IsIsolatedCityCaptureFixApplied())
        InstallIsolatedCityCaptureFix();

      return false;
    }

    AddLog(u8"[포로관리] 비활성화 완료");
    return true;
  }

} // namespace DX11Base
