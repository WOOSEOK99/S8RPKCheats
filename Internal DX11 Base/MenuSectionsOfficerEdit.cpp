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

    // GrowthM이 참조하는 게임 능력 성장 설정.
    // EXE + 0x3BA9850 구조체의 +0x38 한 바이트에
    // 플레이어 세력/타 세력 성장 속도가 각각 2bit로 저장되어 있다.
    // Step 1에서는 읽기 전용 진단만 수행한다.
    struct OfficerGrowthGameSettings {
      bool valid = false;
      uint8_t packed = 0;
      int playerForce = 0; // bits 0..1
      int otherForces = 0; // bits 2..3
    };

    static OfficerGrowthGameSettings ReadOfficerGrowthGameSettings() {
      OfficerGrowthGameSettings result{};
      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return result;

      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr =
          exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;

      if (!DX11Base::IsValidPtr(addr, 1))
        return result;

      result.packed = *(const uint8_t *)addr;
      result.playerForce = (int)(result.packed & 0x03);
      result.otherForces = (int)((result.packed >> 2) & 0x03);
      result.valid = true;
      return result;
    }

    static const char *OfficerGrowthSettingName(int raw) {
      switch (raw) {
      case 0: return u8"없음";
      case 1: return u8"느림";
      case 2: return u8"보통";
      case 3: return u8"빠름";
      default: return u8"알 수 없음";
      }
    }

    static bool WriteOfficerGrowthGameSettingsRaw(int playerForce, int otherForces) {
      if (playerForce < 0 || playerForce > 3 ||
          otherForces < 0 || otherForces > 3)
        return false;

      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return false;

      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr =
          exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;

      if (!DX11Base::IsValidPtr(addr, 1))
        return false;

      const uint8_t oldValue = *(const uint8_t *)addr;
      const uint8_t newValue =
          (uint8_t)((oldValue & 0xF0) |
                    (playerForce & 0x03) |
                    ((otherForces & 0x03) << 2));

      DWORD oldProtect = 0;
      if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProtect))
        return false;

      *(uint8_t *)addr = newValue;

      DWORD dummy = 0;
      VirtualProtect((LPVOID)addr, 1, oldProtect, &dummy);
      return *(const uint8_t *)addr == newValue;
    }

    static bool WriteOfficerGrowthGameSettings(int speed) {
      if (speed < 1 || speed > 3)
        return false;

      return WriteOfficerGrowthGameSettingsRaw(speed, speed);

    }

    void DrawOfficerEditSection(uintptr_t p1, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.75f, 0.6f, 1.0f, 1.0f), u8"[ 무장편집 ]");
      ImGui::TextDisabled(u8"전법 / 특기 / 기재 / 특수능력 / 기술 편집");
      ImGui::Spacing();

      float btnHeight = 26.0f * scale;

      if (ImGui::BeginTable("OfficerEditOpenRow", 2,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (p1 != 0) {
          if (ImGui::Button(u8"주인공", ImVec2(-FLT_MIN, btnHeight))) {
            bShowOfficerDetail = !bShowOfficerDetail;
          }
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(u8"모든 무장", ImVec2(-FLT_MIN, btnHeight))) {
          DX11Base::bShowOfficerListWin = !DX11Base::bShowOfficerListWin;
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      if (ImGui::Button(u8"전법 및 특기 일괄 변경", ImVec2(-1, 30 * scale))) {
        bShowBatchOfficerEditWin = !bShowBatchOfficerEditWin;
      }

      if (ImGui::BeginTable("OfficerEditScenarioTools", 2,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"모든 세력 기술 초기화", &bTechZero)) {
          DX11Base::SetTechZero(bTechZero);
          NotifyFeatureToggle(u8"모든 세력 기술 초기화", bTechZero);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"체크한 상태로 새로운 시나리오 시작시 모든 세력의 기술이 초기화 됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"모든 미발견 무장 재야로 변경", &bUndiscoveredToRonin)) {
          DX11Base::SetUndiscoveredToRonin(bUndiscoveredToRonin);
          NotifyFeatureToggle(u8"모든 미발견 무장 재야로 변경", bUndiscoveredToRonin);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"<주의사항>");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"체크시 저장된 게임 불러올시에도 적용이 됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      if (ImGui::Button(u8"세력별 기술력 편집", ImVec2(-1, 30 * scale))) {
        bShowFactionTechEditor = !bShowFactionTechEditor;
      }

      ImGui::Spacing();

      // 기재 관련 공통 편집
      if (ImGui::BeginTable("WarTraitRow", 2,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"기재 3슬롯 활성화", &DX11Base::bTraitViewer)) {
          const bool requested = DX11Base::bTraitViewer;
          if (!DX11Base::SetTraitViewerFeature(requested))
            DX11Base::bTraitViewer = DX11Base::IsTraitViewerFeatureApplied();
          NotifyFeatureToggle(u8"기재 3슬롯 활성화", DX11Base::bTraitViewer);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"기재 슬롯을 2개에서 3개로 확장합니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"편집 메뉴에서 3번째 기재를 부여할 수 있습니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(u8"기재 이름 편집", ImVec2(-FLT_MIN, btnHeight))) {
          DX11Base::OpenTraitTextEditorWindow();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"기본 기재 이름및 설명을 편집할수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"적용후 저장까지 하면 게임실행시 자동으로 적용이 됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      if (ImGui::Button(u8"모든 무장 일괄 랜덤기재 부여", ImVec2(-1, 30 * scale))) {
        DX11Base::OpenBatchRandomTraitAssignmentWindow();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"모든 유효 무장의 기존 기재는 유지하고 빈 슬롯만 랜덤으로 채웁니다.");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"실행 전 황금/녹색/적색 등급을 선택할 수 있습니다.");
        ImGui::EndTooltip();
      }

      // AI 무장 자동성장: 사용자에게는 하나의 옵션만 노출하고
      // 내부적으로 게임 원본 능력성장 설정(플레이어/타 세력)도 같은 속도로 맞춘다.
      {
        static bool s_growthSettingApplied = false;

        if (bAIOfficerAutoGrowth && !s_growthSettingApplied) {
          if (iAIOfficerGrowthSpeed < 1 || iAIOfficerGrowthSpeed > 3)
            iAIOfficerGrowthSpeed = 2;
          if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
            s_growthSettingApplied = true;
          }
        }
        if (!bAIOfficerAutoGrowth)
          s_growthSettingApplied = false;

        if (ImGui::Checkbox(u8"AI 무장 자동성장", &bAIOfficerAutoGrowth)) {
          if (bAIOfficerAutoGrowth) {
            const OfficerGrowthGameSettings current =
                ReadOfficerGrowthGameSettings();

            // 게임이 이미 느림/보통/빠름이면 그 값을 초기 속도로 존중한다.
            // 둘 다 없음이면 보통으로 시작한다.
            if (current.valid) {
              bAIOfficerGrowthRestoreNone =
                  (current.playerForce == 0 && current.otherForces == 0);

              if (current.otherForces >= 1 && current.otherForces <= 3)
                iAIOfficerGrowthSpeed = current.otherForces;
              else if (current.playerForce >= 1 && current.playerForce <= 3)
                iAIOfficerGrowthSpeed = current.playerForce;
              else
                iAIOfficerGrowthSpeed = 2;
            } else {
              bAIOfficerGrowthRestoreNone = false;
              iAIOfficerGrowthSpeed = 2;
            }

            if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
              s_growthSettingApplied = true;
              AddLog(u8"[AI성장] 자동성장 ON / 게임 능력성장=%s",
                     OfficerGrowthSettingName(iAIOfficerGrowthSpeed));
            } else {
              AddLog(u8"[AI성장] 게임 능력성장 설정 적용 실패");
            }
          } else {
            s_growthSettingApplied = false;

            if (bAnnualSpecialAbilityAutoAssign) {
              bAnnualSpecialAbilityAutoAssign = false;
              AddLog(u8"[AI성장] 자동성장 OFF -> 자동 특수능력 부여도 OFF");
            }

            if (bAIOfficerGrowthRestoreNone) {
              if (WriteOfficerGrowthGameSettingsRaw(0, 0)) {
                AddLog(u8"[AI성장] 자동성장 OFF / 원래 설정이 '없음'이어서 게임 능력성장도 '없음'으로 복귀");
              } else {
                AddLog(u8"[AI성장] 자동성장 OFF / 게임 능력성장 '없음' 복귀 실패");
              }
              bAIOfficerGrowthRestoreNone = false;
            } else {
              AddLog(u8"[AI성장] 자동성장 OFF / 게임 능력성장 설정은 유지");
            }
          }
          SaveConfig();
        }
        const bool growthToggleHovered = ImGui::IsItemHovered();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bAIOfficerAutoGrowth);
        ImGui::SetNextItemWidth(90.0f * scale);

        const char *growthSpeedItems[] = {u8"느림", u8"보통", u8"빠름"};
        int speedIndex = iAIOfficerGrowthSpeed - 1;
        if (speedIndex < 0 || speedIndex > 2)
          speedIndex = 1;

        if (ImGui::Combo(u8"##AIOfficerGrowthSpeed",
                         &speedIndex,
                         growthSpeedItems,
                         IM_ARRAYSIZE(growthSpeedItems))) {
          iAIOfficerGrowthSpeed = speedIndex + 1;
          if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
            s_growthSettingApplied = true;
            AddLog(u8"[AI성장] 성장 속도 변경: %s",
                   OfficerGrowthSettingName(iAIOfficerGrowthSpeed));
          } else {
            AddLog(u8"[AI성장] 성장 속도 적용 실패");
          }
          SaveConfig();
        }
        const bool growthComboHovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();

        const char *growthSpeedGuide =
            speedIndex == 0
                ? u8"장기 시나리오용"
                : (speedIndex == 2
                       ? u8"단기 시나리오 / 성장 체감 강조용"
                       : u8"추천 기본값");
        ImGui::SameLine();
        ImGui::TextDisabled(u8"- %s", growthSpeedGuide);
        const bool growthGuideHovered = ImGui::IsItemHovered();

        if (growthToggleHovered || growthComboHovered || growthGuideHovered) {
          ImGui::BeginTooltip();
          ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
          ImGui::TextUnformatted(
              u8"AI 자동성장과 게임 원본 능력성장 속도를 한 번에 제어합니다.");
          ImGui::Separator();
          ImGui::TextColored(
              ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
              u8"느림 : 장기 시나리오용");
          ImGui::TextColored(
              ImVec4(0.65f, 1.0f, 0.65f, 1.0f),
              u8"보통 : 추천 기본값");
          ImGui::TextColored(
              ImVec4(1.0f, 0.82f, 0.45f, 1.0f),
              u8"빠름 : 단기 시나리오 / 성장 체감 강조용");
          ImGui::Separator();
          ImGui::TextUnformatted(
              u8"- 게임 설정이 '없음'이면 기능을 켤 때 자동으로 '보통'으로 변경합니다.");
          ImGui::TextUnformatted(
              u8"- 원래 게임 설정이 '없음'이었다면 기능을 끌 때 다시 '없음'으로 복귀합니다.");
          ImGui::TextUnformatted(
              u8"- 원래 느림/보통/빠름이었다면 기능을 꺼도 마지막 선택 속도를 유지합니다.");
          ImGui::PopTextWrapPos();
          ImGui::EndTooltip();
        }
      }

      if (!bAIOfficerAutoGrowth)
        ImGui::BeginDisabled();

      if (ImGui::Checkbox(u8"자동 특수능력 부여", &bAnnualSpecialAbilityAutoAssign)) {
        NotifyFeatureToggle(u8"자동 특수능력 부여", bAnnualSpecialAbilityAutoAssign);
        SaveConfig();
      }
      const bool annualSpecialHovered = ImGui::IsItemHovered();

      if (!bAIOfficerAutoGrowth)
        ImGui::EndDisabled();

      if (annualSpecialHovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
        if (!bAIOfficerAutoGrowth) {
          ImGui::TextColored(
              ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
              u8"AI 무장 자동성장을 먼저 켜야 사용할 수 있습니다.");
          ImGui::Separator();
        }
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"매년 1월 평정에서 AI 자동성장이 끝난 뒤 특수 능력을 자동 판정합니다.");
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
            u8"[ 자동 기능 조합 ]");
        ImGui::TextUnformatted(
            u8"AI 성장 OFF / 특수능력 OFF : 두 기능 모두 미사용");
        ImGui::TextUnformatted(
            u8"AI 성장 ON  / 특수능력 OFF : AI 자동성장만 적용");
        ImGui::TextUnformatted(
            u8"AI 성장 ON  / 특수능력 ON  : AI 성장 후 특수능력 자동 판정");
        ImGui::TextDisabled(
            u8"AI 성장 OFF / 특수능력 ON  : 사용 불가 (특수능력 자동은 비활성화)");
        ImGui::Separator();
        ImGui::TextUnformatted(u8"- AI 자동성장 결과로 상승한 전법/능력치를 반영한 뒤 판정합니다.");
        ImGui::TextUnformatted(u8"- 이미 특수 능력을 하나라도 보유한 무장은 자동 판정에서 제외합니다.");
        ImGui::TextUnformatted(u8"- 새로 부여된 무장이 있을 때만 상단 알림과 알림 내역에 표시합니다.");
        ImGui::TextDisabled(u8"※ 수동 '모든 무장 특수 능력 자동 부여' 버튼과는 별도로 작동합니다.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }

      const bool specialAutoRunning = DX11Base::IsSpecialAbilityAutoAssignRunning();
      if (specialAutoRunning)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"모든 무장 특수 능력 자동 부여", ImVec2(-1, 30 * scale))) {
        if (DX11Base::AutoAssignSpecialAbilities())
          ImGui::OpenPopup(u8"특수 능력 자동 부여 진행###SpecialAbilityAutoAssignPopup");
      }
      if (specialAutoRunning)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 42.0f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"전체 무장 공간 1~5102의 실제 전법/특기/능력치를 분석하여 특수 능력을 자동으로 추가합니다.");
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           u8"기존에 수동으로 부여한 특수 능력은 삭제하지 않습니다.");
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), u8"[ 자동 부여 조건 ]");
        ImGui::TextUnformatted(u8"군악대 : 분기/고무 중 하나가 Lv2 이상 + 두 전법 레벨 합계 4 이상");
        ImGui::TextUnformatted(u8"무쌍 보병 : 무력 80 이상 + 강격/맹돌 중 하나 Lv2 이상 + 보병 전법 합계 + 보장×2가 9 이상");
        ImGui::TextUnformatted(u8"불꽃 기병 : 무력 80 이상 + 연격/기사 중 하나 Lv2 이상 + 기병 전법 합계 + 기장×2가 9 이상");
        ImGui::TextUnformatted(u8"원격 궁병 : 무력 75 이상 + 궁병 전법 합계 + 궁장×2가 9 이상 + 원사/시람 Lv2 이상 또는 궁병 전법 합계 8 이상");
        ImGui::TextUnformatted(u8"총사령관 : 통솔 90 이상 + 보병/기병/궁병 중 2계통 이상 전법합 6 이상 + 3병종 전법 합계 16 이상 + 군사 특기 합계 6 이상");
        ImGui::TextUnformatted(u8"기습부대 : 교란/급습/요격 중 2종 이상 보유 + 세 전법 레벨 합계 5 이상");
        ImGui::TextUnformatted(u8"대군사 : 지력 85 이상 + 열화/격류/낙석/요격 합계 7 이상 + 군사 특기 합계 5 이상 또는 신산 Lv2 이상");
        ImGui::TextUnformatted(u8"무신 : 무력 95 이상 + 보병/기병/궁병 전법 합계가 각각 5 이상 + 세 병종 총합 18 이상 + 각 병종에 Lv2 이상 전법 1개 이상");
        ImGui::TextUnformatted(u8"함선 병기화 : 함선 전법 레벨 합계 7 이상 + 수군 Lv2 이상");
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                           u8"등갑군 : 현재 자동 판정 제외 (수동 지정만 가능)");

        ImGui::Separator();
        ImGui::TextDisabled(u8"※ 전법/특기의 '합계'는 해당 항목들의 레벨 합계입니다.");
        ImGui::TextDisabled(u8"※ 기재 이름/ID는 자동 판정에 사용하지 않습니다.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }

      ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 480.0f * scale), ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal(
              u8"특수 능력 자동 부여 진행###SpecialAbilityAutoAssignPopup",
              nullptr,
              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const bool popupRunning = DX11Base::IsSpecialAbilityAutoAssignRunning();
        const char *autoStatus = DX11Base::GetSpecialAbilityAutoAssignStatus();

        ImGui::TextColored(
            ImVec4(1.0f, 0.84f, 0.0f, 1.0f),
            popupRunning ? u8"[ 특수 능력 자동 부여 진행 중 ]"
                         : u8"[ 특수 능력 자동 부여 결과 ]");
        ImGui::Separator();

        ImGui::ProgressBar(
            DX11Base::GetSpecialAbilityAutoAssignProgress(),
            ImVec2(-1.0f, 0.0f),
            autoStatus && autoStatus[0] != '\0' ? autoStatus : nullptr);

        if (popupRunning) {
          ImGui::Spacing();
          ImGui::TextDisabled(
              u8"전체 무장 공간을 분석하고 있습니다.");
          ImGui::TextDisabled(
              u8"완료 전에는 실제 특수 능력 설정을 변경하지 않습니다.");

          ImGui::Spacing();
          if (ImGui::Button(
                  u8"작업 취소",
                  ImVec2(-1.0f, 30.0f * scale))) {
            DX11Base::CancelSpecialAbilityAutoAssign();
          }
        } else {
          ImGui::Spacing();
          ImGui::TextColored(
              ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
              u8"이번 실행에서 새로 특수 능력을 부여받은 무장");
          ImGui::Separator();

          const std::size_t resultCount =
              DX11Base::GetSpecialAbilityAutoAssignResultCount();

          ImGui::BeginChild(
              "##SpecialAbilityAutoAssignResults",
              ImVec2(0.0f, 300.0f * scale),
              true,
              ImGuiWindowFlags_AlwaysVerticalScrollbar);

          if (resultCount == 0) {
            ImGui::TextDisabled(
                u8"새로 부여된 특수 능력이 없습니다.");
            ImGui::TextDisabled(
                u8"조건을 만족한 무장이 이미 해당 능력을 보유했거나 작업이 취소된 경우입니다.");
          } else {
            for (std::size_t i = 0; i < resultCount; ++i) {
              const char *line =
                  DX11Base::GetSpecialAbilityAutoAssignResultLine(i);
              if (line && line[0] != '\0')
                ImGui::TextWrapped("%s", line);
            }
          }
          ImGui::EndChild();

          ImGui::Spacing();
          if (ImGui::Button(
                  u8"닫기",
                  ImVec2(-1.0f, 32.0f * scale))) {
            ImGui::CloseCurrentPopup();
          }
        }

        ImGui::EndPopup();
      }
      ::DX11Base::DrawBatchOfficerEditWindow(scale);
      ::DX11Base::DrawFactionTechEditor(scale);

      EndSection(); // 무장편집
    }
  } // namespace MenuSections
} // namespace DX11Base
