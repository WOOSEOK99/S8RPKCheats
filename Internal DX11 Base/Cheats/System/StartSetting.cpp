#include "../../NotificationManager.h"
#include <string>

namespace DX11Base {
  // SelectOfficercapture.cpp의 UI-thread refresh bridge.
  // worker에서는 캐시/vector를 직접 만지지 않고 atomic 요청만 올립니다.
  void RequestOfficerListRefresh();

  static void StartSettingAddNotificationProxy(const std::string &message) {
    AddNotification(message);

    // 미발견→재야 일괄 보정이 실제로 끝난 시점에만 모든 무장 목록 캐시를 무효화합니다.
    // 시작 알림("변경중")에는 refresh를 요청하지 않아 불필요한 재스캔을 피합니다.
    if (message.find(u8"명의 미발견 무장이 재야로 변경되었습니다.") != std::string::npos) {
      RequestOfficerListRefresh();
    }
  }
}

#define AddNotification StartSettingAddNotificationProxy
#include "StartSetting_impl.inc"
#undef AddNotification
