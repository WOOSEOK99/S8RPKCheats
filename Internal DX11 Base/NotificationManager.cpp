#include "pch.h"
#include "NotificationManager.h"
#include "MenuState.h"
#include "showlog.h"
#include "Cheats/System/SpeedHack.h"
#include "PerformanceDiagnostics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>

namespace DX11Base {
    // --- UI 스레드가 소유하는 표시/히스토리 상태 ---
    std::vector<Notification> g_notifications;
    std::vector<std::string> g_notificationHistory;
    float g_notificationSpeed = 100.0f;
    bool bShowNotificationLog = false;
    bool bShowWidgetNotif = true;

    namespace {
        constexpr size_t kMaxPendingNotifications = 128;
        constexpr size_t kMaxDisplayNotifications = 8;
        constexpr size_t kMaxNotificationHistory = 50;
        constexpr uint64_t kNotificationTtl100ns = 30ull * 1000ull * 1000ull * 10ull;

        struct PendingNotification {
            std::string message;
            uint32_t repeatCount = 1;
        };

        std::mutex s_pendingMutex;
        std::deque<PendingNotification> s_pendingNotifications;
        uint64_t s_droppedPending = 0;
        std::atomic<uint64_t> s_pendingSize{0};
        std::atomic<uint64_t> s_historySize{0};
        std::atomic<bool> s_configSaveErrorPopupRequested{false};

        void PushHistory(const std::string& msg) {
            g_notificationHistory.push_back(msg);
            if (g_notificationHistory.size() > kMaxNotificationHistory)
                g_notificationHistory.erase(g_notificationHistory.begin());
            s_historySize.store(static_cast<uint64_t>(g_notificationHistory.size()),
                                std::memory_order_relaxed);
        }

        void PushDisplay(const std::string& msg, uint64_t now100ns) {
            if (g_notifications.size() >= kMaxDisplayNotifications)
                return;

            Notification n;
            n.message = msg;
            n.xPos = 0.0f;
            n.width = 0.0f;
            n.active = true;
            n.createdAt100ns = now100ns;
            g_notifications.push_back(std::move(n));
        }

        void DrainPendingToUi() {
            if (g_notifications.size() >= kMaxDisplayNotifications)
                return;

            const size_t available = kMaxDisplayNotifications - g_notifications.size();
            std::array<PendingNotification, kMaxDisplayNotifications> drained{};
            size_t drainedCount = 0;
            uint64_t droppedCount = 0;

            {
                std::lock_guard<std::mutex> lock(s_pendingMutex);

                droppedCount = s_droppedPending;
                s_droppedPending = 0;

                size_t messageSlots = available;
                if (droppedCount != 0 && messageSlots != 0)
                    --messageSlots;

                drainedCount = (std::min)(messageSlots, s_pendingNotifications.size());
                for (size_t i = 0; i < drainedCount; ++i) {
                    drained[i] = std::move(s_pendingNotifications.front());
                    s_pendingNotifications.pop_front();
                }

                s_pendingSize.store(static_cast<uint64_t>(s_pendingNotifications.size()),
                                    std::memory_order_relaxed);
            }

            const uint64_t now100ns = PerfRealNow100ns();

            if (droppedCount != 0 && g_notifications.size() < kMaxDisplayNotifications) {
                std::string summary = u8"알림 ";
                summary += std::to_string(droppedCount);
                summary += u8"개가 대기 한도를 초과해 생략되었습니다.";
                PushHistory(summary);
                PushDisplay(summary, now100ns);
            }

            for (size_t i = 0; i < drainedCount; ++i) {
                std::string message = std::move(drained[i].message);
                if (drained[i].repeatCount > 1) {
                    message += " (x";
                    message += std::to_string(drained[i].repeatCount);
                    message += ")";
                }

                PushHistory(message);
                PushDisplay(message, now100ns);
            }
        }
    } // namespace

    // --- 일반 알림 추가 ---
    void AddNotification(const std::string& msg) {
        PerfScope perfScope(PerfMetric::NotificationProduce, static_cast<uint64_t>(msg.size()));

        uint64_t pendingSize = 0;
        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);

            // 동일 알림이 연속 폭주할 때는 pending 한 칸에서 횟수만 합칩니다.
            if (!s_pendingNotifications.empty() &&
                s_pendingNotifications.back().message == msg &&
                s_pendingNotifications.back().repeatCount < (std::numeric_limits<uint32_t>::max)()) {
                ++s_pendingNotifications.back().repeatCount;
            } else {
                // 생산자 큐는 128개로 제한합니다. 최근 알림을 살리기 위해 가장 오래된
                // pending 항목을 버리고, UI에서 한 번에 생략 개수를 알려줍니다.
                if (s_pendingNotifications.size() >= kMaxPendingNotifications) {
                    s_pendingNotifications.pop_front();
                    ++s_droppedPending;
                }

                PendingNotification pending;
                pending.message = msg;
                s_pendingNotifications.push_back(std::move(pending));
            }

            pendingSize = static_cast<uint64_t>(s_pendingNotifications.size());
            s_pendingSize.store(pendingSize, std::memory_order_relaxed);
        }

        PerfRecordNotificationSizes(
            static_cast<size_t>(pendingSize),
            static_cast<size_t>(s_historySize.load(std::memory_order_relaxed)));
    }

    void RequestConfigSaveErrorPopup() {
        s_configSaveErrorPopupRequested.store(true, std::memory_order_release);
    }

    void DrawConfigSaveErrorPopup(float scale) {
        if (s_configSaveErrorPopupRequested.exchange(false, std::memory_order_acq_rel))
            ImGui::OpenPopup(u8"설정 저장 실패###ConfigSaveError");

        ImGui::SetNextWindowSizeConstraints(
            ImVec2(560.0f * scale, 0.0f),
            ImVec2(560.0f * scale, 1000.0f * scale));

        if (ImGui::BeginPopupModal(
                u8"설정 저장 실패###ConfigSaveError",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetWindowFontScale(1.20f);

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

            const float buttonWidth = 150.0f * scale;
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
        PerfScope perfScope(PerfMetric::NotificationRender);

        // producer mutex는 여기서 짧게만 잡고, 실제 렌더링/문자열 측정 중에는 잡지 않습니다.
        DrainPendingToUi();
        PerfRecordNotificationSizes(
            static_cast<size_t>(s_pendingSize.load(std::memory_order_relaxed)),
            g_notificationHistory.size());

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

            // 이동은 원본 QPC 기반 실제 DeltaTime, 수명은 SpeedHack과 독립된 실제 시간으로 처리합니다.
            float deltaTime = SpeedHack_GetRealDeltaTime();
            const uint64_t now100ns = PerfRealNow100ns();
            float speed = g_notificationSpeed * scale;
            float minNextX = marqueeWidth;
            float gap = 80.0f * scale;
            float fontSize = ImGui::GetFontSize() * 1.5f;

            for (auto it = g_notifications.begin(); it != g_notifications.end();) {
                const bool ttlExpired =
                    it->createdAt100ns != 0 && now100ns >= it->createdAt100ns &&
                    (now100ns - it->createdAt100ns) >= kNotificationTtl100ns;

                if (ttlExpired) {
                    it = g_notifications.erase(it);
                    continue;
                }

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

        // 표시 슬롯이 비었으면 다음 프레임을 기다리지 않고 pending을 한 번 더 채울 수 있습니다.
        DrainPendingToUi();
        PerfRecordNotificationSizes(
            static_cast<size_t>(s_pendingSize.load(std::memory_order_relaxed)),
            g_notificationHistory.size());
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
                s_historySize.store(0, std::memory_order_relaxed);
                PerfRecordNotificationSizes(
                    static_cast<size_t>(s_pendingSize.load(std::memory_order_relaxed)), 0);
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
