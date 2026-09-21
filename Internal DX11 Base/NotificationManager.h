#pragma once
#include <string>
#include <vector>
#include "Framework/imgui.h"

namespace DX11Base {
    // --- 알림 데이터 구조 ---
    struct Notification {
        std::string message;
        float xPos;      // 상단 마퀴용 현재 X 위치
        float width;     // 텍스트 측정 너비
        bool active;
        bool isError = false; // 오류 알림: 붉은색 점멸 강조
    };

    // --- 알림 상태 전역 변수 (extern) ---
    extern std::vector<Notification> g_notifications;
    extern std::vector<std::string> g_notificationHistory;
    extern float g_notificationSpeed;
    extern bool bShowNotificationLog;
    extern bool bShowWidgetNotif;

    // --- 기능 함수 ---
    void AddNotification(const std::string& msg);
    void AddErrorNotification(const std::string& msg);
    /// 메인 메뉴 체크박스 등 ON/OFF 알림용
    inline void NotifyFeatureToggle(const char *featureLabel, bool enabled) {
        AddNotification(std::string(featureLabel) + (enabled ? u8": 활성화" : u8": 비활성화"));
    }
    void DrawMarqueeNotifications(float scale);
    void DrawNotificationHistoryWindow(float scale);
}
