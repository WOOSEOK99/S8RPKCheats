#include "Cheats.h"
#include "Engine.h"
#include "Menu.h"
#include "pch.h"
#include "showlog.h"
#include <cstdarg>
#include <string>
#include <vector>

namespace DX11Base {
  extern int *pSelectedVar;

    // [신규] 숫자 패드(계산기) 팝업 함수
    void ShowCalcPopup(const char* title, int* target) {
        if (ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(u8"현재 설정값: %d", *target);
            ImGui::Separator();

            // 1~9 숫자 버튼 (마우스용)
            for (int i = 1; i <= 9; i++) {
                if (ImGui::Button(std::to_string(i).c_str(), ImVec2(45, 40))) {
                    if (*target < 100000000)
                        *target = (*target * 10) + i;
                }
                if (i % 3 != 0)
                    ImGui::SameLine();
            }

            // 0, C, Back 버튼
            if (ImGui::Button("0", ImVec2(45, 40))) {
                if (*target < 100000000) *target *= 10;
            }
            ImGui::SameLine();
            if (ImGui::Button("C", ImVec2(45, 40))) {
                *target = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("<-", ImVec2(45, 40))) {
                *target /= 10;
            }

            ImGui::Separator();
            
            // 확인 버튼
            if (ImGui::Button(u8"확인 (Enter)", ImVec2(-1, 40)) || 
                ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                pSelectedVar = nullptr;
                ImGui::CloseCurrentPopup();
            }
            
            // 키보드 숫자 입력 지원 유지 (선택 사항이지만 유용함)
            for (int i = 0; i <= 9; i++) {
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i)) || ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + i))) {
                    if (*target < 100000000) *target = (*target * 10) + i;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) *target /= 10;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) *target = 0;

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pSelectedVar = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
} // namespace DX11Base