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

        void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");

      if (ImGui::BeginTable(
              "WarOptionsLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(
            "WarLeft",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);
        ImGui::TableSetupColumn(
            "WarRight",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);

        // 1행: 모든 무장 성향 적극 / AI 전투 개선
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
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

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"AI 전투 개선", &bAIWarImprove)) {
          const bool requested = bAIWarImprove;
          DX11Base::SetAIWarImprove(requested);
          DX11Base::NotifyFeatureToggle(u8"AI 전투 개선", bAIWarImprove);
          DX11Base::SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"AI 세력의 공격 판단, 공백지 점령, 항복권고 관련 문제를 함께 보정합니다.");
          ImGui::TextUnformatted(u8"- 공격 후보를 넓혀 기존 목표세력 외의 국가도 공격 대상으로 평가");
          ImGui::TextUnformatted(u8"- 주인공만 지나치게 공격하는 행동을 줄임");
          ImGui::TextUnformatted(u8"- 주인공이 군주가 아닐 때 소속 도시의 별도 공격 대기 조건을 제거");
          ImGui::TextUnformatted(u8"- 주인공 소속 도시도 일반 AI처럼 게임의 호전도 설정에 맞는 공격 기준을 사용");
          ImGui::TextUnformatted(u8"- 일부 군주가 빈 도시를 점령하지 않는 현상을 완화");
          ImGui::TextUnformatted(u8"- 의리가 높은 AI 군주는 항복권고를 더 잘 거부하도록 보정");
          ImGui::TextUnformatted(u8"- 항복권고 실패 후 3개월 동안 같은 대상 재권고를 막고 공격 우선도를 크게 상승");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 관련 패치 또는 hook이 예상한 원본 상태와 다르면 전체 적용을 보류합니다.");
          ImGui::EndTooltip();
        }

        // 2행: 전투맵 랜덤 / 도독 포로 직접 처분
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
          DX11Base::SetBattleMapShuffle(bBattleMapShuffle);
          NotifyFeatureToggle(u8"전투맵 랜덤(관문제외)", bBattleMapShuffle);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"매 분기 평정 기간 마다 모든 도시의 전투맵 데이터를 랜덤하게 섞습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 평정 종료 시 자동으로 원상 복구됩니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"포로 및 처형 개선", &bPrisonerCaptureManagement)) {
          const bool requested = bPrisonerCaptureManagement;
          if (!DX11Base::SetPrisonerCaptureManagement(requested))
            bPrisonerCaptureManagement =
                DX11Base::IsPrisonerCaptureManagementApplied();
          NotifyFeatureToggle(u8"포로 및 처형 개선", bPrisonerCaptureManagement);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(u8"- 총대장이 포로가 되면 같은 부대의 부장도 함께 포로 처리");
          ImGui::TextUnformatted(u8"- 고립된 도시가 함락되면 그 도시에 남은 수비측 장수 전부 포로 처리");
          ImGui::TextUnformatted(u8"- AI의 포로 석방/처형 조건에 상성·의리·관계 등을 반영");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"도독 포로 직접 처분", &bGovernorPrisonerDisposal)) {
          const bool requested = bGovernorPrisonerDisposal;
          if (!DX11Base::SetGovernorPrisonerDisposal(requested))
            bGovernorPrisonerDisposal = DX11Base::IsGovernorPrisonerDisposalApplied();
          NotifyFeatureToggle(u8"도독 포로 직접 처분", bGovernorPrisonerDisposal);
          SaveConfig();
        }
        const bool governorPrisonerHovered = ImGui::IsItemHovered();

        ImGui::Indent(18.0f * scale);
        if (ImGui::Checkbox(u8"특권 1개 소비", &bGovernorPrisonerConsumePrivilege)) {
          SaveConfig();
        }
        const bool governorPrivilegeHovered = ImGui::IsItemHovered();
        ImGui::Unindent(18.0f * scale);

        if (governorPrisonerHovered) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"도독이 통치권 내 도시의 정규군 전투에서 승리했을 때 포로를 직접 처분할지 선택할 수 있게 합니다.");
          ImGui::TextUnformatted(u8"- 조건을 만족하면 포로 처분 전에 예/아니오 질문이 표시됩니다.");
          ImGui::TextUnformatted(u8"- 아니오를 선택하면 원래 게임의 포로 처분 흐름을 그대로 따릅니다.");
          ImGui::EndTooltip();
        }

        if (governorPrivilegeHovered) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(u8"OFF: 예를 선택해도 특권을 소비하지 않습니다.");
          ImGui::TextUnformatted(u8"ON: 예를 선택하면 특권 1개를 소비한 뒤 직접 처분합니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 특권이 0개이면 특권 소비 모드에서 직접 처분할 수 없습니다.");
          ImGui::EndTooltip();
        }

        // 3행: 원군 도착 턴 즉시 행동 / 수비측 원군 총대장 근처 배치
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"원군 도착 턴 즉시 행동", &bReinforcementArrivalAction)) {
          const bool requested = bReinforcementArrivalAction;
          if (!DX11Base::SetReinforcementArrivalAction(requested))
            bReinforcementArrivalAction = DX11Base::IsReinforcementArrivalActionApplied();
          NotifyFeatureToggle(u8"원군 도착 턴 즉시 행동", bReinforcementArrivalAction);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"전투에 도착한 원군이 도착한 그 턴부터 바로 행동할 수 있게 합니다.");
          ImGui::TextUnformatted(u8"- 원군 도착 처리를 명령 처리보다 먼저 실행하도록 순서를 변경합니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 전투 턴 전환 중에는 이 옵션을 켜거나 끄지 마세요.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"수비측 원군 총대장 근처 배치", &bReinforcementDefenderPlacement)) {
          const bool requested = bReinforcementDefenderPlacement;
          if (!DX11Base::SetReinforcementDefenderPlacement(requested))
            bReinforcementDefenderPlacement = DX11Base::IsReinforcementDefenderPlacementApplied();
          NotifyFeatureToggle(u8"수비측 원군 총대장 근처 배치", bReinforcementDefenderPlacement);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"수비측 원군이 도착할 때 총대장과 가까운 이동 가능한 빈 타일에 배치합니다.");
          ImGui::TextUnformatted(u8"- 총대장 기준 최대 5칸 범위에서 가까운 순서로 배치 위치를 탐색합니다.");
          ImGui::TextUnformatted(u8"- 점유된 타일, 사용 불가 지형, 통행 불가 타일은 제외합니다.");
          ImGui::TextUnformatted(u8"- 5칸 범위 안에 적절한 위치가 없으면 게임의 원래 배치 방식을 사용합니다.");
          ImGui::TextUnformatted(u8"- 총대장을 찾지 못하거나 판별이 애매해도 게임의 원래 배치 방식을 사용합니다.");
          ImGui::TextUnformatted(u8"- 공격측 원군의 배치 위치는 변경하지 않습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 원군 도착 순간 일시적인 화면 끊김이 발생할 수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 다수의 원군이 동시에 도착할 경우 더 눈에 띌 수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 전투 턴 전환 중에는 이 옵션을 켜거나 끄지 마세요.");
          ImGui::EndTooltip();
        }

        // 4행: 병력수 공방 반영
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"병력수 공방 반영", &bTroopCountCombatScaling)) {
          const bool requested = bTroopCountCombatScaling;
          if (!DX11Base::SetTroopCountCombatScaling(requested))
            bTroopCountCombatScaling = DX11Base::IsTroopCountCombatScalingApplied();
          NotifyFeatureToggle(u8"병력수 공방 반영", bTroopCountCombatScaling);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
              u8"오리지널 시절의 방식처럼 병력 수가 부대 공격/방어 계산에 더 직접적으로 반영되게 합니다.");
          ImGui::TextUnformatted(
              u8"- 공격/방어 공용 계산 함수에서 게임에 남아 있는 선형 병력 환산 경로를 사용합니다.");
          ImGui::TextUnformatted(
              u8"- 병력이 많을수록 공방 계산에서 더 유리하고, 병력이 적을수록 상대적으로 불리해지는 방향입니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      // 단기접전 대기일수는 체크 상태와 수정값이 하나의 설정임을 명확히 보이도록 묶습니다.
      ImGui::PushStyleColor(
          ImGuiCol_Border,
          ImVec4(0.62f, 0.54f, 0.28f, 0.90f));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * scale);
      ImGui::BeginChild(
          "##ShortBattleCooldownGroup",
          ImVec2(0.0f, 42.0f * scale),
          true,
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

      bool shortCooldownHovered = false;
      if (ImGui::Checkbox(u8"단기접전 대기일수 변경", &bShortBattleCooldownEnabled)) {
        const bool requested = bShortBattleCooldownEnabled;
        if (!DX11Base::SetShortBattleCooldown(requested, iShortBattleCooldownDays))
          bShortBattleCooldownEnabled = DX11Base::IsShortBattleCooldownApplied();
        NotifyFeatureToggle(u8"단기접전 대기일수 변경", bShortBattleCooldownEnabled);
        SaveConfig();
      }
      shortCooldownHovered |= ImGui::IsItemHovered();

      ImGui::SameLine(0.0f, 14.0f * scale);
      ImGui::SetNextItemWidth(55.0f * scale);
      const int previousShortCooldownDays = iShortBattleCooldownDays;
      if (ImGui::InputInt("##ShortBattleCooldownDays", &iShortBattleCooldownDays, 0, 0,
                          ImGuiInputTextFlags_CharsDecimal)) {
        if (bShortBattleCooldownEnabled &&
            !DX11Base::SetShortBattleCooldown(true, iShortBattleCooldownDays)) {
          iShortBattleCooldownDays = previousShortCooldownDays;
        }
        SaveConfig();
      }
      shortCooldownHovered |= ImGui::IsItemHovered();

      ImGui::SameLine();
      ImGui::TextDisabled(u8"기본값 : 10");

      if (shortCooldownHovered) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"단기접전(일기토)이 다시 발생하기까지의 대기 날짜를 설정합니다.");
        ImGui::TextUnformatted(u8"- 예: 3으로 설정하면 단기접전 발생 후 3일 동안은 다시 단기접전이 발생하지 않습니다.");
        ImGui::TextUnformatted(u8"- 장수별 개별 대기시간이 아니라 전쟁에 출전한 부대 전체에 공통으로 적용됩니다.");
        ImGui::TextUnformatted(u8"- 기본값은 10일입니다.");
        ImGui::TextUnformatted(u8"- 체크 상태와 적용값은 설정 파일에 저장되어 다음 실행 시 다시 불러옵니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 체크 해제 시 활성화 전에 읽어둔 원래 값으로 복구합니다.");
        ImGui::EndTooltip();
      }

      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();

      ImGui::Spacing();

      if (ImGui::Button(u8"전투 환경 및 조건 설정", ImVec2(150 * scale, 30 * scale))) {
        bShowBattleEnvWin = !bShowBattleEnvWin;
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"클릭하여 날씨, 일자, 지형, 여울(공성전) 등의 상세 설정을 엽니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine();
      if (ImGui::Button(u8"전법 편집", ImVec2(110.0f * scale, 30.0f * scale))) {
        bShowTacticsEditWin = !bShowTacticsEditWin;
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 책략 ]");

      if (ImGui::Checkbox(u8"공격측 책략 게이지 시작 최대", &bMaxAttackStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxAttackStratagemGauge = !bMaxAttackStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"공격측 책략 게이지 최대", bMaxAttackStratagemGauge);
          SaveConfig();
        }
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"수비측 책략 게이지 시작 최대", &bMaxDefenseStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxDefenseStratagemGauge = !bMaxDefenseStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"수비측 책략 게이지 최대", bMaxDefenseStratagemGauge);
          SaveConfig();
        }
      }

      if (ImGui::Checkbox(u8"5번 책략 활성화", &bStratagemFiveEnabled)) {
        const bool enable = bStratagemFiveEnabled;
        const bool ok = DX11Base::SetStratagemFiveFeature(enable);
        if (!ok) {
          bStratagemFiveEnabled = !enable;
          AddNotification(enable ? u8"5번 책략 활성화 실패 - 로그 확인"
                                 : u8"5번 책략 해제 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"5번 책략 활성화", bStratagemFiveEnabled);
          SaveConfig();
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"전투 시작 전에 활성화하면 5번째 책략을 사용할 수 있습니다.");
        ImGui::TextUnformatted(u8"현재는 주인공 부대의 총대장만 사용할 수 있으며, AI 총대장은 사용할 수 없습니다.");
        ImGui::TextUnformatted(u8"전투 중에 체크하거나 해제한 경우에는 현재 전투에는 적용되지 않습니다.");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"변경 내용은 전투 종료 후 다음 전투부터 적용됩니다.");
        ImGui::TextDisabled(u8"※ 전투 중 저장한 세이브 파일을 불러온 경우에도 현재 전투에는 적용하지 않습니다.");
        ImGui::EndTooltip();
      }

      static DX11Base::Spell5CustomSettings s_stratagem5Edit{};
      ImGui::SameLine();
      if (ImGui::Button(u8"5번 책략 설정")) {
        s_stratagem5Edit = DX11Base::GetSpell5CustomSettings();
        ImGui::OpenPopup(u8"5번 책략 설정###Stratagem5SettingsPopup");
      }

      ImGui::SetNextWindowSize(ImVec2(590.0f * scale, 570.0f * scale),
                               ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal(
              u8"5번 책략 설정###Stratagem5SettingsPopup",
              nullptr,
              ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f),
                           u8"[ 5번 책략 사용자 설정 ]");
        ImGui::Separator();
        ImGui::TextDisabled(u8"책략 ID 5/5/5와 사용횟수 1회는 고정합니다.");

        const char *targetNames[] = {u8"아군", u8"적군", u8"피아불문"};
        int targetIndex = s_stratagem5Edit.target - 1;
        if (targetIndex < 0 || targetIndex > 2)
          targetIndex = 0;
        ImGui::SetNextItemWidth(180.0f * scale);
        if (ImGui::Combo(u8"대상", &targetIndex,
                         targetNames, IM_ARRAYSIZE(targetNames))) {
          s_stratagem5Edit.target = targetIndex + 1;
        }

        static const int effectCodes[] = {0, 3, 10, 11, 12};
        const char *effectNames[] = {
            u8"없음", u8"상태이상", u8"사기 증감", u8"직접 데미지", u8"화계"};

        auto effectIndexFromCode = [&](int code) {
          for (int i = 0; i < IM_ARRAYSIZE(effectCodes); ++i)
            if (effectCodes[i] == code)
              return i;
          return 0;
        };

        auto drawEffectEditor = [&](const char *title, const char *idPrefix,
                                    int &effect, int &power, int &duration) {
          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f), "%s", title);

          int effectIndex = effectIndexFromCode(effect);
          char comboId[64] = {};
          sprintf_s(comboId, "%s_effect", idPrefix);
          ImGui::SetNextItemWidth(180.0f * scale);
          if (ImGui::Combo(comboId, &effectIndex,
                           effectNames, IM_ARRAYSIZE(effectNames))) {
            effect = effectCodes[effectIndex];
            if (effect == 0) {
              power = 0;
              duration = 0;
            } else if (effect == 3) {
              if (power < 1 || power > 3)
                power = 1;
              if (duration < 0)
                duration = 0;
            } else if (effect == 12) {
              if (power < 0 || power > 100)
                power = 100;
              duration = 0;
            } else {
              duration = 0;
            }
          }

          if (effect == 3) {
            const char *statusNames[] = {u8"저지", u8"혼란", u8"공황"};
            int statusIndex = power - 1;
            if (statusIndex < 0 || statusIndex > 2)
              statusIndex = 0;

            char statusId[64] = {};
            sprintf_s(statusId, "%s_status", idPrefix);
            ImGui::SetNextItemWidth(180.0f * scale);
            if (ImGui::Combo(statusId, &statusIndex,
                             statusNames, IM_ARRAYSIZE(statusNames))) {
              power = statusIndex + 1;
            }

            char durationId[64] = {};
            sprintf_s(durationId, "%s_duration", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(durationId, &duration, 1, 3)) {
              if (duration < 0) duration = 0;
              if (duration > 30) duration = 30;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"지속일");
          } else if (effect == 10) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_morale", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 5, 10)) {
              if (power < -100) power = -100;
              if (power > 100) power = 100;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"사기 증감");
          } else if (effect == 11) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_damage", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 50, 100)) {
              if (power < 0) power = 0;
              if (power > 10000) power = 10000;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"위력");
          } else if (effect == 12) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_fire", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 5, 10)) {
              if (power < 0) power = 0;
              if (power > 100) power = 100;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"발동 확률(%)");
          } else {
            ImGui::TextDisabled(u8"추가 설정 없음");
          }
        };

        drawEffectEditor(u8"효과 1", "##S5E1",
                         s_stratagem5Edit.effect1,
                         s_stratagem5Edit.power1,
                         s_stratagem5Edit.duration1);

        drawEffectEditor(u8"효과 2", "##S5E2",
                         s_stratagem5Edit.effect2,
                         s_stratagem5Edit.power2,
                         s_stratagem5Edit.duration2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(120.0f * scale);
        if (ImGui::InputInt(u8"범위##S5Range",
                            &s_stratagem5Edit.range, 1, 1)) {
          if (s_stratagem5Edit.range < 1) s_stratagem5Edit.range = 1;
          if (s_stratagem5Edit.range > 100) s_stratagem5Edit.range = 100;
        }

        ImGui::SetNextItemWidth(140.0f * scale);
        if (ImGui::InputInt(u8"추가 병력 회복##S5Heal",
                            &s_stratagem5Edit.healAmount, 100, 500)) {
          if (s_stratagem5Edit.healAmount < 0)
            s_stratagem5Edit.healAmount = 0;
          if (s_stratagem5Edit.healAmount > 65535)
            s_stratagem5Edit.healAmount = 65535;
        }
        ImGui::TextDisabled(
            u8"※ 추가 병력 회복은 우리가 별도로 넣은 기능이며, 효과1이 '사기 증가'일 때만 적용됩니다.");
        ImGui::TextDisabled(
            u8"※ 첫 번째 효과가 직접 데미지/화계 계통이면 원본 게임의 지형 판정 영향을 받을 수 있습니다.");

        ImGui::Spacing();
        if (ImGui::Button(u8"현재 기본값 복원",
                          ImVec2(150.0f * scale, 30.0f * scale))) {
          s_stratagem5Edit = DX11Base::Spell5CustomSettings{};
        }

        ImGui::Separator();

        if (ImGui::Button(u8"적용 및 저장",
                          ImVec2(180.0f * scale, 34.0f * scale))) {
          DX11Base::SetSpell5CustomSettings(s_stratagem5Edit);
          s_stratagem5Edit = DX11Base::GetSpell5CustomSettings();
          SaveConfig();
          AddNotification(u8"5번 책략 설정 적용 완료");
          ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(u8"취소",
                          ImVec2(100.0f * scale, 34.0f * scale))) {
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      EndSection(); // 전쟁
    }
  } // namespace MenuSections
} // namespace DX11Base
