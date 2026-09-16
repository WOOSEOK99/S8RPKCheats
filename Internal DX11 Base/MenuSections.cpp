#include "MenuSections.h"
#include "Cheats.h"
#include "Cheats/Civilian/BangmokCity.h"
#include "Cheats/Civilian/Bigcityconvert.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/Civilian/DomesticsMult.h"
#include "Cheats/Civilian/NonggyeongCity.h"
#include "Cheats/Civilian/SangeopCity.h"
#include "Cheats/Civilian/Techpointcave.h"
#include "Cheats/Civilian/Techzero.h"
#include "Cheats/Officer/OfficerRosterResolve.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Social/Fastrelationship.h"
#include "Cheats/Social/Infinitegift.h"
#include "Cheats/Social/Infinitetalk.h"
#include "Cheats/Social/InstantLoveCave.h"
#include "Cheats/Social/Loyaltycave.h"
#include "Cheats/Social/Resonancecave.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/SpeedHack.h"
#include "Cheats/Officer/OfficerDetail.h"
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
#include "Cheats/War/Roadblock.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/SiegeWarfare.h"
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
                                          IM_COL32(255, 165, 0, 140), 8.0f, 0, 1.2f);
      ImGui::Spacing();
    }

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

    void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 자원 및 도시 활동 ]");
        DrawStatMini(u8"금", &v_Gold, 0x300, 4, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"행동력", &v_AP, 0xEE, 1, p1, 40, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"우호의 증표", &v_Token, 0xF8, 2, gameBase, 40, scale);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox(u8"행동력 무한", &bInfiniteAP)) {
          NotifyFeatureToggle(u8"행동력 무한", bInfiniteAP);
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

        if (ImGui::Checkbox(u8"명품 자동 배분 (평정 끝날 때)", &bAutoFillSpecialties)) {
          NotifyFeatureToggle(u8"명품 자동 배분 (평정 끝날 때)", bAutoFillSpecialties);
          SaveConfig();
        }
        if (ImGui::Checkbox(u8"청부 무한 유지 (주점)", &bInfiniteTavernRequests)) {
          NotifyFeatureToggle(u8"청부 무한 유지 (주점)", bInfiniteTavernRequests);
          SaveConfig();
        }

        if (ImGui::Checkbox(u8"내정 배율 적용", &bDomestics)) {
          ::DX11Base::SetDomesticsMult(bDomestics);
          NotifyFeatureToggle(u8"내정 배율 적용", bDomestics);
          SaveConfig();
        }
        bool domesticsHov = ImGui::IsItemHovered();
        ImGui::SameLine(160.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button,
          bShowCityInfoWin ? ImVec4(0.18f, 0.55f, 0.18f, 1.f) : ImVec4(0.15f, 0.30f, 0.55f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.80f, 1.f));
        if (ImGui::Button(u8"도시 정보", ImVec2(70.f * scale, 0.f)))
          bShowCityInfoWin = !bShowCityInfoWin;
        ImGui::PopStyleColor(2);
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool wasBigCityRunning = DX11Base::g_bigCityThreadRunning.load();
        if (wasBigCityRunning) ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"기술도시로 전환", &bBigCity)) {
          DX11Base::SetBigCityConvert(bBigCity);
          NotifyFeatureToggle(u8"기술도시로 전환", bBigCity);
          SaveConfig();
        }
        if (wasBigCityRunning) ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 낙양, 장안, 허창, 업, 양양, 건업, 성도");
          ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"내용 : 기술도시로 변환 및 최대 수치 한도 보정");
          ImGui::EndTooltip();
        }
        ImGui::SameLine(160.0f * scale);

        bool wasBangmokRunning = DX11Base::g_bangmokThreadRunning.load();
        if (wasBangmokRunning) ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"방목도시 황폐화", &bBangmokCity)) {
          DX11Base::SetBangmokCity(bBangmokCity);
          NotifyFeatureToggle(u8"방목도시 황폐화", bBangmokCity);
          SaveConfig();
        }
        if (wasBangmokRunning) ImGui::EndDisabled();

        bool wasNongRunning = DX11Base::g_nongCityThreadRunning.load();
        if (wasNongRunning) ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"농경도시 버프", &bNonggyeongCity)) {
          DX11Base::SetNonggyeongCity(bNonggyeongCity);
          NotifyFeatureToggle(u8"농경도시 버프", bNonggyeongCity);
          SaveConfig();
        }
        if (wasNongRunning) ImGui::EndDisabled();
        ImGui::SameLine(160.0f * scale);
        bool wasSagRunning = DX11Base::g_sagCityThreadRunning.load();
        if (wasSagRunning) ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"상업도시 버프", &bSangeopCity)) {
          DX11Base::SetSangeopCity(bSangeopCity);
          NotifyFeatureToggle(u8"상업도시 버프", bSangeopCity);
          SaveConfig();
        }
        if (wasSagRunning) ImGui::EndDisabled();
        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 명성치 편집 ]");
        DrawStatMini(u8"무명", &v_RepM, 0x106, 2, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"문명", &v_RepL, 0x104, 2, p1, 60, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"악명", &v_RepI, 0x108, 2, p1, 60, scale);
        ImGui::SetCursorPosX(200 * scale);
        if (ImGui::Checkbox(u8"악명 항상 0 유지", &bZeroInfamy)) {
          NotifyFeatureToggle(u8"악명 항상 0 유지", bZeroInfamy);
          SaveConfig();
        }
        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 및 진급 관련 ]");
        DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP, p1, gameBase, scale);
        DrawStatRow(u8"공적", 0x100, 2, &v_Merit, p1, gameBase, scale);
        DrawStatRow(u8"특권", 0xEA, 1, &v_Priv, 0, gameBase, scale);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Checkbox(u8"전기 발생 무제한", &bInfTengi)) {
          NotifyFeatureToggle(u8"전기 발생 무제한", bInfTengi); SaveConfig();
        }
        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"중지 성성 취소", &bCancelCastleEvent)) {
          NotifyFeatureToggle(u8"중지 성성 취소", bCancelCastleEvent); SaveConfig();
        }
        if (ImGui::Button(u8"전기발생 취소", ImVec2(120, 26))) {
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[수동] 전기 취소 (플래그 적용)");
          }
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Checkbox(u8"만병 습득 조건 해제", &bSkillCondition)) {
          DX11Base::ApplySkillCondition(bSkillCondition); NotifyFeatureToggle(u8"만병 습득 조건 해제", bSkillCondition); SaveConfig();
        }
        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"상병 습득 조건 해제", &bSangbyeongCondition)) {
          DX11Base::ApplySangbyeongCondition(bSangbyeongCondition); NotifyFeatureToggle(u8"상병 습득 조건 해제", bSangbyeongCondition); SaveConfig();
        }
        if (ImGui::Checkbox(u8"유목기병 습득 조건 해제", &bYumokCondition)) {
          DX11Base::ApplyYumokCondition(bYumokCondition); NotifyFeatureToggle(u8"유목기병 습득 조건 해제", bYumokCondition); SaveConfig();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Checkbox(u8"능력치 한계돌파", &bAutoStatUp99)) {
          NotifyFeatureToggle(u8"능력치 한계돌파", bAutoStatUp99); SaveConfig();
        }
        {
          static uintptr_t s_lastHookAddr = 0;
          static uintptr_t s_lastCaptAddr = 0;
          uintptr_t hookAddr = DX11Base::GetTengiHookAddr();
          uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();
          if (hookAddr != s_lastHookAddr) {
            if (hookAddr != 0) DX11Base::AddLog(u8"[전기] 훅 지점 발견: %p (+0x%llX)", (void *)hookAddr, (unsigned long long)DX11Base::GetTengiHookOffset());
            s_lastHookAddr = hookAddr;
          }
          if (captAddr != s_lastCaptAddr) {
            if (captAddr != 0) DX11Base::AddLog(u8"[전기] 캡처 주소 확보: %p", (void *)captAddr);
            s_lastCaptAddr = captAddr;
          }
        }
        EndSection();
      }

      BeginSection();
      if (p1 != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"[ 보주 설정 ]");
        DrawStatMini(u8"담력", &v_Brave, 0x5BB8, 4, gameBase, 60, scale);
        ImGui::SameLine(120 * scale);
        if (ImGui::Checkbox(u8"보주 교체 무제한", &bFastJewel)) { NotifyFeatureToggle(u8"보주 교체 무제한", bFastJewel); SaveConfig(); }
      }
      EndSection();
    }

    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.82f, 0.7f, 0.55f, 1.0f), u8"[ 결혼/인연 관련 ]");
      bool wasRunning = ::DX11Base::g_initThreadRunning;
      auto DrawLoveCheckbox = [&](const char *label, bool *var, LoveMode mode) {
        if (wasRunning) ImGui::BeginDisabled();
        if (ImGui::Checkbox(label, var)) {
          if (*var) {
            if (mode == LoveMode::Normal) bHateCave = false; else bLoveCave = false;
            DX11Base::SetInstantLoveCave(false);
            if (::DX11Base::marriageApplied) ::DX11Base::SetMarriageCondition(false);
          }
          DX11Base::SetInstantLoveCave(*var, mode); NotifyFeatureToggle(label, *var); SaveConfig();
        }
        if (wasRunning) ImGui::EndDisabled();
      };
      DrawLoveCheckbox(u8"즉시 경애 맺기", &bLoveCave, LoveMode::Normal);
      ImGui::SameLine(160.0f * scale);
      DrawLoveCheckbox(u8"혐오/상극 무시 경애", &bHateCave, LoveMode::HateIgnore);
      if (::DX11Base::g_resonanceThreadRunning) ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"무조건 공명 발생", &bResonance)) { DX11Base::SetInstantResonance(bResonance); NotifyFeatureToggle(u8"무조건 공명 발생", bResonance); SaveConfig(); }
      if (::DX11Base::g_resonanceThreadRunning) ImGui::EndDisabled();
      ImGui::SameLine(160.0f * scale);
      if (ImGui::Checkbox(u8"경애 시 무조건 공명", &bFastRelationship)) { DX11Base::SetFastRelationship(bFastRelationship); NotifyFeatureToggle(u8"경애 시 무조건 공명", bFastRelationship); SaveConfig(); }
      if (ImGui::Checkbox(u8"선물 기증 무제한", &bInfiniteGift)) { DX11Base::SetInfiniteGift(bInfiniteGift); NotifyFeatureToggle(u8"선물 기증 무제한", bInfiniteGift); SaveConfig(); }
      ImGui::SameLine(160.0f * scale);
      if (ImGui::Checkbox(u8"담화 실행 무제한", &bInfiniteTalk)) { DX11Base::SetInfiniteTalk(bInfiniteTalk); NotifyFeatureToggle(u8"담화 실행 무제한", bInfiniteTalk); SaveConfig(); }
      if (::DX11Base::g_loyaltyThreadRunning.load()) ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"무장 충성도 100", &bLoyalty)) { DX11Base::SetInstantLoyalty(bLoyalty); NotifyFeatureToggle(u8"무장 충성도 100", bLoyalty); SaveConfig(); }
      if (::DX11Base::g_loyaltyThreadRunning.load()) ImGui::EndDisabled();
      ImGui::SameLine(160.0f * scale);
      bool tempMarriage = ::DX11Base::marriageApplied;
      if (ImGui::Checkbox(u8"결혼 무제한", &tempMarriage)) {
        if (tempMarriage && (bLoveCave || bHateCave)) { bLoveCave = false; bHateCave = false; DX11Base::SetInstantLoveCave(false); }
        ::DX11Base::SetMarriageCondition(tempMarriage); NotifyFeatureToggle(u8"결혼 무제한", tempMarriage); SaveConfig();
      }
      EndSection();
    }

    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");

      if (ImGui::Checkbox(u8"AI 전투 개선", &DX11Base::bAIWarImprove)) {
        DX11Base::SetAIWarImprove(DX11Base::bAIWarImprove);
        DX11Base::NotifyFeatureToggle(u8"AI 전투 개선", DX11Base::bAIWarImprove);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"컴퓨터 세력의 군단이 전쟁 행동에서 빠지는 현상을 완화합니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 게임 버전이 달라 원본 바이트가 일치하지 않으면 안전하게 적용하지 않습니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(160.0f * scale);
      if (ImGui::Checkbox(u8"모든 무장 성향 적극", &bAllAggressive)) {
        DX11Base::NotifyFeatureToggle(u8"모든 무장 성향 적극 자동 적용", bAllAggressive);
        DX11Base::SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1,1,0,1), u8"게임 진입 순간 모든 유효 무장의 전략 성향을 '적극'으로 자동 적용합니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
        DX11Base::SetBattleMapShuffle(bBattleMapShuffle); NotifyFeatureToggle(u8"전투맵 랜덤(관문제외)", bBattleMapShuffle); SaveConfig();
      }
      if (ImGui::Button(u8"전투 환경 및 조건 설정", ImVec2(150 * scale, 30 * scale))) bShowBattleEnvWin = !bShowBattleEnvWin;
      ImGui::SameLine();
      if (ImGui::Button(u8"모든 무장 일괄 편집", ImVec2(-1, 30 * scale))) bShowBatchOfficerEditWin = !bShowBatchOfficerEditWin;
      if (ImGui::Button(u8"세력별 기술력 편집", ImVec2(-1, 30 * scale))) bShowFactionTechEditor = !bShowFactionTechEditor;
      ::DX11Base::DrawBatchOfficerEditWindow(scale);
      ::DX11Base::DrawFactionTechEditor(scale);
      ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(0.5f,0.5f,0,1), u8"[ 전법 및 건물 ]");
      ImGui::SameLine();
      if (ImGui::Button(u8"수정", ImVec2(100.0f * scale, 25.0f * scale))) bShowTacticsEditWin = !bShowTacticsEditWin;
      if (ImGui::Checkbox(u8"치료", &bSelfHeal)) { DX11Base::SetSelfHeal(bSelfHeal); NotifyFeatureToggle(u8"치료", bSelfHeal); SaveConfig(); }
      ImGui::SameLine();
      if (ImGui::Checkbox(u8"동토", &bDongto)) { DX11Base::SetDongto(bDongto); NotifyFeatureToggle(u8"동토", bDongto); SaveConfig(); }
      ImGui::SameLine();
      if (ImGui::Checkbox(u8"천계", &bCelestial)) { DX11Base::SetCelestialMod(bCelestial); NotifyFeatureToggle(u8"천계", bCelestial); SaveConfig(); }
      ImGui::SameLine();
      if (ImGui::Checkbox(u8"투석", &bCatapult)) { DX11Base::SetCatapultCheat(bCatapult); NotifyFeatureToggle(u8"투석", bCatapult); SaveConfig(); }
      ImGui::SameLine();
      if (ImGui::Checkbox(u8"격류/낙석", &bTerrainIgnore)) { DX11Base::SetTerrainIgnore(bTerrainIgnore); NotifyFeatureToggle(u8"격류/낙석", bTerrainIgnore); SaveConfig(); }
      if (ImGui::Checkbox(u8"방어 건물 강화", &bDefBuilding)) { DX11Base::SetDefBuildingBoost(bDefBuilding); NotifyFeatureToggle(u8"방어 건물 강화", bDefBuilding); SaveConfig(); }
      EndSection();
    }

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.0f, 1.0f), u8"[ 시나리오 ]");
      ImGui::PushID(u8"ScenarioDate");
      {
        static int s_scenarioYearEdit = 200;
        static int s_scenarioMonthEdit = 1;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("["); ImGui::SameLine(0,0); ImGui::SetNextItemWidth(40.0f*scale);
        ImGui::InputInt(u8"##scY", &s_scenarioYearEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        const bool yearDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit(); const bool yearActive = ImGui::IsItemActive();
        ImGui::SameLine(0,0); ImGui::TextUnformatted("]"); ImGui::SameLine(0,4.0f*scale); ImGui::Text(u8"년");
        ImGui::SameLine(0,10.0f*scale); ImGui::TextUnformatted("["); ImGui::SameLine(0,0); ImGui::SetNextItemWidth(20.0f*scale);
        ImGui::InputInt(u8"##scM", &s_scenarioMonthEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        const bool monthDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit(); const bool monthActive = ImGui::IsItemActive();
        ImGui::SameLine(0,0); ImGui::TextUnformatted("]"); ImGui::SameLine(0,4.0f*scale); ImGui::Text(u8"월");
        if (yearDeactivatedAfterEdit) UpdateYear((unsigned short)s_scenarioYearEdit);
        if (monthDeactivatedAfterEdit) { if (s_scenarioMonthEdit>=1 && s_scenarioMonthEdit<=12) UpdateMonth((uint8_t)s_scenarioMonthEdit); else DX11Base::AddLog(u8"[시나리오 날짜] 월은 1~12만 가능합니다."); }
        if (!yearActive && !yearDeactivatedAfterEdit && !monthActive && !monthDeactivatedAfterEdit) {
          static unsigned long long s_lastScenarioDatePoll=0; const unsigned long long now=GetTickCount64();
          if (now-s_lastScenarioDatePoll>=250ull) { s_lastScenarioDatePoll=now; unsigned short cy=0; uint8_t cm=0; if (ReadScenarioDate(&cy,&cm)) { if ((int)cy!=s_scenarioYearEdit) s_scenarioYearEdit=(int)cy; if ((int)cm!=s_scenarioMonthEdit) s_scenarioMonthEdit=(int)cm; } }
        }
      }
      ImGui::PopID();
      ImGui::SameLine(160.0f*scale);
      if (ImGui::Checkbox(u8"세력 군주 보너스 자동 배정", &bFactionLordBonus)) { DX11Base::SetFactionLordBonus(bFactionLordBonus); NotifyFeatureToggle(u8"세력 군주 보너스 자동 배정", bFactionLordBonus); SaveConfig(); }
      if (ImGui::Checkbox(u8"시나리오 수정", &bStartSetting)) { DX11Base::SetStartSetting(bStartSetting); NotifyFeatureToggle(u8"시나리오 수정", bStartSetting); SaveConfig(); }
      ImGui::SameLine(160.0f*scale);
      if (ImGui::Checkbox(u8"모든 미발견 무장 재야로 변경", &bUndiscoveredToRonin)) { DX11Base::SetUndiscoveredToRonin(bUndiscoveredToRonin); NotifyFeatureToggle(u8"모든 미발견 무장 재야로 변경", bUndiscoveredToRonin); SaveConfig(); }
      if (ImGui::Checkbox(u8"모든 세력 기술 초기화", &bTechZero)) { DX11Base::SetTechZero(bTechZero); NotifyFeatureToggle(u8"모든 세력 기술 초기화", bTechZero); SaveConfig(); }
      ImGui::SameLine(160.0f*scale);
      if (ImGui::Checkbox(u8"교지 <-> 건녕 도로 차단", &bRoadBlock)) { DX11Base::SetRoadBlock(bRoadBlock); NotifyFeatureToggle(u8"교지 <-> 건녕 도로 차단", bRoadBlock); SaveConfig(); }
      if (ImGui::Checkbox(u8"재야 장수 등장 알림", &bMonitorRonin)) { NotifyFeatureToggle(u8"재야 장수 등장 알림", bMonitorRonin); SaveConfig(); }
      ImGui::SameLine(160.0f*scale);
      if (ImGui::Checkbox(u8"교지 <-> 회계 도로 차단", &bRoadBlock2)) { DX11Base::SetRoadBlock2(bRoadBlock2); NotifyFeatureToggle(u8"교지 <-> 회계 도로 차단", bRoadBlock2); SaveConfig(); }
      float demoBtnWidth=140.0f*scale;
      if (ImGui::Button(u8"데모플레이 중지", ImVec2(demoBtnWidth,26.0f*scale))) {
        uintptr_t gBase=DX11Base::GetGameBase(); uintptr_t p1_ptr=gBase+0xE0;
        if (DX11Base::g_savedHeroAddr>0x10000 && DX11Base::IsValidPtr(p1_ptr,8)) { DWORD oldP; if (VirtualProtect((LPVOID)p1_ptr,8,PAGE_READWRITE,&oldP)) { *(uintptr_t*)p1_ptr=DX11Base::g_savedHeroAddr; VirtualProtect((LPVOID)p1_ptr,8,oldP,&oldP); DX11Base::AddLog(u8"[데모] 데모 플레이 중지 (주인공 주소 복원 완료: %p)",(void*)DX11Base::g_savedHeroAddr); } }
        else if (DX11Base::g_savedHeroAddr<=0x10000) DX11Base::AddLog(u8"[데모] 복원할 백업 주소가 없습니다.");
      }
      EndSection();

      BeginSection();
      float btnWidth=80.0f*scale, btnHeight=26.0f*scale, spacing=10.0f*scale;
      ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f),u8"[ 정보 ]");
      if (p1!=0) {
        if (ImGui::Button(u8"명품",ImVec2(btnWidth,btnHeight))) bShowSpecialtyInfoWin=!bShowSpecialtyInfoWin;
        ImGui::SameLine(0,spacing); if (ImGui::Button(u8"주인공",ImVec2(btnWidth,btnHeight))) bShowOfficerDetail=!bShowOfficerDetail;
        ImGui::SameLine(0,spacing); if (ImGui::Button(u8"선택 무장",ImVec2(btnWidth,btnHeight))) bShowSelectedOfficerWin=!bShowSelectedOfficerWin;
        ImGui::SameLine(0,spacing);
      }
      if (ImGui::Button(u8"모든 무장",ImVec2(btnWidth,btnHeight))) DX11Base::bShowOfficerListWin=!DX11Base::bShowOfficerListWin;
      EndSection();

      BeginSection();
      ImGui::TextColored(ImVec4(1.0f,0.84f,0.0f,1.0f),u8"[ 위젯 ]");
      if (ImGui::Checkbox(u8"전기취소##WIDGET",&DX11Base::bShowWidgetTengi)) { NotifyFeatureToggle(u8"위젯: 전기취소",DX11Base::bShowWidgetTengi); SaveConfig(); }
      ImGui::SameLine(); if (ImGui::Checkbox(u8"주인공##WIDGET",&DX11Base::bShowWidgetHero)) { NotifyFeatureToggle(u8"위젯: 주인공",DX11Base::bShowWidgetHero); SaveConfig(); }
      ImGui::SameLine(); if (ImGui::Checkbox(u8"모든무장##WIDGET",&DX11Base::bShowWidgetAllOfficers)) { NotifyFeatureToggle(u8"위젯: 모든 무장",DX11Base::bShowWidgetAllOfficers); SaveConfig(); }
      ImGui::SameLine(); if (ImGui::Checkbox(u8"알림확인##WIDGET",&DX11Base::bShowWidgetNotif)) { NotifyFeatureToggle(u8"위젯: 알림확인",DX11Base::bShowWidgetNotif); SaveConfig(); }
      EndSection();

      BeginSection();
      ImGui::TextColored(ImVec4(0.0f,1.0f,1.0f,1.0f),u8"[ 알림 설정 ]");
      ImGui::SetNextItemWidth(150.0f*scale);
      if (ImGui::SliderFloat(u8"알림 속도",&DX11Base::g_notificationSpeed,20.0f,500.0f,"%.0f px/s")) SaveConfig();
      ImGui::SameLine(0,20.0f*scale);
      if (ImGui::Button(u8"알림 비우기",ImVec2(100.0f*scale,0))) { g_notifications.clear(); AddLog(u8"[알림] 모든 내역을 초기화했습니다."); }
      EndSection();
    }
  } // namespace MenuSections
} // namespace DX11Base
