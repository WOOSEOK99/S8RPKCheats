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
  extern bool g_initThreadRunning;
  extern bool marriageApplied;
  extern void ToggleMarriageCondition();
  extern void SetMarriageCondition(bool enable);
} // namespace DX11Base

namespace DX11Base {
  namespace MenuSections {

    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(
          ImVec4(0.82f, 0.7f, 0.55f, 1.0f),
          u8"[ 교류 ]");

      bool wasRunning = ::DX11Base::g_initThreadRunning;

      auto DrawLoveCheckbox = [&](const char *label, bool *var, LoveMode mode) {
        if (wasRunning)
          ImGui::BeginDisabled();

        if (ImGui::Checkbox(label, var)) {
          if (*var) {
            if (mode == LoveMode::Normal)
              bHateCave = false;
            else
              bLoveCave = false;
            DX11Base::SetInstantLoveCave(false);
          }

          DX11Base::SetInstantLoveCave(*var, mode);
          NotifyFeatureToggle(label, *var);
          SaveConfig();
        }

        if (wasRunning)
          ImGui::EndDisabled();
      };

      if (ImGui::BeginTable(
              "SocialInteractionLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(
            "SocialUnlimited",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);
        ImGui::TableSetupColumn(
            "SocialOthers",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);

        // 1행: 선물 기증 무제한 / 즉시 경애
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"선물 기증 무제한", &bInfiniteGift)) {
          DX11Base::SetInfiniteGift(bInfiniteGift);
          NotifyFeatureToggle(u8"선물 기증 무제한", bInfiniteGift);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"즉시 경애 맺기", &bFastRelationship)) {
          DX11Base::SetFastRelationship(bFastRelationship);
          NotifyFeatureToggle(u8"즉시 경애 맺기", bFastRelationship);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"교류로 관계가 갱신되는 대상의 친밀도를 100으로 처리해 즉시 경애 상태로 진입시킵니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 기존 즉시 경애 패치 대신 CT ID 321 방식을 사용합니다.");
          ImGui::EndTooltip();
        }

        // 2행: 담화 무제한 / 혐오·상극 무시
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"담화 실행 무제한", &bInfiniteTalk)) {
          DX11Base::SetInfiniteTalk(bInfiniteTalk);
          NotifyFeatureToggle(u8"담화 실행 무제한", bInfiniteTalk);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        DrawLoveCheckbox(
            u8"혐오/상극 무시 경애",
            &bHateCave,
            LoveMode::HateIgnore);

        // 3행: 대련 무제한 / 공명
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"대련 실행 무제한", &bInfiniteDuel)) {
          DX11Base::SetInfiniteDuel(bInfiniteDuel);
          NotifyFeatureToggle(u8"대련 실행 무제한", bInfiniteDuel);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        {
          const bool resonanceBusy =
              ::DX11Base::g_resonanceThreadRunning.load();

          if (resonanceBusy)
            ImGui::BeginDisabled();

          if (ImGui::Checkbox(u8"무조건 공명 발생", &bResonance)) {
            DX11Base::SetInstantResonance(bResonance);
            NotifyFeatureToggle(u8"무조건 공명 발생", bResonance);
            SaveConfig();
          }

          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(
                ImVec4(1, 1, 0, 1),
                u8"무조건 공명갯수 4개로 되고,");
            ImGui::TextColored(
                ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                u8"다음번 담화때 상생 발생함.");
            ImGui::EndTooltip();
          }

          if (resonanceBusy)
            ImGui::EndDisabled();
        }

        // 4행: 토론 무제한 / 충성도
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"토론 실행 무제한", &bInfiniteDebate)) {
          DX11Base::SetInfiniteDebate(bInfiniteDebate);
          NotifyFeatureToggle(u8"토론 실행 무제한", bInfiniteDebate);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (::DX11Base::g_loyaltyThreadRunning.load())
          ImGui::BeginDisabled();

        if (ImGui::Checkbox(u8"무장 충성도 100", &bLoyalty)) {
          DX11Base::SetInstantLoyalty(bLoyalty);
          NotifyFeatureToggle(u8"무장 충성도 100", bLoyalty);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"교류 클릭시 목록에 있는 모든 무장의 충성이 100이 됨.");
          ImGui::EndTooltip();
        }

        if (::DX11Base::g_loyaltyThreadRunning.load())
          ImGui::EndDisabled();

        // 5행: 중개 무제한 / AI 친밀도 가속
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"중개 무제한", &bInfiniteMediation)) {
          if (bInfiniteMediation)
            ::DX11Base::TickInfiniteMediation();
          NotifyFeatureToggle(u8"중개 무제한", bInfiniteMediation);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(
                u8"AI 친밀도 가속",
                &g_autoAffinityGrowthEnabled)) {
          ResetAutoAffinityGrowthState();
          NotifyFeatureToggle(
              u8"AI 친밀도 가속",
              g_autoAffinityGrowthEnabled);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"매 분기 평정월(1·4·7·10월) 시작 시 AI 무장끼리 친밀도를 추가 상승시킵니다.");
          ImGui::TextUnformatted(
              u8"상성과 흥미·중시의 일치 정도를 반영하며 최대 친밀도는 100입니다.");
          ImGui::TextUnformatted(
              u8"친밀도만 가속하며 부부·의형제·상생 관계를 직접 생성하지 않습니다.");
          ImGui::EndTooltip();
        }

        // 6행: 연회 무제한 / 자녀 관리
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"연회 무제한", &bInfiniteBanquet)) {
          if (bInfiniteBanquet)
            ::DX11Base::TickInfiniteBanquet();
          NotifyFeatureToggle(u8"연회 무제한", bInfiniteBanquet);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(
                u8"자녀 관리",
                ImVec2(120.0f * scale, 0))) {
          bShowChildManagerWin = true;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"감지된 자녀 목록을 열어 자녀별로 임관 시점을 설정합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 새로 태어난 자녀도 이후 자녀 처리 시 자동으로 목록에 추가됩니다.");
          ImGui::EndTooltip();
        }

        // 7행: 결혼 무제한 / 빈칸
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool tempMarriage = ::DX11Base::marriageApplied;
        if (ImGui::Checkbox(u8"결혼 무제한", &tempMarriage)) {
          ::DX11Base::SetMarriageCondition(tempMarriage);
          NotifyFeatureToggle(u8"결혼 무제한", tempMarriage);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"배우자가 있어도 무조건 결혼이 됩니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 상대가 경애 상태일 때 기존 배우자 제한을 우회합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 대신 타 세력의 경우 등용은 안되네요.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      EndSection(); // 교류
    }
  } // namespace MenuSections
} // namespace DX11Base
