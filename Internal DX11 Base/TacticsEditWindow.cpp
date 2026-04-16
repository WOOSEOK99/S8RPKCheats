#include "TacticsEditWindow.h"
#include "Cheats/War/Catapult.h"
#include "Cheats/War/Celestia.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/Dongto.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/Terrainignore.h"
#include "NotificationManager.h"
#include "Config.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace DX11Base {
  namespace MenuSections {
    void DrawTacticsEditWindow(float scale) {
      if (!bShowTacticsEditWin)
        return;

      ImGui::SetNextWindowSize(ImVec2(950 * scale, 700 * scale), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(u8"전법 세부 수정###TacticsEditWin", &bShowTacticsEditWin, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("TacticsTabs")) {
          if (ImGui::BeginTabItem(u8"치료")) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"[ 치료 전법 설정 ]");

            // Level 1
            if (ImGui::CollapsingHeader(u8"Level 1", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::Checkbox(u8"자기치료 활성화##Lv1", &bHealLv1_Self);

              ImGui::Spacing();
              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##Lv1", &v_HealLv1_Amount)) {
                if (v_HealLv1_Amount < 0)
                  v_HealLv1_Amount = 0;
                if (v_HealLv1_Amount > 32767)
                  v_HealLv1_Amount = 32767;
              }
            }

            // Level 2
            if (ImGui::CollapsingHeader(u8"Level 2", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::Checkbox(u8"자기치료 활성화##Lv2", &bHealLv2_Self);

              ImGui::Text(u8"치료 범위 선택:");
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
              for (int i = 1; i <= 11; i++) {
                bool isSelected = (v_HealLv2_Range == i);
                ImGui::PushID(i + 200);
                if (isSelected) {
                  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1)); // Yellow border
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                }

                ImGui::BeginGroup();
                if (g_RangeTextures[i]) {
                  char btnId[32];
                  sprintf_s(btnId, "range2_%d", i);
                  // Use a subtle background even when NOT selected to make it look like a grid slot
                  ImVec4 bgCol = isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0);
                  if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                         ImVec2(0, 0), ImVec2(1, 1), bgCol)) {
                    v_HealLv2_Range = i;
                  }
                } else {
                  if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale))) {
                    v_HealLv2_Range = i;
                  }
                }
                ImGui::Text(" %d", i);
                ImGui::EndGroup();

                if (isSelected) {
                  ImGui::PopStyleVar();
                  ImGui::PopStyleColor();
                }
                ImGui::PopID();

                if (i < 11)
                  ImGui::SameLine();
              }
              ImGui::PopStyleVar();

              ImGui::Spacing();
              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##Lv2", &v_HealLv2_Amount)) {
                if (v_HealLv2_Amount < 0)
                  v_HealLv2_Amount = 0;
                if (v_HealLv2_Amount > 32767)
                  v_HealLv2_Amount = 32767;
              }
            }

            // Level 3
            if (ImGui::CollapsingHeader(u8"Level 3", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::Checkbox(u8"자기치료 활성화##Lv3", &bHealLv3_Self);

              ImGui::Text(u8"치료 범위 선택:");
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
              for (int i = 1; i <= 11; i++) {
                bool isSelected = (v_HealLv3_Range == i);
                ImGui::PushID(i + 300);
                if (isSelected) {
                  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1)); // Yellow border
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                }

                ImGui::BeginGroup();
                if (g_RangeTextures[i]) {
                  char btnId[32];
                  sprintf_s(btnId, "range3_%d", i);
                  ImVec4 bgCol = isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0);
                  if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                         ImVec2(0, 0), ImVec2(1, 1), bgCol)) {
                    v_HealLv3_Range = i;
                  }
                } else {
                  if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale))) {
                    v_HealLv3_Range = i;
                  }
                }
                ImGui::Text(" %d", i);
                ImGui::EndGroup();

                if (isSelected) {
                  ImGui::PopStyleVar();
                  ImGui::PopStyleColor();
                }
                ImGui::PopID();

                if (i < 11)
                  ImGui::SameLine();
              }
              ImGui::PopStyleVar();

              ImGui::Spacing();
              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##Lv3", &v_HealLv3_Amount)) {
                if (v_HealLv3_Amount < 0)
                  v_HealLv3_Amount = 0;
                if (v_HealLv3_Amount > 32767)
                  v_HealLv3_Amount = 32767;
              }
            }

            ImGui::Separator();
            if (ImGui::Button(u8"설정 적용", ImVec2(-1, 30 * scale))) {
              SetSelfHeal(bSelfHeal); // 리프레시를 위해 강제로 다시 호출
              SaveConfig();
              AddNotification(u8"치료 설정 적용 완료");
            }
            // ImGui::TextWrapped(u8"※ 전투 중 실시간으로 반영됩니다.");
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(u8"천계")) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"[ 천계 전법 설정 ]");
            ImGui::Checkbox(u8"천계 전법 강화 활성화", &bCelestial);
            ImGui::Separator();

            // Level 1
            if (ImGui::CollapsingHeader(u8"Level 1", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##CelestiaLv1ProbSlider", &v_CelestiaLv1_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##CelestiaLv1ProbInput", &v_CelestiaLv1_Prob, 0);

              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##CLv1", &v_CelestiaLv1_Amount, 0)) {
                if (v_CelestiaLv1_Amount < 0)
                  v_CelestiaLv1_Amount = 0;
              }

              ImGui::Text(u8"범위 선택:");
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
              for (int i = 1; i <= 11; i++) {
                bool isSelected = (v_CelestiaLv1_Range == i);
                ImGui::PushID(i + 600);
                if (isSelected) {
                  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                }
                if (g_RangeTextures[i]) {
                  char btnId[32];
                  sprintf_s(btnId, "cl_range1_%d", i);
                  if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                         ImVec2(0, 0), ImVec2(1, 1),
                                         isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                    v_CelestiaLv1_Range = i;
                } else {
                  if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                    v_CelestiaLv1_Range = i;
                }
                if (isSelected) {
                  ImGui::PopStyleVar();
                  ImGui::PopStyleColor();
                }
                ImGui::PopID();
                if (i < 11)
                  ImGui::SameLine();
              }
              ImGui::PopStyleVar();
            }

            // Level 2
            if (ImGui::CollapsingHeader(u8"Level 2", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##CelestiaLv2ProbSlider", &v_CelestiaLv2_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##CelestiaLv2ProbInput", &v_CelestiaLv2_Prob, 0);

              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##CLv2", &v_CelestiaLv2_Amount, 0)) {
                if (v_CelestiaLv2_Amount < 0)
                  v_CelestiaLv2_Amount = 0;
              }

              ImGui::Text(u8"범위 선택:");
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
              for (int i = 1; i <= 11; i++) {
                bool isSelected = (v_CelestiaLv2_Range == i);
                ImGui::PushID(i + 700);
                if (isSelected) {
                  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                }
                if (g_RangeTextures[i]) {
                  char btnId[32];
                  sprintf_s(btnId, "cl_range2_%d", i);
                  if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                         ImVec2(0, 0), ImVec2(1, 1),
                                         isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                    v_CelestiaLv2_Range = i;
                } else {
                  if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                    v_CelestiaLv2_Range = i;
                }
                if (isSelected) {
                  ImGui::PopStyleVar();
                  ImGui::PopStyleColor();
                }
                ImGui::PopID();
                if (i < 11)
                  ImGui::SameLine();
              }
              ImGui::PopStyleVar();
            }

            // Level 3
            if (ImGui::CollapsingHeader(u8"Level 3", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##CelestiaLv3ProbSlider", &v_CelestiaLv3_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##CelestiaLv3ProbInput", &v_CelestiaLv3_Prob, 0);

              ImGui::TextUnformatted(u8"치료량:");
              ImGui::SameLine(100 * scale);
              ImGui::SetNextItemWidth(120 * scale);
              if (ImGui::InputInt(u8"##CLv3", &v_CelestiaLv3_Amount, 0)) {
                if (v_CelestiaLv3_Amount < 0)
                  v_CelestiaLv3_Amount = 0;
              }

              ImGui::Text(u8"범위 선택:");
              ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
              for (int i = 1; i <= 11; i++) {
                bool isSelected = (v_CelestiaLv3_Range == i);
                ImGui::PushID(i + 800);
                if (isSelected) {
                  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                }
                if (g_RangeTextures[i]) {
                  char btnId[32];
                  sprintf_s(btnId, "cl_range3_%d", i);
                  if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                         ImVec2(0, 0), ImVec2(1, 1),
                                         isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                    v_CelestiaLv3_Range = i;
                } else {
                  if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                    v_CelestiaLv3_Range = i;
                }
                if (isSelected) {
                  ImGui::PopStyleVar();
                  ImGui::PopStyleColor();
                }
                ImGui::PopID();
                if (i < 11)
                  ImGui::SameLine();
              }
              ImGui::PopStyleVar();
            }

            ImGui::Separator();
            if (ImGui::Button(u8"설정 적용##Celestia", ImVec2(-1, 30 * scale))) {
              SaveConfig();
              SetCelestialMod(bCelestial);
              AddNotification(u8"천계 설정 적용 완료");
            }
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(u8"동토")) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), u8"[ 동토 전법 설정 ]");

            // Level 1
            if (ImGui::CollapsingHeader(u8"Level 1", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv1ProbSlider", &v_DongtoLv1_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv1ProbInput", &v_DongtoLv1_Prob, 0);

              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"상태이상 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv1StateProbSlider", &v_DongtoLv1_StateProb, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv1StateProbInput", &v_DongtoLv1_StateProb, 0);
            }

            // Level 2
            if (ImGui::CollapsingHeader(u8"Level 2", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv2ProbSlider", &v_DongtoLv2_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv2ProbInput", &v_DongtoLv2_Prob, 0);

              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"상태이상 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv2StateProbSlider", &v_DongtoLv2_StateProb, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv2StateProbInput", &v_DongtoLv2_StateProb, 0);
            }

            // Level 3
            if (ImGui::CollapsingHeader(u8"Level 3", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"성공 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv3ProbSlider", &v_DongtoLv3_Prob, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv3ProbInput", &v_DongtoLv3_Prob, 0);

              ImGui::AlignTextToFramePadding(); // 위젯 높이에 맞춰 텍스트 위치 조정
              ImGui::TextUnformatted(u8"상태이상 확률 (%):");
              ImGui::SameLine(130 * scale);
              ImGui::SetNextItemWidth(200 * scale);
              ImGui::SliderInt(u8"##DongtoLv3StateProbSlider", &v_DongtoLv3_StateProb, 0, 100);
              ImGui::SameLine();
              ImGui::SetNextItemWidth(80 * scale);
              ImGui::InputInt(u8"##DongtoLv3StateProbInput", &v_DongtoLv3_StateProb, 0);
            }

            ImGui::Separator();
            if (ImGui::Button(u8"설정 적용##Dongto", ImVec2(-1, 30 * scale))) {
              SetDongto(bDongto);
              SaveConfig();
              AddNotification(u8"동토 설정 적용 완료");
            }
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(u8"투석 병기")) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1, 1), u8"[ 투석 병기 설정 ]");
            ImGui::TextUnformatted(u8"최소 사거리:");
            ImGui::SameLine(100 * scale);
            ImGui::SetNextItemWidth(200 * scale);
            ImGui::SliderInt(u8"##CatapultMinRangeSlider", &v_Catapult_MinRange, 1, 20);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80 * scale);
            ImGui::InputInt(u8"##CatapultMinRangeInput", &v_Catapult_MinRange, 0);

            ImGui::TextUnformatted(u8"최대 사거리:");
            ImGui::SameLine(100 * scale);
            ImGui::SetNextItemWidth(200 * scale);
            ImGui::SliderInt(u8"##CatapultMaxRangeSlider", &v_Catapult_MaxRange, 1, 20);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80 * scale);
            ImGui::InputInt(u8"##CatapultMaxRangeInput", &v_Catapult_MaxRange, 0);

            ImGui::TextUnformatted(u8"부대 위력:");
            ImGui::SameLine(100 * scale);
            ImGui::SetNextItemWidth(120 * scale);
            if (ImGui::InputInt(u8"##CatapultAmount", &v_Catapult_Amount, 0)) {
              if (v_Catapult_Amount < 0)
                v_Catapult_Amount = 0;
            }

            ImGui::Text(u8"범위 선택:");
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
            for (int i = 1; i <= 11; i++) {
              bool isSelected = (v_Catapult_Range == i);
              ImGui::PushID(i + 900);
              if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
              }
              if (g_RangeTextures[i]) {
                char btnId[32];
                sprintf_s(btnId, "cat_range_%d", i);
                if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                  v_Catapult_Range = i;
              } else {
                if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                  v_Catapult_Range = i;
              }
              if (isSelected) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
              }
              ImGui::PopID();
              if (i < 11)
                ImGui::SameLine();
            }
            ImGui::PopStyleVar();

            ImGui::Separator();
            if (ImGui::Button(u8"투석 설정 적용", ImVec2(-1, 30 * scale))) {
              SaveConfig();
              SetCatapultCheat(bCatapult);
              AddNotification(u8"투석 설정 적용 완료");
            }
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(u8"격류/낙석")) {
            ImGui::TextColored(ImVec4(0, 1, 1, 1), u8"[ 격류/낙석 설정 ]");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"※ 발동 조건 : 격류 - 비 / 낙석 - 강풍");

            if (ImGui::CollapsingHeader(u8"격류 (Torrent)", ImGuiTreeNodeFlags_DefaultOpen)) {
              for (int lv = 1; lv <= 3; lv++) {
                char lvHeader[32];
                sprintf_s(lvHeader, "Level %d##Water", lv);
                if (ImGui::TreeNodeEx(lvHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                  int *pAmount = (lv == 1) ? &v_WaterLv1_Amount : (lv == 2) ? &v_WaterLv2_Amount : &v_WaterLv3_Amount;
                  int *pRange = (lv == 1) ? &v_WaterLv1_Range : (lv == 2) ? &v_WaterLv2_Range : &v_WaterLv3_Range;

                  ImGui::AlignTextToFramePadding();
                  ImGui::TextUnformatted(u8"위력:");
                  ImGui::SameLine(100 * scale);
                  ImGui::SetNextItemWidth(120 * scale);
                  ImGui::InputInt((std::string("##WaterAmt") + std::to_string(lv)).c_str(), pAmount, 0);

                  ImGui::TextUnformatted(u8"범위:");
                  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
                  for (int i = 1; i <= 11; i++) {
                    bool isSelected = (*pRange == i);
                    ImGui::PushID(i + 1000 + (lv * 100));
                    if (isSelected) {
                      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                    }
                    if (g_RangeTextures[i]) {
                      char btnId[32];
                      sprintf_s(btnId, "w_range%d_%d", lv, i);
                      if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                             ImVec2(0, 0), ImVec2(1, 1),
                                             isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                        *pRange = i;
                    } else {
                      if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                        *pRange = i;
                    }
                    if (isSelected) {
                      ImGui::PopStyleVar();
                      ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                    if (i < 11)
                      ImGui::SameLine();
                  }
                  ImGui::PopStyleVar();
                  ImGui::TreePop();
                }
              }
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader(u8"낙석 (Falling Rocks)", ImGuiTreeNodeFlags_DefaultOpen)) {
              for (int lv = 1; lv <= 3; lv++) {
                char lvHeader[32];
                sprintf_s(lvHeader, "Level %d##Stone", lv);
                if (ImGui::TreeNodeEx(lvHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                  int *pAmount = (lv == 1) ? &v_StoneLv1_Amount : (lv == 2) ? &v_StoneLv2_Amount : &v_StoneLv3_Amount;
                  int *pRange = (lv == 1) ? &v_StoneLv1_Range : (lv == 2) ? &v_StoneLv2_Range : &v_StoneLv3_Range;

                  ImGui::AlignTextToFramePadding();
                  ImGui::TextUnformatted(u8"위력:");
                  ImGui::SameLine(100 * scale);
                  ImGui::SetNextItemWidth(120 * scale);
                  ImGui::InputInt((std::string("##StoneAmt") + std::to_string(lv)).c_str(), pAmount, 0);

                  ImGui::TextUnformatted(u8"범위:");
                  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5 * scale, 5 * scale));
                  for (int i = 1; i <= 11; i++) {
                    bool isSelected = (*pRange == i);
                    ImGui::PushID(i + 2000 + (lv * 100));
                    if (isSelected) {
                      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
                      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f * scale);
                    }
                    if (g_RangeTextures[i]) {
                      char btnId[32];
                      sprintf_s(btnId, "s_range%d_%d", lv, i);
                      if (ImGui::ImageButton(btnId, (ImTextureID)g_RangeTextures[i], ImVec2(64 * scale, 64 * scale),
                                             ImVec2(0, 0), ImVec2(1, 1),
                                             isSelected ? ImVec4(1, 1, 0, 0.3f) : ImVec4(0, 0, 0, 0)))
                        *pRange = i;
                    } else {
                      if (ImGui::Button(std::to_string(i).c_str(), ImVec2(64 * scale, 64 * scale)))
                        *pRange = i;
                    }
                    if (isSelected) {
                      ImGui::PopStyleVar();
                      ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                    if (i < 11)
                      ImGui::SameLine();
                  }
                  ImGui::PopStyleVar();
                  ImGui::TreePop();
                }
              }
            }

            ImGui::Separator();
            if (ImGui::Button(u8"격류/낙석 설정 적용", ImVec2(-1, 30 * scale))) {
              SaveConfig();
              SetTerrainIgnore(bTerrainIgnore);
              AddNotification(u8"격류/낙석 설정 적용 완료");
            }
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(u8"건물강화")) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 방어 건물 강화 설정 ]");
            ImGui::Checkbox(u8"건물 강화 활성화", &bDefBuilding);
            ImGui::Separator();

            auto DrawBuildingSection = [&](const char* label, int* dur, int* range, int* atk, int* sight) {
              if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 100 * scale);

                ImGui::TextUnformatted(u8"내구도:"); ImGui::NextColumn();
                ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt((std::string("##Dur") + label).c_str(), dur, 100, 500); ImGui::NextColumn();

                if (range) {
                  ImGui::TextUnformatted(u8"사거리:"); ImGui::NextColumn();
                  ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt((std::string("##Range") + label).c_str(), range, 1, 1); ImGui::NextColumn();
                }

                if (atk) {
                  ImGui::TextUnformatted(u8"공격력:"); ImGui::NextColumn();
                  ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt((std::string("##Atk") + label).c_str(), atk, 1, 5); ImGui::NextColumn();
                }

                ImGui::TextUnformatted(u8"시야:"); ImGui::NextColumn();
                ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt((std::string("##Sight") + label).c_str(), sight, 1, 1); ImGui::NextColumn();

                ImGui::Columns(1);
              }
            };

            DrawBuildingSection(u8"도시 (City)", &v_City_Dur, &v_City_Range, &v_City_Atk, &v_City_Sight);
            DrawBuildingSection(u8"관문 (Gate)", &v_Gate_Dur, &v_Gate_Range, &v_Gate_Atk, &v_Gate_Sight);
            DrawBuildingSection(u8"망루 (Tower)", &v_Tower_Dur, &v_Tower_Range, &v_Tower_Atk, &v_Tower_Sight);
            DrawBuildingSection(u8"투석기 (Catapult)", &v_WallCatapult_Dur, &v_WallCatapult_Range, &v_WallCatapult_Atk, &v_WallCatapult_Sight);
            
            if (ImGui::CollapsingHeader(u8"봉화대 (Signal Fire)", ImGuiTreeNodeFlags_DefaultOpen)) {
              ImGui::Columns(2, nullptr, false);
              ImGui::SetColumnWidth(0, 100 * scale);
              ImGui::TextUnformatted(u8"내구도:"); ImGui::NextColumn();
              ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt("##SignalDur", &v_Signal_Dur, 100, 500); ImGui::NextColumn();
              ImGui::TextUnformatted(u8"전의증가:"); ImGui::NextColumn();
              ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt("##SignalSpirit", &v_Signal_Spirit, 1, 5); ImGui::NextColumn();
              ImGui::TextUnformatted(u8"시야:"); ImGui::NextColumn();
              ImGui::SetNextItemWidth(150 * scale); ImGui::InputInt("##SignalSight", &v_Signal_Sight, 1, 1); ImGui::NextColumn();
              ImGui::Columns(1);
            }

            ImGui::Separator();
            if (ImGui::Button(u8"건물 강화 설정 적용", ImVec2(-1, 30 * scale))) {
              SaveConfig();
              DX11Base::SetDefBuildingBoost(bDefBuilding);
              AddNotification(u8"건물 강화 설정 적용 완료");
            }
            ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }
      }
      ImGui::End();
    }
  } // namespace MenuSections
} // namespace DX11Base
