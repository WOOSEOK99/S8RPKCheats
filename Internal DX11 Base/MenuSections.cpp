#define DrawWarSection DrawWarSectionLegacy
#include "MenuSectionsBase.inc"
#undef DrawWarSection

namespace DX11Base {
  namespace MenuSections {
    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");

      if (ImGui::Checkbox(u8"모든 무장 성향 적극", &bAllAggressive)) {
        DX11Base::NotifyFeatureToggle(u8"모든 무장 성향 적극 자동 적용", bAllAggressive);
        DX11Base::SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(
            ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
            u8"체크 시, 게임 진입(주인공 포착) 순간 모든 유효 무장의 전략 성향이 '적극'으로 자동 적용됩니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 로드할 때 딱 한 번 적용되며 계속 유지해야 다음 플레이 시에도 반영됩니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(160.0f * scale);

      if (ImGui::Checkbox(u8"AI 전투 개선", &bAIWarImprove)) {
        const bool requested = bAIWarImprove;
        DX11Base::SetAIWarImprove(requested);
        DX11Base::NotifyFeatureToggle(u8"AI 전투 개선", bAIWarImprove);
        DX11Base::SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"AI 세력의 전쟁 행동 관련 3개 분기를 함께 조정합니다.");
        ImGui::TextUnformatted(u8"- 한 세력의 복수 공격 분기");
        ImGui::TextUnformatted(u8"- 주인공 대상 호전성 증가 분기 제거");
        ImGui::TextUnformatted(u8"- 일부 군주의 공백지 점령 제한 플래그 무력화");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 세 주소 중 하나라도 예상 바이트와 다르면 전체 패치를 적용하지 않습니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
        DX11Base::SetBattleMapShuffle(bBattleMapShuffle);
        NotifyFeatureToggle(u8"전투맵 랜덤(관문제외)", bBattleMapShuffle);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"매 분기 평정 기간 마다 모든 도시의 전투맵 데이터를 랜덤하게 섞습니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 평정 종료 시 자동으로 원상 복구됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Button(u8"전투 환경 및 조건 설정", ImVec2(150 * scale, 30 * scale))) {
        bShowBattleEnvWin = !bShowBattleEnvWin;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"모든 무장 일괄 편집", ImVec2(-1, 30 * scale))) {
        bShowBatchOfficerEditWin = !bShowBatchOfficerEditWin;
      }

      if (ImGui::Button(u8"세력별 기술력 편집", ImVec2(-1, 30 * scale))) {
        bShowFactionTechEditor = !bShowFactionTechEditor;
      }
      ::DX11Base::DrawBatchOfficerEditWindow(scale);
      ::DX11Base::DrawFactionTechEditor(scale);

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"클릭하여 날씨, 일자, 지형, 여울(공성전) 등의 상세 설정을 엽니다.");
        ImGui::EndTooltip();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 전법 및 건물 ]");
      ImGui::SameLine();
      if (ImGui::Button(u8"수정", ImVec2(100.0f * scale, 25.0f * scale))) {
        bShowTacticsEditWin = !bShowTacticsEditWin;
      }

      if (ImGui::Checkbox(u8"치료", &bSelfHeal)) {
        DX11Base::SetSelfHeal(bSelfHeal);
        NotifyFeatureToggle(u8"치료", bSelfHeal);
        SaveConfig();
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"동토", &bDongto)) {
        DX11Base::SetDongto(bDongto);
        NotifyFeatureToggle(u8"동토", bDongto);
        SaveConfig();
      }
      ImGui::SameLine();

      if (ImGui::Checkbox(u8"천계", &bCelestial)) {
        DX11Base::SetCelestialMod(bCelestial);
        NotifyFeatureToggle(u8"천계", bCelestial);
        SaveConfig();
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"투석", &bCatapult)) {
        DX11Base::SetCatapultCheat(bCatapult);
        NotifyFeatureToggle(u8"투석", bCatapult);
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"격류/낙석", &bTerrainIgnore)) {
        DX11Base::SetTerrainIgnore(bTerrainIgnore);
        NotifyFeatureToggle(u8"격류/낙석", bTerrainIgnore);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"방어 건물 강화", &bDefBuilding)) {
        DX11Base::SetDefBuildingBoost(bDefBuilding);
        NotifyFeatureToggle(u8"방어 건물 강화", bDefBuilding);
        SaveConfig();
      }

      EndSection(); // 전쟁
    }
  } // namespace MenuSections
} // namespace DX11Base
