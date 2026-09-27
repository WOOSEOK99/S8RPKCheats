#include <atomic>

namespace {
  std::atomic<bool> g_externalOfficerListRefreshRequested{false};
}

// 기존 13만 바이트 구현은 그대로 보존하고, public entry point만 wrapper로 감쌉니다.
// 내부 구현의 static 캐시는 UI 스레드에서만 접근해야 하므로 외부 worker는
// 아래 atomic 요청만 올리고 실제 refresh 플래그 전환은 DrawOfficerListWindow에서 수행합니다.
#define DrawOfficerListWindow DrawOfficerListWindowImpl
#include "SelectOfficercapture_impl.inc"
#undef DrawOfficerListWindow

namespace DX11Base {

  void RequestOfficerListRefresh() {
    g_externalOfficerListRefreshRequested.store(true, std::memory_order_release);
  }

  void DrawOfficerListWindow(uintptr_t p1, float scale) {
    if (g_externalOfficerListRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
      s_requestOfficerListRefresh = true;
    }

    DrawOfficerListWindowImpl(p1, scale);
  }

} // namespace DX11Base
