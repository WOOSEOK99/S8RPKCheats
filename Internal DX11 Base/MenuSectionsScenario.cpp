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

    void DrawScenarioSection(uintptr_t p1, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.0f, 1.0f), u8"[ 시나리오 ]");

      if (ImGui::BeginTable(
              "ScenarioLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(
            "ScenarioLeft",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);
        ImGui::TableSetupColumn(
            "ScenarioRight",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);

        // 1행: 시나리오 날짜 / 세력 군주 보너스 자동 배정
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(u8"ScenarioDate");
        {
          static int s_scenarioYearEdit = 200;
          static int s_scenarioMonthEdit = 1;

          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted("[");
          ImGui::SameLine(0, 0);
          ImGui::SetNextItemWidth(40.0f * scale);
          ImGui::InputInt(u8"##scY", &s_scenarioYearEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
          const bool yearDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
          const bool yearActive = ImGui::IsItemActive();

          ImGui::SameLine(0, 0);
          ImGui::TextUnformatted("]");
          ImGui::SameLine(0, 4.0f * scale);
          ImGui::Text(u8"년");
          ImGui::SameLine(0, 10.0f * scale);
          ImGui::TextUnformatted("[");
          ImGui::SameLine(0, 0);
          ImGui::SetNextItemWidth(20.0f * scale);
          ImGui::InputInt(u8"##scM", &s_scenarioMonthEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
          const bool monthDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
          const bool monthActive = ImGui::IsItemActive();

          ImGui::SameLine(0, 0);
          ImGui::TextUnformatted("]");
          ImGui::SameLine(0, 4.0f * scale);
          ImGui::Text(u8"월");

          if (yearDeactivatedAfterEdit)
            UpdateYear((unsigned short)s_scenarioYearEdit);
          if (monthDeactivatedAfterEdit) {
            if (s_scenarioMonthEdit >= 1 && s_scenarioMonthEdit <= 12)
              UpdateMonth((uint8_t)s_scenarioMonthEdit);
            else
              DX11Base::AddLog(u8"[시나리오 날짜] 월은 1~12만 가능합니다.");
          }

          // 매 프레임 VirtualQuery 폭주 방지: 짧게 스로틀 + 한 번에 연·월 읽기
          if (!yearActive && !yearDeactivatedAfterEdit && !monthActive && !monthDeactivatedAfterEdit) {
            static unsigned long long s_lastScenarioDatePoll = 0;
            const unsigned long long now = GetTickCount64();
            if (now - s_lastScenarioDatePoll >= 250ull) {
              s_lastScenarioDatePoll = now;
              unsigned short cy = 0;
              uint8_t cm = 0;
              if (ReadScenarioDate(&cy, &cm)) {
                if ((int)cy != s_scenarioYearEdit)
                  s_scenarioYearEdit = (int)cy;
                if ((int)cm != s_scenarioMonthEdit)
                  s_scenarioMonthEdit = (int)cm;
              }
            }
          }
        }
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"세력 군주 보너스 자동 배정", &bFactionLordBonus)) {
          DX11Base::SetFactionLordBonus(bFactionLordBonus);
          NotifyFeatureToggle(u8"세력 군주 보너스 자동 배정", bFactionLordBonus);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"관작 보너스");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"황제 : 모든 능력치 +5, 병력 +5000");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"왕 : 모든 능력치 +4, 병력 +3000");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"공 : 모든 능력치 +3, 병력 +2000");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"주목 : 모든 능력치 +2, 병력 +1000");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"그냥 군주 : 모든 능력치 +1");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 지역별 왕이나 공의 차이는 없음");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 군주 관작 중 승상, 대장군은 주목과 동격");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 방랑군 두령은 보너스를 적용받지 않음");
          ImGui::EndTooltip();
        }

        // 2행: 시나리오 수정 / 데모플레이 중지
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"시나리오 수정", &bStartSetting)) {
          DX11Base::SetStartSetting(bStartSetting);
          NotifyFeatureToggle(u8"시나리오 수정", bStartSetting);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"시나리오 설정");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"체크시 새로운 시나리오 시작시 자동으로 적용이 됩니다.");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"※ 시나리오 변경(수정) 내용 ※");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"관우진군 : 관우-조홍 원수 버그 수정");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"지장집결 : 제갈량의 기술력 (연노병, 투석기까지 개발)");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"범장집결 : 전예 재야 신분으로 주인공 선택 가능");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"삼의 삼국지 : 환씨 조앙군으로 이적");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"네 군주 마지막 전쟁 : 반동탁 연합 해산, 네군주 우호도 0");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(u8"데모플레이 중지", ImVec2(-FLT_MIN, 26.0f * scale))) {
          uintptr_t gBase = DX11Base::GetGameBase();
          uintptr_t p1_ptr = gBase + 0xE0;
          if (DX11Base::g_savedHeroAddr > 0x10000 && DX11Base::IsValidPtr(p1_ptr, 8)) {
            DWORD oldP;
            if (VirtualProtect((LPVOID)p1_ptr, 8, PAGE_READWRITE, &oldP)) {
              *(uintptr_t *)p1_ptr = DX11Base::g_savedHeroAddr;
              VirtualProtect((LPVOID)p1_ptr, 8, oldP, &oldP);
              DX11Base::AddLog(u8"[데모] 데모 플레이 중지 (주인공 주소 복원 완료: %p)",
                               (void *)DX11Base::g_savedHeroAddr);
            }
          } else if (DX11Base::g_savedHeroAddr <= 0x10000) {
            DX11Base::AddLog(u8"[데모] 복원할 백업 주소가 없습니다.");
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"[사용 방법]");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"데모플레이 중 중지 버튼을 눌러 데모플레이를 중지합니다.");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"저장을 한뒤에 불러오기를 하면 정상적으로 플레이가 가능합니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), u8"[ 주의 사항 ]");
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                             u8"마우스 우측키를 눌러 일시 정지후에 중지 버튼을 누르면 까만화면으로 바뀝니다.");
          ImGui::EndTooltip();
        }

        // 3행: 도로 차단 2종
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"교지 <-> 건녕 도로 차단", &bRoadBlock)) {
          DX11Base::SetRoadBlock(bRoadBlock);
          NotifyFeatureToggle(u8"교지 <-> 건녕 도로 차단", bRoadBlock);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"교지 <-> 회계 도로 차단", &bRoadBlock2)) {
          DX11Base::SetRoadBlock2(bRoadBlock2);
          NotifyFeatureToggle(u8"교지 <-> 회계 도로 차단", bRoadBlock2);
          SaveConfig();
        }

        ImGui::EndTable();
      }

      EndSection(); // 시나리오
    }
  } // namespace MenuSections
} // namespace DX11Base
