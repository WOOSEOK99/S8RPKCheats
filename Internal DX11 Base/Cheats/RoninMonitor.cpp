// =============================================================================
// RoninMonitor.cpp  –  재야 장수 자동 감시 모듈 (독립형)
// 2026-04-04
//
// [작동 방식]
//   1. RoninMonitor_Tick(p1)을 Menu::Loops() 백그라운드 스레드에서 매 루프 호출.
//   2. IsConfigReady() (= 3초 지연 통과)가 참이 되면 단 한 번, p1 기반으로
//      무장 배열 시작점을 자동 계산합니다.
//        arrayBase = p1 - ((heroID - 1) * 0x3D0)
//   3. 이후 2초마다 5102명 전수 조사하여 미발견→재야 전환을 감지합니다.
//   4. RoninMonitor_Draw()를 Engine.cpp 렌더링 루프에서 호출해 알림창을 그립니다.
//
// [롤백 방법]
//   - 이 파일(.h/.cpp) 삭제
//   - Menu.cpp 호출 1줄, Engine.cpp 호출 1줄 주석 처리
//   - SelectOfficercapture.cpp, Config.cpp 에 손댄 것이 없으므로 완전 복구됨.
// =============================================================================

#include "RoninMonitor.h"
#include "Cheats.h"          // GetGameBase(), IsValidPtr(), bMonitorRonin
#include "Config.h"          // IsConfigReady()
#include "OfficerData.h"     // g_officerNames
#include "CityData.h"        // g_CityList, g_CityCount
#include "MenuState.h"       // bMonitorRonin
#include "showlog.h"         // AddLog()
#include "Framework/imgui.h"
#include "pch.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>

namespace DX11Base {

// ---------------------------------------------------------------------------
// 내부 전용 상태 변수 (외부에서 직접 접근 불가)
// ---------------------------------------------------------------------------
namespace {
    struct RoninNotification {
        std::string name;
        std::string cityName;
        float       timeRemaining;
    };

    static uintptr_t                         s_arrayBase       = 0;
    static bool                              s_baseResolved    = false;
    static bool                              s_initialized     = false;
    static std::unordered_map<int, uint8_t>  s_prevStatuses;
    static std::vector<RoninNotification>    s_notifications;
}

// ---------------------------------------------------------------------------
// 헬퍼: 도시 배열 베이스 주소를 안전하게 획득
// ---------------------------------------------------------------------------
static uintptr_t GetCityArrayBase()
{
    uintptr_t gameBase = GetGameBase();
    if (!gameBase) return 0;

    // gameBase + 0xE0 → p1(주인공) → p1+0x0 계열 포인터 체인에서 도시 배열 획득
    // (이 오프셋은 다른 모듈에서 검증된 0x34C8630 경로를 사용)
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase) return 0;

    uintptr_t pa = *(uintptr_t*)(exeBase + 0x34C8630);
    if (!IsValidPtr(pa, 8)) return 0;
    uintptr_t pb = *(uintptr_t*)(pa);
    if (!IsValidPtr(pb, 8)) return 0;
    uintptr_t cityBase = *(uintptr_t*)(pb);
    return (cityBase > 0x10000) ? cityBase : 0;
}

// ---------------------------------------------------------------------------
// 주인공 포인터(p1)를 이용해 무장 배열 시작점을 딱 한 번 자동 계산
// ---------------------------------------------------------------------------
static bool TryResolveBase(uintptr_t p1)
{
    if (s_baseResolved) return true;
    if (!p1 || p1 < 0x10000) return false;
    if (!IsValidPtr(p1, 0x10)) return false;

    unsigned short heroID = *(unsigned short*)(p1 + 0x08);
    if (heroID == 0 || heroID > 5102) return false;

    uintptr_t candidate = p1 - (uintptr_t)(heroID - 1) * 0x3D0;

    // 간단한 유효성 검증: 1번 무장 주소가 읽히는지 확인
    if (!IsValidPtr(candidate, 0x20)) return false;

    s_arrayBase    = candidate;
    s_baseResolved = true;
    AddLog(u8"[RoninMonitor] 무장 배열 주소 자동 확보: 0x%llX (주인공 ID %d 기반)",
           (unsigned long long)s_arrayBase, (int)heroID);
    return true;
}

// ---------------------------------------------------------------------------
// RoninMonitor_Tick  –  백그라운드 루프에서 호출 (Menu::Loops)
// ---------------------------------------------------------------------------
void RoninMonitor_Tick(uintptr_t p1)
{
    // ① 모니터링 옵션이 꺼져 있으면 상태 초기화 후 종료
    if (!bMonitorRonin) {
        s_initialized  = false;
        s_baseResolved = false;
        s_arrayBase    = 0;
        s_prevStatuses.clear();
        s_notifications.clear();
        return;
    }

    // ② Config 3초 지연 미통과 시 대기
    if (!IsConfigReady()) return;

    // ③ 주소 미확보 시 계산 시도
    if (!TryResolveBase(p1)) return;

    // ④ 2초 주기 체크
    static uint64_t s_lastCheck = 0;
    uint64_t now = GetTickCount64();
    if (now - s_lastCheck < 2000) return;
    s_lastCheck = now;

    // ⑤ 도시 배열 주소 획득
    uintptr_t cityBase = GetCityArrayBase();

    // ⑥ 전수 조사
    for (int i = 1; i <= 5102; i++) {
        uintptr_t addr = s_arrayBase + (uintptr_t)(i - 1) * 0x3D0;
        if (!IsValidPtr(addr, 0x30)) continue;

        uint8_t status = *(uint8_t*)(addr + 0x10);

        if (s_initialized) {
            uint8_t prev = s_prevStatuses[i];
            // 미발견(0x68, 0x78) → 재야(0x58) 전환 감지
            if ((prev == 0x68 || prev == 0x78) && status == 0x58) {
                std::string name = g_officerNames.count(i)
                                   ? g_officerNames[i]
                                   : u8"알 수 없는 무장";

                std::string city = u8"알 수 없는 장소";
                if (cityBase) {
                    uintptr_t cityPtr = *(uintptr_t*)(addr + 0x20);
                    if (cityPtr >= cityBase) {
                        int idx = (int)((cityPtr - cityBase) / 0x2A0);
                        if (idx >= 0 && idx < g_CityCount)
                            city = g_CityList[idx].cityname;
                    }
                }

                s_notifications.push_back({ name, city, 12.0f });
                AddLog(u8"[재야 감지] %s → %s", name.c_str(), city.c_str());
            }
        }
        s_prevStatuses[i] = status;
    }
    s_initialized = true;
}

// ---------------------------------------------------------------------------
// RoninMonitor_Draw  –  렌더링 루프에서 호출 (Engine.cpp)
// ---------------------------------------------------------------------------
void RoninMonitor_Draw()
{
    if (!bMonitorRonin || s_notifications.empty()) return;

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(
        ImVec2(screen.x - 20.0f, screen.y * 0.12f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(30.0f, 20.0f));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoMove;

    float dt = ImGui::GetIO().DeltaTime;

    if (ImGui::Begin("##RoninMonitorNotif", nullptr, kFlags)) {
        ImGui::SetWindowFontScale(1.8f);

        for (auto it = s_notifications.begin(); it != s_notifications.end(); ) {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.6f, 1.0f));
            ImGui::Text(u8" [ 재야 장수 등판 발견!! ]");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 1.0f, 1.0f));
            ImGui::Text(u8"  \u25b6 %s (%s)", it->name.c_str(), it->cityName.c_str());
            ImGui::PopStyleColor();

            it->timeRemaining -= dt;
            it = (it->timeRemaining <= 0.0f) ? s_notifications.erase(it) : ++it;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace DX11Base
