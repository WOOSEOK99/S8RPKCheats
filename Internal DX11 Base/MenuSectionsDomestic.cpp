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
  extern void SetInstantAttitude(bool enable);
} // namespace DX11Base

namespace DX11Base {
  namespace MenuSections {

    static void DrawStatMini(const char *label, int *val, int offset, int size, uintptr_t baseAddr, int inputsize,
                             float scale) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(label);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(inputsize * scale);
      ImGui::PushID(label);
      if (ImGui::InputInt("##val", val, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
        if (baseAddr > 0x10000)
          DX11Base::ModifyStat(baseAddr, offset, *val, size);
      }
      if (!ImGui::IsItemActive() && baseAddr > 0x10000) {
        if (size == 1)
          *val = (int)(*(unsigned char *)(baseAddr + offset));
        else if (size == 2)
          *val = (int)(*(unsigned short *)(baseAddr + offset));
        else
          *val = (int)(*(unsigned int *)(baseAddr + offset));
      }
      ImGui::PopID();
    }

    void DrawDomesticSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();

        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 내정 ]");
        // DrawStatRow(u8"금", 0x300, 2, &v_Gold, p1, gameBase, scale);
        // DrawStatRow(u8"행동력", 0xEE, 1, &v_AP, p1, gameBase, scale);
        // DrawStatRow(u8"우호의 증표", 0xF8, 2, &v_Token, 0, gameBase, scale);

        DrawStatMini(u8"금", &v_Gold, 0x300, 4, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"행동력", &v_AP, 0xEE, 1, p1, 40, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"우호의 증표", &v_Token, 0xF8, 2, gameBase, 40, scale);

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"행동력 무제한", &bInfiniteAP)) {
          NotifyFeatureToggle(u8"행동력 무제한", bInfiniteAP);
          SaveConfig();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"견문 시 민심 최대", &bAttitudeHack)) {
          ::DX11Base::SetInstantAttitude(bAttitudeHack);
          NotifyFeatureToggle(u8"견문 시 민심 최대", bAttitudeHack);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"도시에서 견문을 1회만 해도 민심 수치가 100이 됩니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"청부 무제한 (주점)", &bInfiniteTavernRequests)) {
          NotifyFeatureToggle(u8"청부 무제한 (주점)", bInfiniteTavernRequests);
          SaveConfig();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"명품 자동 배분 (평정 끝날 때)", &bAutoFillSpecialties)) {
          NotifyFeatureToggle(u8"명품 자동 배분 (평정 끝날 때)", bAutoFillSpecialties);
          SaveConfig();
        }

        if (ImGui::Button(u8"명품", ImVec2(70.0f * scale, 0.0f))) {
          bShowSpecialtyInfoWin = !bShowSpecialtyInfoWin;
        }

        // -----------------------
        // ImGui::Separator();
        // ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), u8"[ 내정 배율 설정 ]");

        if (ImGui::Checkbox(u8"내정 배율 적용", &bDomestics)) {
          ::DX11Base::SetDomesticsMult(bDomestics);
          NotifyFeatureToggle(u8"내정 배율 적용", bDomestics);
          SaveConfig();
        }
        bool domesticsHov = ImGui::IsItemHovered(); // SameLine 전에 캡처

        // [도시 관리] 버튼 – 도시 목록 / 수송 / 무장 배치
        ImGui::SameLine(160.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button,
          bShowCityInfoWin ? ImVec4(0.18f, 0.55f, 0.18f, 1.f)
                           : ImVec4(0.15f, 0.30f, 0.55f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.80f, 1.f));
        if (ImGui::Button(u8"도시 관리", ImVec2(70.f * scale, 0.f)))
          bShowCityInfoWin = !bShowCityInfoWin;
        const bool cityManageHov = ImGui::IsItemHovered();
        ImGui::PopStyleColor(2);

        if (cityManageHov) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"도시 목록을 확인하고, 도시 간 수송 및 무장 배치를 관리합니다.");
          ImGui::EndTooltip();
        }

        if (domesticsHov) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"내정(개발, 보수 등) 시 배율을 적용합니다.");
          ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"현재 선택된 무장이 플레이어로 자동 등록됩니다.");
          ImGui::EndTooltip();
        }

        if (bDomestics) {
          ImGui::Indent();
          if (ImGui::SliderFloat(u8"플레이어 배율", &fDomesticsPlayer, 1.0f, 10.0f, "%.1fx")) {
            ::DX11Base::SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
            SaveConfig();
          }
          if (ImGui::SliderFloat(u8"세력 배율", &fDomesticsForce, 1.0f, 10.0f, "%.1fx")) {
            ::DX11Base::SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
            SaveConfig();
          }
          ImGui::Unindent();
        }

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        // --- 대도시 전환 추가 ---
        bool wasBigCityRunning = DX11Base::g_bigCityThreadRunning.load();
        if (wasBigCityRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"기술도시로 전환", &bBigCity)) {
          DX11Base::SetBigCityConvert(bBigCity);
          NotifyFeatureToggle(u8"기술도시로 전환", bBigCity);
          SaveConfig();
        }
        if (wasBigCityRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 낙양, 장안, 허창, 업, 양양, 건업, 성도");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 기술도시로 변환 및 최대 수치 한도 보정");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        bool wasBangmokRunning = DX11Base::g_bangmokThreadRunning.load();
        if (wasBangmokRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"방목도시 황폐화", &bBangmokCity)) {
          DX11Base::SetBangmokCity(bBangmokCity);
          NotifyFeatureToggle(u8"방목도시 황폐화", bBangmokCity);
          SaveConfig();
        }
        if (wasBangmokRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 오환, 강, 선비, 저, 남만 등 방목도시");
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                             u8"내용 : 방목도시의 능력치를 저하시키고 최대 수치를 고정합니다.");
          ImGui::EndTooltip();
        }

        bool wasNongRunning = DX11Base::g_nongCityThreadRunning.load();
        if (wasNongRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"농경도시 버프", &bNonggyeongCity)) {
          DX11Base::SetNonggyeongCity(bNonggyeongCity);
          NotifyFeatureToggle(u8"농경도시 버프", bNonggyeongCity);
          SaveConfig();
        }
        if (wasNongRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 남피, 평원, 북해, 제남, 하비, 소패, 계양 등");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 농경도시로 변환 및 농촌/상가 수치 한도 상향");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        bool wasSagRunning = DX11Base::g_sagCityThreadRunning.load();
        if (wasSagRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"상업도시 버프", &bSangeopCity)) {
          DX11Base::SetSangeopCity(bSangeopCity);
          NotifyFeatureToggle(u8"상업도시 버프", bSangeopCity);
          SaveConfig();
        }
        if (wasSagRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 무희, 제남, 요동, 업, 성도, 건업 등 (기술도시 제외)");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 상업도시로 변환 및 농촌/상가 수치 한도 상향");
          ImGui::EndTooltip();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawStatMini(u8"무명", &v_RepM, 0x106, 2, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"문명", &v_RepL, 0x104, 2, p1, 60, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"악명", &v_RepI, 0x108, 2, p1, 60, scale);

        ImGui::SetCursorPosX(200 * scale); // '악명' 라벨이 시작되는 위치와 동일하게 설정
        if (ImGui::Checkbox(u8"악명 항상 0 유지", &bZeroInfamy)) {
          NotifyFeatureToggle(u8"악명 항상 0 유지", bZeroInfamy);
          SaveConfig();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawStatMini(u8"담력", &v_Brave, 0x5BB8, 4, gameBase, 60, scale);
        ImGui::SameLine(160 * scale);
        if (ImGui::Checkbox(u8"보주 교체 무제한", &bFastJewel)) {
          NotifyFeatureToggle(u8"보주 교체 무제한", bFastJewel);
          SaveConfig();
        }

        bool allJewelsOpen = DX11Base::IsAllJewelsOpenPreferred();
        if (ImGui::Checkbox(u8"보주 전체 개방", &allJewelsOpen)) {
          if (DX11Base::SetAllJewelsOpen(allJewelsOpen)) {
            DX11Base::AddNotification(allJewelsOpen ? u8"보주 전체 개방 ON" : u8"보주 전체 개방 OFF");
            SaveConfig();
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"모든 보주가 개방됩니다.");
          ImGui::EndTooltip();
        }

        bool allSecondaryJewels = DX11Base::IsAllSecondaryJewelsEnabled();
        ImGui::SameLine(160 * scale);
        if (ImGui::Checkbox(u8"보조 보주 모두 사용", &allSecondaryJewels)) {
          if (DX11Base::SetAllSecondaryJewelsEnabled(allSecondaryJewels)) {
            DX11Base::AddNotification(allSecondaryJewels ? u8"보조 보주 모두 사용 ON"
                                                         : u8"보조 보주 모두 사용 OFF");
            SaveConfig();
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"모든 보주를 사용할수 있도록 설정합니다.");
          ImGui::EndTooltip();
        }

        EndSection();

      }
    }
  } // namespace MenuSections
} // namespace DX11Base
