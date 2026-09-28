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
  namespace MenuSections {

    void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase,
                     float scale) {
      // 1. 현재 값 미리 읽기
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

      // 2. UI 그리기
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", label);

      // 레이블 이후 정렬 위치 고정 (테이블 없이 SameLine으로 깔끔하게 처리)
      ImGui::SameLine(100.0f * scale);

      ImGui::PushID(label);
      // 1. [-] 버튼
      if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)--;
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::SameLine();

      // 2. 직접 입력 가능한 수치 박스 (InputInt)
      ImGui::SetNextItemWidth(70 * scale);
      // EnterReturnsTrue를 제거하여 자판 입력 시 즉시 변수에 반영되도록 함 (숫자만 입력 가능하도록 플래그 추가)
      ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);

      // 포커스를 잃거나 Enter를 쳤을 때(Deactivated) 수정한 내역이 있다면 저장
      bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
      if (justFinished) {
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }

      // [중요] 사용자가 입력 중(포커스 상태)이거나, 막 입력이 끝난 프레임에는 메모리 값을 덮어씌우지 않음
      if (!ImGui::IsItemActive() && !justFinished && targetAddr > 0x10000) {
        if (size == 1)
          *inputVal = (int)(*(unsigned char *)(targetAddr + offset));
        else if (size == 2)
          *inputVal = (int)(*(unsigned short *)(targetAddr + offset));
        else
          *inputVal = (int)(*(unsigned int *)(targetAddr + offset));
      }
      ImGui::SameLine();

      // 3. [+] 버튼
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

        // DrawStatMini(u8"전략P", &v_SP, 0xED, 1, p1, 60, scale);
        // ImGui::SameLine(110 * scale);
        // DrawStatMini(u8"공적", &v_Merit, 0x100, 2, p1, 60, scale);
        // ImGui::SameLine(210 * scale);
        // DrawStatMini(u8"특권", &v_Priv, 0xEA, 1, p1, 60, scale);

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"매 평정 새로운 전기 발생", &bInfTengi)) {
          NotifyFeatureToggle(u8"매 평정 새로운 전기 발생", bInfTengi);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"매 평정 마다 새로운 전기가 발생합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"중지 성성 취소", &bCancelCastleEvent)) {
          NotifyFeatureToggle(u8"중지 성성 취소", bCancelCastleEvent);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"중지 성성이 발생하면 즉시 취소합니다.");
          ImGui::EndTooltip();
        }

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

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Button(u8"전기발생 즉시 취소", ImVec2(120, 26))) {
          // 일회용 버튼: 현재 캡처된 주소가 있으면 값과 무관하게 취소(플래그 0으로 처리)
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[수동] 전기 취소 (플래그 적용)");
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"현재 발생된 전기를 즉시 취소합니다.");
          ImGui::EndTooltip();
        }

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

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

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

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

        // 훅/캡처 상태를 로그로 출력 (상태 변경 시 1회만)
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
        EndSection(); // 평정 및 진급
      }
    }
  } // namespace MenuSections
} // namespace DX11Base
