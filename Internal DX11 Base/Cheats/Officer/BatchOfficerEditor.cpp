#include "../../pch.h"
#include "../../MenuState.h"
#include "OfficerDetail.h"
#include "StatMonitor.h"

namespace DX11Base {

    // 레벨 버튼 색상 정의 (스크린샷 기반)
    static ImVec4 GetLevelColor(int level) {
        switch (level) {
        case 1: return ImVec4(0.24f, 0.52f, 0.88f, 1.0f); // 파랑
        case 2: return ImVec4(0.27f, 0.75f, 0.27f, 1.0f); // 초록
        case 3: return ImVec4(0.95f, 0.70f, 0.00f, 1.0f); // 주황/황금
        default: return ImVec4(0.40f, 0.40f, 0.40f, 1.0f); // 회색 (0)
        }
    }

    // 개별 레벨 버튼 UI 컴포넌트
    static void DrawBatchEditItem(const char* label, bool* enabled, int* level, float scale, int id) {
        ImGui::TableNextColumn();
        
        // 체크박스 (일괄 적용 여부)
        char checkId[64];
        sprintf_s(checkId, "##en_%d_%s", id, label);
        ImGui::Checkbox(checkId, enabled);
        ImGui::SameLine(0, 5 * scale);
        
        // 이름 표시 (가변 폭 대응을 위해 텍스트 사용)
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetColumnWidth() - 40 * scale); // 우측 정렬 느낌으로 레벨 버튼 배치

        // 레벨 버튼 (클릭 시 0->1->2->3->0 순환)
        char btnId[64];
        sprintf_s(btnId, "%d##lvl_%d_%s", *level, id, label);
        
        ImGui::PushStyleColor(ImGuiCol_Button, GetLevelColor(*level));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetLevelColor(*level)); 
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetLevelColor(*level));
        
        if (ImGui::Button(btnId, ImVec2(25 * scale, 22 * scale))) {
            *level = (*level + 1) % 4;
            *enabled = true; // 레벨을 변경하면 자동으로 적용 대상으로 간주
        }
        ImGui::PopStyleColor(3);
    }

    void DrawBatchOfficerEditWindow(float scale) {
        if (!bShowBatchOfficerEditWin) return;

        ImGui::SetNextWindowSize(ImVec2(750 * scale, 460 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"모든 무장 전법 및 특기 일괄 편집###BatchOfficerEditWin", &bShowBatchOfficerEditWin, ImGuiWindowFlags_NoSavedSettings)) {
            
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"정보: 체크된 항목들만 모든 유효 무장(5102명)에게 일괄 적용됩니다. 레벨을 클릭하면 변경됩니다.");
            ImGui::Separator();

            // --- [ 전법 섹션 ] ---
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), u8"[전법]");
            static const char* tacticGroupNames[] = { u8"보병", u8"기병", u8"궁병", u8"함선", u8"군략", u8"보조", u8"둔갑" };
            static const char* tactics[7][5] = {
                { u8"강격", u8"난격", u8"교란", u8"맹돌", u8"창금" },
                { u8"연격", u8"돌격", u8"급습", u8"기사", u8"차현" },
                { u8"제사", u8"난사", u8"화시", u8"원사", u8"시람" },
                { u8"화전", u8"난전", u8"연환", u8"돌관", u8"폭선" },
                { u8"열화", u8"격류", u8"낙석", u8"요격", u8"동토" },
                { u8"분기", u8"고무", u8"매성", u8"치료", u8"천계" },
                { u8"풍변", u8"천변", u8"요술", u8"환술", u8"낙뢰" }
            };

            if (ImGui::BeginTable("TacticsGrid", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn(u8"분류", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
                for (int i = 0; i < 5; i++) ImGui::TableSetupColumn("##t", ImGuiTableColumnFlags_WidthStretch);

                for (int g = 0; g < 7; g++) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(tacticGroupNames[g]);
                    
                    for (int i = 0; i < 5; i++) {
                        int idx = g * 5 + i;
                        DrawBatchEditItem(tactics[g][i], &g_batchTactics[idx].enabled, &g_batchTactics[idx].level, scale, idx + 100);
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- [ 특기 섹션 ] ---
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), u8"[특기]");
            static const char* traitGroupNames[] = { u8"임무", u8"지모", u8"병과", u8"군사" };
            static const char* traits[4][6] = {
                { u8"경작", u8"상재", u8"축성", u8"경비", u8"발명", u8"천성" },
                { u8"교섭", u8"허보", u8"공작", u8"화술", u8"열변", u8"귀모" },
                { u8"보장", u8"기장", u8"궁장", u8"수군", u8"조기", u8"신산" },
                { u8"원호", u8"파성", u8"행군", u8"여력", u8"과감", u8"위풍" }
            };

            if (ImGui::BeginTable("TraitsGrid", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn(u8"분류", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
                for (int i = 0; i < 6; i++) ImGui::TableSetupColumn("##tr", ImGuiTableColumnFlags_WidthStretch);

                for (int g = 0; g < 4; g++) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(traitGroupNames[g]);
                    
                    for (int i = 0; i < 6; i++) {
                        int idx = g * 6 + i;
                        DrawBatchEditItem(traits[g][i], &g_batchTraits[idx].enabled, &g_batchTraits[idx].level, scale, idx + 200);
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(u8"모든 무장에게 일괄 적용!!!", ImVec2(-1, 40 * scale))) {
                ImGui::OpenPopup(u8"BatchConfirmPopup");
            }

            if (ImGui::BeginPopupModal(u8"BatchConfirmPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(u8"정말로 모든 무장에게 선택한 값을 적용하시겠습니까?");
                ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"이 작업은 되돌릴 수 없으며, 모든 무장의 메모리를 직접 수정합니다.");
                ImGui::Separator();

                if (ImGui::Button(u8"예, 적용합니다", ImVec2(120 * scale, 0))) {
                    ApplyBatchOfficerEdit();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"아니오", ImVec2(120 * scale, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::End();
        }
    }
}
