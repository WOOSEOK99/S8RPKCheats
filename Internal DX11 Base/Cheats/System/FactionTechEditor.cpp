#include "../../pch.h"
#include "../../MenuState.h"
#include "../../Cheats.h"
#include "../Officer/OfficerData.h"
#include "FactionTechEditor.h"
#include <string>

namespace DX11Base {

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

    void DrawFactionTechEditor(float scale) {
        if (!bShowFactionTechEditor) return;

        ImGui::SetNextWindowSize(ImVec2(800 * scale, 500 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"세력별 기술력 편집###FactionTechEditorWin", &bShowFactionTechEditor)) {
            
            uintptr_t root = ResolveRoot();
            if (!root) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"오류: 게임 데이터 루트 베이스를 찾을 수 없습니다.");
                ImGui::End();
                return;
            }

            ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"※ 현재 게임에 존재하는 세력의 기술력을 실시간으로 편집합니다.");
            ImGui::Separator();

            if (ImGui::BeginTable("FactionTechTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn(u8"세력명 (군주)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(u8"보병(3)", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
                ImGui::TableSetupColumn(u8"기병(3)", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
                ImGui::TableSetupColumn(u8"궁병(3)", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
                ImGui::TableSetupColumn(u8"병기(5)", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
                ImGui::TableSetupColumn(u8"함선(3)", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
                ImGui::TableSetupColumn(u8"작동", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
                ImGui::TableHeadersRow();

                for (int i = 0; i < 120; i++) {
                    uintptr_t factionBase = root + 0xC6848 + i * 0x998;
                    uintptr_t existAddr  = factionBase + 0xC5;
                    
                    if (!IsValidPtr(existAddr, 1)) continue;
                    uint8_t exist = *(uint8_t*)existAddr;
                    if (exist < 1) continue;

                    // 군주 정보를 통해 세력명 확인
                    uintptr_t lordPtr = *(uintptr_t*)(factionBase + 0xC0); // OFF_FACTION_LORD_PTR in FactionLordBonus was C6908-C6848=C0
                    std::string factionName = u8"알 수 없는 세력";
                    if (lordPtr && IsValidPtr(lordPtr + 0x08, 2)) {
                        unsigned short lordID = *(unsigned short*)(lordPtr + 0x08);
                        if (g_officerNames.count(lordID)) {
                            factionName = g_officerNames[lordID] + u8" 세력";
                        } else {
                            factionName = u8"무장 ID " + std::to_string(lordID) + u8" 세력";
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(factionName.c_str());

                    uintptr_t techAddr = factionBase + 0x205;
                    if (IsValidPtr(techAddr, 5)) {
                        uint8_t* pTech = (uint8_t*)techAddr;
                        
                        auto TechInput = [&](int idx, int maxVal, const char* idLabel) {
                            ImGui::TableNextColumn();
                            int val = pTech[idx];
                            ImGui::SetNextItemWidth(60 * scale);
                            char id[32]; sprintf_s(id, "##t_%d_%d", i, idx);
                            if (ImGui::InputInt(id, &val, 0, 0)) {
                                if (val < 0) val = 0;
                                if (val > maxVal) val = maxVal;
                                pTech[idx] = (uint8_t)val;
                            }
                        };

                        TechInput(0, 3, "Inf");
                        TechInput(1, 3, "Cav");
                        TechInput(2, 3, "Arc");
                        TechInput(3, 5, "Wep");
                        TechInput(4, 3, "Shp");
                    }

                    ImGui::TableNextColumn();
                    char btnId[32]; sprintf_s(btnId, u8"MAX###m_%d", i);
                    if (ImGui::Button(btnId)) {
                        uint8_t* pTech = (uint8_t*)techAddr;
                        pTech[0] = 3; pTech[1] = 3; pTech[2] = 3; pTech[3] = 5; pTech[4] = 3;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::End();
        }
    }
}
