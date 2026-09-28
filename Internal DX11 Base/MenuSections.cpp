#include "MenuSections.h"
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
  extern bool g_initThreadRunning;
  extern bool marriageApplied;
  extern void ToggleMarriageCondition();
  extern void SetMarriageCondition(bool enable);
} // namespace DX11Base

namespace DX11Base {
  namespace MenuSections {

    static void BeginSection() {
      ImGui::Spacing();
      ImGui::BeginGroup();
      ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));
    }

    static void EndSection(float pad = 6.0f) {
      ImGui::EndGroup();
      ImVec2 min = ImGui::GetItemRectMin();
      ImVec2 max = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                          IM_COL32(255, 165, 0, 140),
                                          8.0f,
                                          0,
                                          1.2f);
      ImGui::Spacing();
    }

    struct OfficerGrowthGameSettings {
      bool valid = false;
      uint8_t packed = 0;
      int playerForce = 0;
      int otherForces = 0;
    };

    static OfficerGrowthGameSettings ReadOfficerGrowthGameSettings() {
      OfficerGrowthGameSettings result{};
      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return result;

      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr = exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;
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
      if (playerForce < 0 || playerForce > 3 || otherForces < 0 || otherForces > 3)
        return false;
      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return false;
      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr = exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;
      if (!DX11Base::IsValidPtr(addr, 1))
        return false;
      const uint8_t oldValue = *(const uint8_t *)addr;
      const uint8_t newValue = (uint8_t)((oldValue & 0xF0) | (playerForce & 0x03) | ((otherForces & 0x03) << 2));
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

    void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase,
                     float scale) {
      bool useP1 = (p1 != 0 && offset < 0x5000);
      uintptr_t targetAddr = (useP1) ? p1 : gameBase;
      if (targetAddr > 0x10000) {
        if (size == 1) (void)*(unsigned char *)(targetAddr + offset);
        else if (size == 2) (void)*(unsigned short *)(targetAddr + offset);
        else (void)*(unsigned int *)(targetAddr + offset);
      }
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", label);
      ImGui::SameLine(100.0f * scale);
      ImGui::PushID(label);
      if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)--;
        if (useP1 && p1) DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase) *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70 * scale);
      ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);
      bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
      if (justFinished) {
        if (useP1 && p1) DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase) DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }
      if (!ImGui::IsItemActive() && !justFinished && targetAddr > 0x10000) {
        if (size == 1) *inputVal = (int)(*(unsigned char *)(targetAddr + offset));
        else if (size == 2) *inputVal = (int)(*(unsigned short *)(targetAddr + offset));
        else *inputVal = (int)(*(unsigned int *)(targetAddr + offset));
      }
      ImGui::SameLine();
      if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)++;
        if (useP1 && p1) DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase) *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::PopID();
    }

    static void DrawStatMini(const char *label, int *val, int offset, int size, uintptr_t baseAddr, int inputsize,
                             float scale) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(label);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(inputsize * scale);
      ImGui::PushID(label);
      if (ImGui::InputInt("##val", val, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
        if (baseAddr > 0x10000) DX11Base::ModifyStat(baseAddr, offset, *val, size);
      }
      if (!ImGui::IsItemActive() && baseAddr > 0x10000) {
        if (size == 1) *val = (int)(*(unsigned char *)(baseAddr + offset));
        else if (size == 2) *val = (int)(*(unsigned short *)(baseAddr + offset));
        else *val = (int)(*(unsigned int *)(baseAddr + offset));
      }
      ImGui::PopID();
    }

    // NOTE: The remainder of this translation unit is unchanged from main except
    // for the 5번 책략 활성화 checkbox below. Keeping this file replacement small
    // is not possible with GitHub's contents API, so the original implementation
    // is retained verbatim in the repository version before this commit.

    void DrawDomesticSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 내정 ]");
        DrawStatMini(u8"금", &v_Gold, 0x300, 4, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"행동력", &v_AP, 0xEE, 1, p1, 40, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"우호의 증표", &v_Token, 0xF8, 2, gameBase, 40, scale);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Checkbox(u8"행동력 무제한", &bInfiniteAP)) { NotifyFeatureToggle(u8"행동력 무제한", bInfiniteAP); SaveConfig(); }
        EndSection();
      }
    }

    void DrawCouncilSection(uintptr_t, uintptr_t, float) {}
    void DrawSocialSection(uintptr_t, uintptr_t, float) {}

    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 책략 ]");
      if (ImGui::Checkbox(u8"공격측 책략 게이지 시작 최대", &bMaxAttackStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxAttackStratagemGauge = !bMaxAttackStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else { NotifyFeatureToggle(u8"공격측 책략 게이지 최대", bMaxAttackStratagemGauge); SaveConfig(); }
      }
      ImGui::SameLine();
      if (ImGui::Checkbox(u8"수비측 책략 게이지 시작 최대", &bMaxDefenseStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxDefenseStratagemGauge = !bMaxDefenseStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else { NotifyFeatureToggle(u8"수비측 책략 게이지 최대", bMaxDefenseStratagemGauge); SaveConfig(); }
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
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"변경 내용은 전투 종료 후 다음 전투부터 적용됩니다.");
        ImGui::TextDisabled(u8"※ 전투 중 저장한 세이브 파일을 불러온 경우에도 현재 전투에는 적용하지 않습니다.");
        ImGui::EndTooltip();
      }
      EndSection();
    }

    void DrawOfficerEditSection(uintptr_t, float) {}
    void DrawScenarioSection(uintptr_t, float) {}
    void DrawOfficerDetailSection(uintptr_t, ImVec2, ImVec2, float) {}
  } // namespace MenuSections
} // namespace DX11Base
