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
#include "Cheats/War/IsolatedTerritoryMovementFeature.h"
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

    void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase,
                     float scale) {
      bool useP1 = (p1 != 0 && offset < 0x5000);
      uintptr_t targetAddr = (useP1) ? p1 : gameBase;
      unsigned int current = 0;
      bool valid = false;

      if (targetAddr > 0x10000) {
        valid = true;
        if (size == 1)
          current = *(unsigned char *)(targetAddr + offset);
        else if (size == 2)
          current = *(unsigned short *)(targetAddr + offset);
        else
          current = *(unsigned int *)(targetAddr + offset);
      }

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", label);
      ImGui::SameLine(100.0f * scale);

      ImGui::PushID(label);
      if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)--;
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::SameLine();

      ImGui::SetNextItemWidth(70 * scale);
      ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);

      bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
      if (justFinished) {
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }

      if (!ImGui::IsItemActive() && !justFinished && targetAddr > 0x10000) {
        if (size == 1)
          *inputVal = (int)(*(unsigned char *)(targetAddr + offset));
        else if (size == 2)
          *inputVal = (int)(*(unsigned short *)(targetAddr + offset));
        else
          *inputVal = (int)(*(unsigned int *)(targetAddr + offset));
      }
      ImGui::SameLine();

      if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)++;
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }
      ImGui::PopID();
    }

    void DrawCouncilSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 ]");
        DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP, p1, gameBase, scale);
        DrawStatRow(u8"공적", 0x100, 2, &v_Merit, p1, gameBase, scale);
        DrawStatRow(u8"특권", 0xEA, 1, &v_Priv, 0, gameBase, scale);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 기존 두 체크박스의 실행 코드는 변경하지 않고 배치만 정리한다.
        if (ImGui::Checkbox(u8"매 평정 새로운 전기 발생", &bInfTengi)) {
          NotifyFeatureToggle(u8"매 평정 새로운 전기 발생", bInfTengi);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"매 평정 마다 새로운 전기가 발생합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(0.f, 24.f * scale);
        if (ImGui::Checkbox(u8"결전 발생 주기 단축", &bTotalWarCycleShortening)) {
          const bool requested = bTotalWarCycleShortening;
          if (!DX11Base::SetTotalWarCycleShortening(requested))
            bTotalWarCycleShortening = DX11Base::IsTotalWarCycleShorteningApplied();
          NotifyFeatureToggle(u8"결전 발생 주기 단축", bTotalWarCycleShortening);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"결전 발생 후 다음 결전의 재발생 대기 주기를 1년으로 단축합니다.");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                             u8"다른 결전 발생 조건은 그대로 유지됩니다.");
          ImGui::EndTooltip();
        }

        // 외부 version.dll의 이름 및 슬롯 매핑을 검증함. 기간만 실험 적용하며 허용 필터는 미구현.
        static const char *const tengiNames[kTengiListEventCount] = {
          u8"결전", u8"이민족습격", u8"악적발호", u8"의심암귀", u8"민심혹란",
          u8"붕벽", u8"여세", u8"피폐", u8"권위고양", u8"보장각성",
          u8"기장각성", u8"궁장각성", u8"병격난무", u8"전승기", u8"중지성성"
        };
        static bool draftAllowed[kTengiListEventCount] = {};
        static int draftMonths[kTengiListEventCount] = {};

        ImGui::Spacing();
        if (ImGui::Button(u8"전기 목록 관리##tengiList", ImVec2(160.f * scale, 0.f))) {
          for (int i = 0; i < kTengiListEventCount; ++i) {
            draftAllowed[i] = g_tengiListAllowed[i];
            draftMonths[i] = g_tengiListDurationMonths[i];
          }
          ImGui::OpenPopup(u8"전기 목록 관리##popup");
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(u8"전기별 기간 메모리 수정 실험. 발생 허용/차단은 아직 적용되지 않습니다.");
        }

        ImGui::SetNextWindowSize(ImVec2(520.f * scale, 510.f * scale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(u8"전기 목록 관리##popup", nullptr,
                                   ImGuiWindowFlags_NoSavedSettings)) {
          ImGui::TextWrapped(u8"전기별 허용 여부와 기간 설정 (0개월 = 게임 기본값)");
          ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                             u8"※ 기간 변경은 게임 메모리에 실험 적용됩니다. 발생 허용 체크는 아직 게임 전기 선택에 연결되지 않았습니다.");
          ImGui::Spacing();

          const ImGuiTableFlags listFlags =
              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
          if (ImGui::BeginTable("##TengiListTable", 3, listFlags,
                                ImVec2(0.f, 355.f * scale))) {
            ImGui::TableSetupColumn(u8"전기 이름", ImGuiTableColumnFlags_WidthStretch, 1.8f);
            ImGui::TableSetupColumn(u8"발생 허용", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn(u8"기간(개월)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < kTengiListEventCount; ++i) {
              ImGui::PushID(i);
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::TextUnformatted(tengiNames[i]);
              ImGui::TableSetColumnIndex(1);
              ImGui::Checkbox("##allow", &draftAllowed[i]);
              ImGui::TableSetColumnIndex(2);
              ImGui::SetNextItemWidth(-1.f);
              if (ImGui::InputInt("##months", &draftMonths[i], 0, 0)) {
                if (draftMonths[i] < 0) draftMonths[i] = 0;
                if (draftMonths[i] > 120) draftMonths[i] = 120;
              }
              ImGui::PopID();
            }
            ImGui::EndTable();
          }

          if (ImGui::Button(u8"설정 저장", ImVec2(115.f * scale, 0.f))) {
            for (int i = 0; i < kTengiListEventCount; ++i) {
              g_tengiListAllowed[i] = draftAllowed[i];
              g_tengiListDurationMonths[i] = draftMonths[i];
            }
            SaveConfig();
            TickTengiListDurations(true);
            DX11Base::AddLog(u8"[전기 목록] 설정 저장 완료. 기간 적용 실험 (발생 허용 필터 미구현)");
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine(0.f, 10.f * scale);
          if (ImGui::Button(u8"닫기##tengiList", ImVec2(90.f * scale, 0.f)))
            ImGui::CloseCurrentPopup();
          ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox(u8"만병 습득 조건 해제", &bSkillCondition)) {
          DX11Base::ApplySkillCondition(bSkillCondition);
          NotifyFeatureToggle(u8"만병 습득 조건 해제", bSkillCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"만병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"상병 습득 조건 해제", &bSangbyeongCondition)) {
          DX11Base::ApplySangbyeongCondition(bSangbyeongCondition);
          NotifyFeatureToggle(u8"상병 습득 조건 해제", bSangbyeongCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"상병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"유목기병 습득 조건 해제", &bYumokCondition)) {
          DX11Base::ApplyYumokCondition(bYumokCondition);
          NotifyFeatureToggle(u8"유목기병 습득 조건 해제", bYumokCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"유목기병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"단절 영토 무장 이동 제한", &bIsolatedTerritoryMovement)) {
          const bool requested = bIsolatedTerritoryMovement;
          if (!DX11Base::SetIsolatedTerritoryMovementFeature(requested))
            bIsolatedTerritoryMovement = DX11Base::IsIsolatedTerritoryMovementFeatureApplied();
          NotifyFeatureToggle(u8"단절 영토 무장 이동 제한", bIsolatedTerritoryMovement);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"서로 연결되지 않은 같은 세력 영토 사이의 무장 이동을 제한합니다.");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                             u8"수송과 배정은 기존 동작을 유지합니다.");
          ImGui::EndTooltip();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox(u8"능력치 한계돌파", &bAutoStatUp99)) {
          NotifyFeatureToggle(u8"능력치 한계돌파", bAutoStatUp99);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"평정 기간 진입 시, 모든 장수의 능력치 중 99인 항목을 100으로 올립니다.");
          ImGui::EndTooltip();
        }

        {
          static uintptr_t s_lastHookAddr = 0;
          static uintptr_t s_lastCaptAddr = 0;
          uintptr_t hookAddr = DX11Base::GetTengiHookAddr();
          uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();

          if (hookAddr != s_lastHookAddr) {
            if (hookAddr != 0) {
              DX11Base::AddLog(u8"[전기] 훅 지점 발견: %p (+0x%llX)", (void *)hookAddr,
                               (unsigned long long)DX11Base::GetTengiHookOffset());
            }
            s_lastHookAddr = hookAddr;
          }

          if (captAddr != s_lastCaptAddr) {
            if (captAddr != 0) {
              DX11Base::AddLog(u8"[전기] 캡처 주소 확보: %p", (void *)captAddr);
            }
            s_lastCaptAddr = captAddr;
          }
        }
        EndSection();
      }
    }
  } // namespace MenuSections
} // namespace DX11Base
