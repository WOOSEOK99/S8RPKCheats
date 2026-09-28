#include "MenuSections.h"
#include "MenuSectionsCommon.h"
#include "Cheats.h"
#include "debug.h"
#include "Cheats/Civilian/BangmokCity.h"
#include "Cheats/Civilian/Bigcityconvert.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/Civilian/JewelSettings.h"
#include "Cheats/Civilian/DomesticsMult.h"
#include "Cheats/Civilian/NonggyeongCity.h"
#include "Cheats/Civilian/SangeopCity.h"
#include "Cheats/Civilian/Techpointcave.h"
#include "Cheats/Civilian/Techzero.h"
#include "Cheats/Officer/OfficerRosterResolve.h"
#include "Cheats/Officer/OfficerData.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Officer/AffinityDisplay.h"
#include "Cheats/Social/Fastrelationship.h"
#include "Cheats/Social/ChildEarlyAppearance.h"
#include "Cheats/Social/Infinitegift.h"
#include "Cheats/Social/Infinitetalk.h"
#include "Cheats/Social/InstantLoveCave.h"
#include "Cheats/Social/Loyaltycave.h"
#include "Cheats/Social/Resonancecave.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/SpeedHack.h"
#include "Cheats/Officer/OfficerDetail.h"
#include "Cheats/Officer/SpecialAbilityAutoAssign.h"
#include "Cheats/Officer/TraitViewerFeature.h"
#include "Cheats/Officer/TraitTextEditorWindow.h"
#include "Cheats/System/FactionTechEditor.h"
#include "Cheats/System/StartSetting.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/War/BattleMapShuffle.h"
#include "Cheats/War/Battleunitcapture.h"
#include "Cheats/War/Catapult.h"
#include "Cheats/War/Celestia.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/Dongto.h"
#include "Cheats/War/FactionLordBonus.h"
#include "Cheats/War/GovernorPrisonerDisposal.h"
#include "Cheats/War/PrisonerCaptureManagement.h"
#include "Cheats/War/Roadblock.h"
#include "Cheats/War/ReinforcementArrivalAction.h"
#include "Cheats/War/ReinforcementDefenderPlacement.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/War/Spell5HealProbe.h"
#include "Cheats/War/StratagemGaugeMax.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Cheats/War/ShortBattleCooldown.h"
#include "Cheats/War/TotalWarCycleShortening.h"
#include "Cheats/War/TroopCountCombatScaling.h"
#include "Cheats/War/Terrainignore.h"
#include "Config.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"

namespace DX11Base {
  namespace MenuSections {

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
      BeginSection();

      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 정보 ]");

      if (ImGui::BeginTable("InfoNotificationRow", 2,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"사망장수 및 등용장수 알림", &bOfficerChangeNotify)) {
          NotifyFeatureToggle(u8"사망장수 및 등용장수 알림", bOfficerChangeNotify);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"평정이 끝나고 내정으로 넘어갈 때 장수 변동을 기존 알림창으로 보여줍니다.");
          ImGui::TextUnformatted(u8"- 새로 사망 상태가 된 장수는 직전 소속 세력/도시를 함께 표시합니다.");
          ImGui::TextUnformatted(u8"- 소속 세력이 새로 생기거나 다른 세력으로 바뀐 장수는 현재 세력/도시를 표시합니다.");
          ImGui::TextUnformatted(u8"- 사망 원인이나 세력 변경 원인까지는 구분하지 않고 최종 상태를 기준으로 판정합니다.");
          ImGui::TextUnformatted(u8"- 체크 상태는 설정 파일에 저장됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"재야 장수 등장 알림", &bMonitorRonin)) {
          NotifyFeatureToggle(u8"재야 장수 등장 알림", bMonitorRonin);
          SaveConfig();
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool requestedAffinity = bAffinityDisplay;
        if (ImGui::Checkbox(u8"상성 인게임 표시", &requestedAffinity)) {
          if (SetAffinityDisplay(requestedAffinity)) {
            bAffinityDisplay = requestedAffinity;
          } else {
            bAffinityDisplay = IsAffinityDisplayApplied();
          }
          NotifyFeatureToggle(u8"상성 인게임 표시", bAffinityDisplay);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(u8"게임의 무장 목록, 무장 정보, 편집/관계 화면과 도감 상세에 상성 항목을 표시합니다.");
          ImGui::TextUnformatted(u8"미확인 무장은 상성 대신 ? 로 표시됩니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 관련 화면을 닫은 상태에서 켜거나 끄는 것을 권장합니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      EndSection(); // 무장 정보
      DX11Base::TickTraitTextEditorAutoApply();
      DX11Base::DrawTraitTextEditorWindow(scale);
      DX11Base::DrawBatchRandomTraitAssignmentWindow(scale);

    }
  } // namespace MenuSections
} // namespace DX11Base
