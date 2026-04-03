#include <string>
#include <cstdarg>
#include <vector>
#include "showlog.h"
#include "pch.h"
#include "Engine.h"
#include "Menu.h"
#include "Cheats.h"

namespace DX11Base {
    extern int* pSelectedVar;

    // [신규] 숫자 패드(계산기) 팝업 함수
    void ShowCalcPopup(const char* title, int* target) {
        if (ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(u8"입력값: %d", *target);
            ImGui::Separator();

            // 1~9 숫자 버튼
            for (int i = 1; i <= 9; i++) {
                if (ImGui::Button(std::to_string(i).c_str(), ImVec2(40, 40))) {
                    if (*target < 1000000)
                        *target = (*target * 10) + i;
                }
                if (i % 3 != 0)
                    ImGui::SameLine();
            }

            // 0, C, Back 버튼
            if (ImGui::Button("0", ImVec2(40, 40))) {
                *target *= 10;
            }
            ImGui::SameLine();
            if (ImGui::Button("C", ImVec2(40, 40))) {
                *target = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("<-", ImVec2(40, 40))) {
                *target /= 10;
            }

            ImGui::Separator();
            if (ImGui::Button(u8"확인", ImVec2(-1, 35))) {
                pSelectedVar = nullptr; // 팝업 닫기 트리거
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
} // namespace DX11Base