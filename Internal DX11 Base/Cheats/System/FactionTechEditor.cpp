#include "../../pch.h"
#include "../../MenuState.h"
#include "../../Cheats.h"
#include "../Officer/OfficerData.h"
#include "FactionTechEditor.h"
#include <string>
#include <vector>
#include <chrono>

namespace DX11Base {

    struct FactionCache {
        int index;
        uintptr_t techAddr;
        std::string name;
    };

    static std::vector<FactionCache> g_factionCache;
    static std::chrono::steady_clock::time_point g_lastCacheUpdate;
    static bool g_needsCacheRefresh = true;

    static uintptr_t ResolveRoot() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return 0;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
        if (!p) return 0;
        p = *(uintptr_t*)(p + 0x0); if (!p) return 0;
        p = *(uintptr_t*)(p + 0x8); if (!p) return 0;
        p = *(uintptr_t*)(p + 0x10); if (!p) return 0;
        p = *(uintptr_t*)(p + 0x0); if (!p) return 0;

        return p;
    }

    static void UpdateFactionCache() {
        uintptr_t root = ResolveRoot();
        if (!root) return;

        g_factionCache.clear();
        for (int i = 0; i < 120; i++) {
            uintptr_t factionBase = root + 0xC6848 + i * 0x998;
            uintptr_t existAddr  = factionBase + 0xC5;
            
            // IsValidPtr은 시스템 콜이므로 필요한 시점에만 최소한으로 호출
            if (!IsValidPtr(existAddr, 1)) continue;
            if (*(uint8_t*)existAddr < 1) continue;

            FactionCache entry;
            entry.index = i;
            entry.techAddr = factionBase + 0x205;
            
            // 군주 정보를 통해 세력명 확인 (캐싱 시점에만 수행)
            uintptr_t lordPtr = *(uintptr_t*)(factionBase + 0xC0);
            entry.name = u8"알 수 없는 세력";
            if (lordPtr && IsValidPtr(lordPtr + 0x08, 2)) {
                unsigned short lordID = *(unsigned short*)(lordPtr + 0x08);
                if (g_officerNames.count(lordID)) {
                    entry.name = g_officerNames[lordID] + u8" 세력";
                } else {
                    entry.name = u8"무장 ID " + std::to_string(lordID) + u8" 세력";
                }
            }
            g_factionCache.push_back(entry);
        }
        g_lastCacheUpdate = std::chrono::steady_clock::now();
        g_needsCacheRefresh = false;
    }

    void DrawFactionTechEditor(float scale) {
        if (!bShowFactionTechEditor) {
            g_needsCacheRefresh = true; 
            return;
        }

        // 창이 처음 열릴 때(g_needsCacheRefresh가 true일 때)만 캐시 갱신
        if (g_needsCacheRefresh) {
            UpdateFactionCache();
        }

        ImGui::SetNextWindowSize(ImVec2(650 * scale, 300 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"세력별 기술력 편집###FactionTechEditorWin", &bShowFactionTechEditor)) {
            
            if (g_factionCache.empty()) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"세력 정보를 불러오는 중이거나 활성 세력이 없습니다...");
                if (ImGui::Button(u8"강제 갱신")) g_needsCacheRefresh = true;
                ImGui::End();
                return;
            }

            ImGui::Separator();

            if (ImGui::BeginTable("FactionTechTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn(u8"세력명 (군주)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(u8"보병(3)", ImGuiTableColumnFlags_WidthFixed, 75.0f * scale);
                ImGui::TableSetupColumn(u8"기병(3)", ImGuiTableColumnFlags_WidthFixed, 75.0f * scale);
                ImGui::TableSetupColumn(u8"궁병(3)", ImGuiTableColumnFlags_WidthFixed, 75.0f * scale);
                ImGui::TableSetupColumn(u8"병기(5)", ImGuiTableColumnFlags_WidthFixed, 75.0f * scale);
                ImGui::TableSetupColumn(u8"함선(3)", ImGuiTableColumnFlags_WidthFixed, 75.0f * scale);
                ImGui::TableSetupColumn(u8"작동", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
                ImGui::TableHeadersRow();

                for (const auto& faction : g_factionCache) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(faction.name.c_str());

                    uint8_t* pTech = (uint8_t*)faction.techAddr;
                    
                    auto TechInput = [&](int idx, int maxVal) {
                        ImGui::TableNextColumn();
                        int val = pTech[idx];
                        ImGui::SetNextItemWidth(70 * scale);
                        char id[32]; sprintf_s(id, "##t_%d_%d", faction.index, idx);
                        if (ImGui::InputInt(id, &val, 1, 1)) {
                            if (val < 0) val = 0;
                            if (val > maxVal) val = maxVal;
                            pTech[idx] = (uint8_t)val;
                        }
                    };

                    TechInput(0, 3); // 보병
                    TechInput(1, 3); // 기병
                    TechInput(2, 3); // 궁병
                    TechInput(3, 5); // 병기
                    TechInput(4, 3); // 함선

                    ImGui::TableNextColumn();
                    char btnId[32]; sprintf_s(btnId, u8"MAX###m_%d", faction.index);
                    if (ImGui::Button(btnId)) {
                        pTech[0] = 3; pTech[1] = 3; pTech[2] = 3; pTech[3] = 5; pTech[4] = 3;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::End();
        }
    }
}
