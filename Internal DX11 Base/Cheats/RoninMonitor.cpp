// =============================================================================
// RoninMonitor.cpp  –  재야 장수 자동 감시 모듈 (독립형)
// 2026-04-07
//
// [작동 방식]
//   1. RoninMonitor_Tick(p1)을 Menu::Loops() 백그라운드 스레드에서 매 루프 호출.
//   2. IsConfigReady() (= 3초 지연 통과)가 참이 되면 단 한 번, p1 기반으로
//      무장 배열 시작점을 자동 계산합니다.
//        arrayBase = p1 - ((heroID - 1) * 0x3D0)
//   3. 이후 2초마다 5102명 전수 조사하여 미발견→재야 전환을 감지합니다.
//   4. RoninMonitor_Draw()를 Engine.cpp 렌더링 루프에서 호출해 알림창을 그립니다.
// =============================================================================

#include "RoninMonitor.h"
#include "BattleMonitor.h"   // IsInBattle()
#include "Cheats.h"          // GetGameBase(), IsValidPtr(), bMonitorRonin
#include "Config.h"          // IsConfigReady()
#include "OfficerData.h"     // g_officerNames
#include "CityData.h"        // g_CityList, g_CityCount
#include "MenuState.h"       // bMonitorRonin
#include "showlog.h"         // AddLog()
#include "Framework/imgui.h"
#include "pch.h"

#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace DX11Base {

namespace {
    struct RoninNotification {
        std::string name;
        std::string cityName;
        float       timeRemaining;
    };

    static uintptr_t                      s_arrayBase    = 0;
    static bool                           s_baseResolved = false;
    static bool                           s_initialized  = false;

    // unordered_map 대신 고정 배열 – O(1) 접근, 할당 오버헤드 없음
    static uint8_t                        s_prevStatuses[5103] = {};

    // Tick(배경)과 Draw(UI)가 공유 – 짧은 잠금으로만 보호
    static std::mutex                     s_notifMtx;
    static std::vector<RoninNotification> s_notifications;
}

// 외부(수동 조작) 동기화
void RoninMonitor_UpdatePrevStatus(int id, uint8_t st) {
    if (id >= 1 && id <= 5102) s_prevStatuses[id] = st;
}

// ---------------------------------------------------------------------------
static bool TryResolveBase(uintptr_t p1)
{
    if (s_baseResolved) return true;
    if (!p1 || p1 < 0x10000 || !IsValidPtr(p1, 0x10)) return false;

    unsigned short heroID = *(unsigned short*)(p1 + 0x08);
    if (heroID == 0 || heroID > 5102) return false;

    uintptr_t candidate = p1 - (uintptr_t)(heroID - 1) * 0x3D0;
    if (!IsValidPtr(candidate, 0x20)) return false;

    s_arrayBase    = candidate;
    s_baseResolved = true;
    AddLog(u8"[RoninMonitor] 무장 배열 확보: 0x%llX (영웅 ID %d)",
           (unsigned long long)s_arrayBase, (int)heroID);
    return true;
}

// ---------------------------------------------------------------------------
// Tick – 백그라운드 스레드
// ---------------------------------------------------------------------------
void RoninMonitor_Tick(uintptr_t p1)
{
    if (!bMonitorRonin) {
        s_initialized = s_baseResolved = false;
        s_arrayBase = 0;
        memset(s_prevStatuses, 0, sizeof(s_prevStatuses));
        { std::lock_guard<std::mutex> lk(s_notifMtx); s_notifications.clear(); }
        return;
    }

    if (IsInBattle()) return;
    if (!IsConfigReady()) {
        static bool s_warnedConfig = false;
        if (!s_warnedConfig) { s_warnedConfig = true; AddLog(u8"[RoninMonitor] IsConfigReady=false, 대기 중..."); }
        return;
    }
    if (!TryResolveBase(p1)) {
        static bool s_warnedBase = false;
        if (!s_warnedBase) { s_warnedBase = true; AddLog(u8"[RoninMonitor] TryResolveBase 실패 (p1=0x%llX)", (unsigned long long)p1); }
        return;
    }

    static uint64_t s_last = 0;
    uint64_t now = GetTickCount64();
    if (now - s_last < 2000) return;
    s_last = now;

    AddLog(u8"[RoninMonitor] 스캔 시작...");

    // 전수 조사 – 4KB 페이지 단위 유효성 체크 (~50회 vs 5102회)
    std::vector<RoninNotification> found;

    // ⑤ 도시 배열 주소 획득
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t cityBase = 0;
    if (exeBase) {
      uintptr_t p1 = *(uintptr_t*)(exeBase + 0x34C8630);
      if (IsValidPtr(p1, 8)) {
          uintptr_t p2 = *(uintptr_t*)(p1);
          if (IsValidPtr(p2, 8)) {
              cityBase = *(uintptr_t*)(p2);
          }
      }
    }

    uintptr_t lastPage = 0;
    bool      pageOk   = false;

    for (int i = 1; i <= 5102; i++) {
        uintptr_t addr = s_arrayBase + (uintptr_t)(i - 1) * 0x3D0;
        uintptr_t page = addr & ~0xFFFull;
        if (page != lastPage) { 
            lastPage = page; 
            pageOk = IsValidPtr(page, 0x1000); 
        }
        if (!pageOk) continue;

        // 경계 교차 시 다음 페이지만 한 번 더 검사 (O(1) 최적화)
        if ((addr + 0x60) > (page + 0xFFF)) {
            if (!IsValidPtr(page + 0x1000, 0x1000)) continue;
        }

        uint8_t cur  = *(uint8_t*)(addr + 0x10);
        uint8_t prev = s_prevStatuses[i];
        s_prevStatuses[i] = cur;

        // 어떤 상태에서든 재야(0x58)로 바뀌면 감지 (미발견, 사망, 재직 등 포함)
        if (s_initialized && prev != 0x58 && cur == 0x58 && g_officerNames.count(i))
        {
            std::string city = u8"알 수 없는 장소";
            if (cityBase > 0x10000) {
                uintptr_t cityPtr = *(uintptr_t*)(addr + 0x20);
                if (cityPtr >= cityBase) {
                    int idx = (int)((cityPtr - cityBase) / 0x2A0);
                    if (idx >= 0 && idx < g_CityCount) {
                        city = g_CityList[idx].cityname;
                    }
                }
            }

            found.push_back({g_officerNames[i], city, 12.f});
        }
    }
    s_initialized = true;

    // 알림 목록에 추가 + 로그 – 잠금 해제 후 AddLog (데드락 방지)
    if (!found.empty()) {
        {
            std::lock_guard<std::mutex> lk(s_notifMtx);
            for (auto& f : found)
                if ((int)s_notifications.size() < 10)
                    s_notifications.push_back(f);
        }
        for (auto& f : found)
            AddLog(u8"[재야 감지] %s → %s", f.name.c_str(), f.cityName.c_str());
    }
}

// ---------------------------------------------------------------------------
// Draw – UI 스레드
// ---------------------------------------------------------------------------
void RoninMonitor_Draw()
{
    if (!bMonitorRonin) return;

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f; // 프리징 후 dt 스파이크로 알림 즉시 만료 방지
    std::vector<RoninNotification> snap;
    {
        std::lock_guard<std::mutex> lk(s_notifMtx);
        for (auto& n : s_notifications) n.timeRemaining -= dt;
        s_notifications.erase(
            std::remove_if(s_notifications.begin(), s_notifications.end(),
                           [](const RoninNotification& n){ return n.timeRemaining <= 0.f; }),
            s_notifications.end());
        snap = s_notifications;
    }

    if (snap.empty()) return;

    float  sc   = ImGui::GetIO().FontGlobalScale;
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(disp.x - 20.f, disp.y * 0.12f),
                            ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(1.f, 0.84f, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(30.f, 20.f));

    constexpr ImGuiWindowFlags kF =
        ImGuiWindowFlags_NoDecoration     | ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoSavedSettings  | ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav            | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##RoninMonitorNotif", nullptr, kF)) {
        ImGui::SetWindowFontScale(1.8f);
        
        // 헤더 1회 출력
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.6f, 1.f));
        ImGui::Text(u8" [ 재야 장수 발견!! ]");
        ImGui::PopStyleColor();
        ImGui::Separator();

        // 테이블 형태로 무장 목록 출력
        if (ImGui::BeginTable("##RoninTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn(u8"이름", ImGuiTableColumnFlags_WidthFixed, 130.f * sc);
            ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed, 90.f * sc);
            
            // 테이블 헤더 (이름        도시)
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.f), u8"이름");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.f), u8"도시");

            // 데이터 행
            for (auto& n : snap) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.3f, 1.f, 1.f, 1.f), u8"%s", n.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(1.f, 1.f, 0.6f, 1.f), u8"%s", n.cityName.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace DX11Base
