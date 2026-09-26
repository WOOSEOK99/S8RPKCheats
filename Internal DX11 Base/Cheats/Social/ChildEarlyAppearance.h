#pragma once

#include <cstdint>

namespace DX11Base {
  namespace ChildManagerDetail {
    uintptr_t NormalizeOfficerPtr(uintptr_t p);
    bool SafeRead16(uintptr_t addr, uint16_t* out);
    bool SafeReadPtr(uintptr_t addr, uintptr_t* out);
    bool ResolveHeroAndRoster(uintptr_t& rosterBase, uintptr_t& heroMaster, uint16_t& heroId);
  }

  // 자녀 처리 루틴을 감시하여 +0x08 무장 ID 기준으로 자녀 목록을 수집합니다.
  void EnsureChildManagerCapture();

  // 수집된 주소를 자녀 목록에 반영하고, 체크된 자녀의 임관 연도를 갱신합니다.
  void RunChildManagerUpdate();

  // 별도 자녀 관리 창.
  void DrawChildManagerWindow(float scale);
}
