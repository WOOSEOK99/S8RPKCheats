#pragma once

#include "ChildLimitDiagnostics.h"
#include "ChildNameDiagnostics.h"

namespace DX11Base {
namespace ChildDiagnosticsBundle {

static void Tick() {
  ChildLimitDiagnostics::Tick();
  // 4004 출산 코드 위치는 이미 확보했으므로 PAGE_GUARD 쓰기 감시는 중단합니다.
  // 생성 자녀 레코드들이 같은 4KB 페이지를 공유해 수동 이름 입력/화면 전환과
  // 충돌할 수 있으므로 이후에는 읽기 전용 이름 진단만 실행합니다.
  ChildNameDiagnostics::Tick();
}

} // namespace ChildDiagnosticsBundle
} // namespace DX11Base
