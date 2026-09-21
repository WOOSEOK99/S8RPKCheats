#include "pch.h"
#include "NotificationManager.h"
#include "MenuState.h"
#include "showlog.h"
#include "Cheats/System/SpeedHack.h"
#include <algorithm>

namespace DX11Base {
    // --- 전역 변수 초기화 ---
    std::vector<Notification> g_notifications;
    std::vector<std::string> g_notificationHistory;
    float g_notificationSpeed = 100.0f;
    bool bShowNotificationLog = false;
    bool bShowWidgetNotif = true;

    static bool s_configSaveErrorPopupRequested = false;

    // --- 일반 알림 추가 ---
    void AddNotification(const std::string& msg) {
        Notification n;
        n.message = msg;
        n.xPos = 0.0f;  // 나중에 Draw 루프에서 초기화됨 (화면 너비 알 수 있는 시점)
        n.width = 0.0f;
        n.active = true;
        g_notifications.push_back(n);

        // 히스토리에 기록 보관 (최대 50개)
        g_notificationHistory.push_back(msg);
        if (g_notificationHistory.size() > 50) {
            g_notificationHistory.erase(g_notificationHistory.begin());
        }
    }

    void RequestConfigSaveErrorPopup() {
        s_configSaveErrorPopupRequested = true;
    }

    void DrawConfigSaveErrorPopup(float scale) {
        if (s_configSaveErrorPopupRequested) {
            ImGui::OpenPopup(u8"설정 저장 실패###ConfigSaveError");
            s_configSaveErrorPopupRequested = false;
        }

        if (ImGui::BeginPopupModal(
                u8"설정 저장 실패###ConfigSaveError",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                u8"치트 설정 파일을 저장할 수 없습니다.");
            ImGui::Spacing();
            ImGui::TextUnformatted(u8"S8RPK_cheat_config.json");
            ImGui::TextWrapped(
                u8"파일의 읽기 전용 속성 또는 치트 폴더의 쓰기 권한을 확인해 주세요.");
            ImGui::TextWrapped(
                u8"설정이 저장되지 않으면 게임을 다시 실행했을 때 체크 상태가 유지되지 않습니다.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float buttonWidth = 120.0f * scale;
            const float available = ImGui::GetContentRegionAvail().x;
            if (available > buttonWidth)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - buttonWidth) * 0.5f);

            if (ImGui::Button(u8"확인", ImVec2(buttonWidth, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    // --- 상단 흐르는 알림(Marquee) 렌더링 ---
    void DrawMarqueeNotifications(float scale) {
        if (g_notifications.empty())
            return;

        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float marqueeWidth = screenWidth / 3.0f;
        float xStart = (screenWidth - marqueeWidth) * 0.5f;

        float baseHeight = 32.0f * scale;
        float marqueeHeight = baseHeight * 1.5f;

        ImGui::SetNextWindowPos(ImVec2(xStart, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(marqueeWidth, marqueeHeight), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("##MarqueeOverlay", nullptr, flags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 clipMin = winPos;
            ImVec2 clipMax = ImVec2(winPos.x + marqueeWidth, winPos.y + marqueeHeight);

            drawList->AddRectFilled(clipMin, clipMax, IM_COL32(0, 0, 0, 160));
            drawList->AddLine(ImVec2(clipMin.x, clipMax.y - 1.0f), ImVec2(clipMax.x, clipMax.y - 1.0f), IM_COL32(255, 255, 50, 150), 2.0f);

            // ImGui의 DeltaTime은 SpeedHack이 후킹한 QPC의 영향을 받을 수 있으므로
            // marquee 애니메이션만 원본 QPC 기반 실제 시간으로 분리합니다.
            float deltaTime = SpeedHack_GetRealDeltaTime();
            float speed = g_notificationSpeed * scale;
            float minNextX = marqueeWidth;
            float gap = 80.0f * scale;
            float fontSize = ImGui::GetFontSize() * 1.5f;

            for (auto it = g_notifications.begin(); it != g_notifications.end();) {
                if (it->width == 0.0f) {
                    it->width = ImGui::CalcTextSize(it->message.c_str()).x * 1.5f;
                    it->xPos = (std::max)(marqueeWidth, minNextX);
                }

                it->xPos -= speed * deltaTime;
                minNextX = it->xPos + it->width + gap;

                if (it->xPos + it->width > 0 && it->xPos < marqueeWidth) {
                    ImVec2 textPos = ImVec2(winPos.x + it->xPos, winPos.y + (marqueeHeight - fontSize) * 0.5f);
                    drawList->AddText(NULL, fontSize, ImVec2(textPos.x + 1.5f, textPos.y + 1.5f), IM_COL32(0, 0, 0, 255), it->message.c_str());
                    drawList->AddText(NULL, fontSize, textPos, IM_COL32(255, 255, 60, 255), it->message.c_str());
                }

                if (it->xPos + it->width < -100.0f) {
                    it = g_notifications.erase(it);
                } else {
                    ++it;
                }
            }
        }
        ImGui::End();
    }

    // --- 알림 기록(History) 창 렌더링 ---
    void DrawNotificationHistoryWindow(float scale) {
        if (!bShowNotificationLog) return;

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x - 20.f, disp.y * 0.12f + 5.0f * scale), ImGuiCond_Always, ImVec2(1.f, 0.f));
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.8f, 0.8f, 1.0f)); 
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f * scale, 15.0f * scale));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | 
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

        float winWidth = 320.0f * scale;
        float winMaxHeight = 400.0f * scale;
        ImGui::SetNextWindowSize(ImVec2(winWidth, 0), ImGuiCond_Always);

        if (ImGui::Begin("##NotificationHistoryWindow", nullptr, flags)) {
            ImGui::SetWindowFontScale(1.1f);
            
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.f), u8"[ 최근 알림 기록 ]");
            ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f * scale);
            if (ImGui::Button(u8"기록 삭제", ImVec2(70.0f * scale, 0))) {
                g_notificationHistory.clear();
            }
            
            ImGui::Separator();
            ImGui::Spacing();

            float childHeight = (std::min)(winMaxHeight, (float)g_notificationHistory.size() * 25.0f * scale + 30.0f * scale);
            if (g_notificationHistory.empty()) childHeight = 40.0f * scale;

            if (ImGui::BeginChild("##HistoryScroll", ImVec2(0, childHeight), false, ImGuiWindowFlags_NoBackground)) {
                if (g_notificationHistory.empty()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"기록된 내용이 없습니다.");
                } else {
                    for (int i = (int)g_notificationHistory.size() - 1; i >= 0; --i) {
                        ImGui::TextWrapped(u8"- %s", g_notificationHistory[i].c_str());
                    }
                }
            }
            ImGui::EndChild();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), u8"우클릭 시 창 닫기");

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(1)) {
                bShowNotificationLog = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
}
