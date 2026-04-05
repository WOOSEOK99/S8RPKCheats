#include "MenuSections.h"
#include "Cheats.h"
#include "Cheats/BattleMapShuffle.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Bigcityconvert.h"
#include "Cheats/Catapult.h"
#include "Cheats/Celestia.h"
#include "Cheats/Defatkboost.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/Dongto.h"
#include "Cheats/Fastrelationship.h"
#include "Cheats/Infinitegift.h"
#include "Cheats/Infinitetalk.h"
#include "Cheats/InstantLoveCave.h"
#include "Cheats/Loyaltycave.h"
#include "Cheats/Resonancecave.h"
#include "Cheats/Roadblock.h"
#include "Cheats/Selfheal.h"
#include "Cheats/SpeedHack.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/Techzero.h"
#include "Cheats/Terrainignore.h"
#include "Config.h"
#include "MenuState.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"


namespace DX11Base {
  // 글로벌/네임스페이스 변수들에 대한 extern 선언 (정의는 다른 cpp 파일에 있음)
  extern void SetInstantAttitude(bool enable);
  extern bool g_initThreadRunning;
  extern bool g_loyaltyThreadRunning;
  extern bool marriageApplied;
  extern void ToggleMarriageCondition();
} // namespace DX11Base

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
      if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale)))
        (*inputVal)--;
      ImGui::SameLine();

      char valBuf[32];
      snprintf(valBuf, sizeof(valBuf), "%d##val", *inputVal);
      if (ImGui::Button(valBuf, ImVec2(70 * scale, 25 * scale))) {
        pSelectedVar = inputVal;
        currentLabel = std::string(label) + u8" 입력기";
      }
      ImGui::SameLine();

      if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale)))
        (*inputVal)++;
      ImGui::SameLine();

      // 적용 버튼에 현재 값 포함 (사용자 요청: 좀 더 깔끔하게)
      char applyBuf[64];
      if (valid)
        snprintf(applyBuf, sizeof(applyBuf), u8"적용 (현재: %u)", current);
      else
        snprintf(applyBuf, sizeof(applyBuf), u8"적용 (연결 끊김)");

      if (ImGui::Button(applyBuf, ImVec2(145 * scale, 25 * scale))) {
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::PopID();
    }

    void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 자원 및 도시 활동 ]");
        DrawStatRow(u8"금", 0x300, 2, &v_Gold, p1, gameBase, scale);
        DrawStatRow(u8"행동력", 0xEE, 1, &v_AP, p1, gameBase, scale);
        DrawStatRow(u8"우호의 증표", 0xF8, 2, &v_Token, 0, gameBase, scale);
        if (ImGui::Checkbox(u8"[내정] 행동력 무한", &bInfiniteAP))
          SaveConfig();

        // --- 대도시 전환 추가 ---
        bool wasBigCityRunning = DX11Base::g_bigCityThreadRunning;
        if (wasBigCityRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"[도시] 기술도시로 전환", &bBigCity)) {
          DX11Base::SetBigCityConvert(bBigCity);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 낙양, 장안, 허창, 업, 양양, 건업, 성도");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"개발 : 9000");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"상업 : 12000");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"방어 : 9000");
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"기술 : 4000");
          ImGui::EndTooltip();
        }

        if (wasBigCityRunning) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"처리 중...");
          ImGui::EndDisabled();
        }
        // -----------------------

        if (ImGui::Checkbox(u8"[도시] 견문 시 민심 최대", &bAttitudeHack)) {
          ::DX11Base::SetInstantAttitude(bAttitudeHack);
          SaveConfig();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(u8"도시에서 견문을 1회만 해도 민심 수치가 100이 됩니다.");

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 명성치 편집 ]");
        DrawStatRow(u8"무명", 0x106, 2, &v_RepM, p1, gameBase, scale);
        DrawStatRow(u8"문명", 0x104, 2, &v_RepL, p1, gameBase, scale);
        DrawStatRow(u8"악명", 0x108, 2, &v_RepI, p1, gameBase, scale);

        if (ImGui::Checkbox(u8"악명 항상 0 유지", &bZeroInfamy)) {
          SaveConfig();
        }

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 및 진급 관련 ]");
        DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP, p1, gameBase, scale);
        DrawStatRow(u8"공적", 0x100, 2, &v_Merit, p1, gameBase, scale);
        DrawStatRow(u8"특권", 0xEA, 1, &v_Priv, 0, gameBase, scale);
      }

      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"[ 보주 설정 ]");
      DrawStatRow(u8"담력", 0x5BB8, 4, &v_Brave, 0, gameBase, scale);
      if (ImGui::Checkbox(u8"[보주] 보주 교체 무제한", &bFastJewel))
        SaveConfig();

      // [ 무장 정보 ] 섹션 신규 추가 (사용자 요청)
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 무장 정보 ]");

      float btnWidth = 138.0f * scale; // 컬럼 침범을 막기 위해 너비를 약간 축소
      float btnHeight = 26.0f * scale;

      if (ImGui::Button(u8"모든 무장 정보", ImVec2(btnWidth, btnHeight))) {
        DX11Base::bShowOfficerListWin = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"주인공 정보", ImVec2(btnWidth, btnHeight))) {
        bShowOfficerDetail = !bShowOfficerDetail;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"선택 무장 정보", ImVec2(btnWidth, btnHeight))) {
        bShowSelectedOfficerWin = !bShowSelectedOfficerWin;
      }
    }

    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 결혼/인연 관련 ]");

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
          SaveConfig();
        }
        if (wasRunning && *var) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), u8"초기화 중...");
        }
        if (wasRunning)
          ImGui::EndDisabled();
      };

      DrawLoveCheckbox(u8"[인연] 즉시 경애 맺기", &bLoveCave, LoveMode::Normal);
      DrawLoveCheckbox(u8"[인연] 혐오/상극 무시 경애", &bHateCave, LoveMode::HateIgnore);

      if (::DX11Base::g_resonanceThreadRunning)
        ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"[담화] 무조건 공명 발생", &bResonance)) {
        DX11Base::SetInstantResonance(bResonance);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"담화시 공명 갯수가 1개라도 있으면 무조건 공명 발생");
        ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"1개도 없으면 공명발생하지 않음.");
        ImGui::EndTooltip();
      }

      if (::DX11Base::g_resonanceThreadRunning) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"초기화 중...");
        ImGui::EndDisabled();
      }

      if (ImGui::Checkbox(u8"[담화] 경애 시 무조건 공명", &bFastRelationship)) {
        DX11Base::SetFastRelationship(bFastRelationship);
        SaveConfig();
      }

      if (::DX11Base::g_loyaltyThreadRunning)
        ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"[교류] 무장 충성도 100", &bLoyalty)) {
        DX11Base::SetInstantLoyalty(bLoyalty);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"교류 클릭시 목록에 있는 모든 무장의 충성이 100이 됨.");
        ImGui::EndTooltip();
      }

      if (::DX11Base::g_loyaltyThreadRunning) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"초기화 중...");
        ImGui::EndDisabled();
      }

      if (ImGui::Checkbox(u8"[교류] 선물 기증 무제한", &bInfiniteGift)) {
        DX11Base::SetInfiniteGift(bInfiniteGift);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"[교류] 담화 실행 무제한", &bInfiniteTalk)) {
        DX11Base::SetInfiniteTalk(bInfiniteTalk);
        SaveConfig();
      }

      bool tempMarriage = ::DX11Base::marriageApplied;
      if (ImGui::Checkbox(u8"[결혼] 결혼 무제한", &tempMarriage)) {
        ::DX11Base::ToggleMarriageCondition();
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"배우자가 있어도 무조건 결혼이 됩니다.");
        ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"대신 타 세력의 경우 등용은 안되네요.");
        ImGui::EndTooltip();
      }
    }

    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 전쟁 관련 ]");

      if (ImGui::Checkbox(u8"[전법 강화] 치료", &bSelfHeal)) {
        DX11Base::SetSelfHeal(bSelfHeal);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 치료 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 1 : 본인 부대 치료");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 2 : 본인 부대 치료 / 아군 부대 1칸 전체 치료");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 3 : 본인 부대 치료 / 아군 부대 2칸 전체 치료");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 동토", &bDongto)) {
        DX11Base::SetDongto(bDongto);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 동토 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 1 : 동토 확률 100%%");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 2 : 동토 확률 100%% / 상태이상 50%%");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 3 : 동토 확률 100%% / 상태이상 100%%");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 격류/낙석", &bTerrainIgnore)) {
        DX11Base::SetTerrainIgnore(bTerrainIgnore);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"발동 조건 : 비");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨별 데미지는 기존 데미지의 1/2");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 투석 병기", &bCatapult)) {
        DX11Base::SetCatapultCheat(bCatapult);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"사거리 증가: 2->5");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"광역딜");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"데미지증가 20->100");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 천계", &bCelestial)) {
        DX11Base::SetCelestialMod(bCelestial);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 천계 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 1 : 치료 효과 2000 / 광범위");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 2 : 치료 효과 3500 / 광범위");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 3 : 치료 효과 7000 / 광범위");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[수비] 방어 건물 사거리 강화", &bDefBuilding)) {
        DX11Base::SetDefBuildingBoost(bDefBuilding);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"수비측의 모든 건물의 사거리/시야 1 증가");
        ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"전투중인 상태로 저장된 게임을 불러올때에는 반영이 안됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[수비] 방어 건물 공격력 강화", &bDefAtk)) {
        DX11Base::SetDefAtkBoost(bDefAtk);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"수비측의 모든 건물의 공격력이 2배 증가합니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
        DX11Base::SetBattleMapShuffle(bBattleMapShuffle);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"매 분기 평정(Council) 기간 마다 모든 도시의 전투맵 데이터를 랜덤하게 섞습니다.");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"※ 평정 종료 시 자동으로 원상 복구됩니다.");
        ImGui::EndTooltip();
      }
    }

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
      ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 시나리오 ]");

      if (ImGui::Checkbox(u8"모든 세력 기술 초기화", &bTechZero)) {
        DX11Base::SetTechZero(bTechZero);
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           u8"체크한 상태로 새로운 시나리오 시작시 모든 세력의 기술이 초기화 됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"교지-건녕 / 교지-회계 도로 차단", &bRoadBlock)) {
        DX11Base::SetRoadBlock(bRoadBlock);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"재야 장수 등장 알림", &bMonitorRonin)) {
        SaveConfig();
      }
    }
  } // namespace MenuSections
} // namespace DX11Base
