#pragma once

#include "Cheats.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/War/CouncilContinueAfterMove.h"
#include "Cheats/War/CouncilExecuteFreeOfficers.h"
#include "Cheats/War/RulerTransferProposal.h"
#include "Cheats/War/TotalWarCycleShortening.h"
#include "Config.h"
#include "Framework/imgui.h"
#include "MenuSections.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "showlog.h"

namespace DX11Base {

  inline void BeginCompactCouncilSection() {
    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));
  }

  inline void EndCompactCouncilSection(float pad = 6.0f) {
    ImGui::EndGroup();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(min.x - pad, min.y - pad),
        ImVec2(max.x + pad, max.y + pad),
        IM_COL32(255, 165, 0, 140),
        8.0f,
        0,
        1.2f);
    ImGui::Spacing();
  }

  inline void DrawCompactCouncilStat(const char *label, int offset, int size,
                                     int *inputVal, uintptr_t p1,
                                     uintptr_t gameBase, float scale,
                                     float controlOffset) {
    const bool useP1 = (p1 != 0 && offset < 0x5000);
    const uintptr_t targetAddr = useP1 ? p1 : gameBase;

    ImGui::PushID(label);
    const float cellStartX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(cellStartX + controlOffset * scale);

    if (ImGui::Button("-", ImVec2(20.0f * scale, 25.0f * scale))) {
      --(*inputVal);
      if (useP1 && p1)
        DX11Base::ModifyStat(p1, offset, *inputVal, size);
      else if (!useP1 && gameBase)
        *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
    }

    ImGui::SameLine(0.0f, 3.0f * scale);
    ImGui::SetNextItemWidth(40.0f * scale);
    ImGui::InputInt("##val", inputVal, 0, 0,
                    ImGuiInputTextFlags_CharsDecimal);

    const bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
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

    ImGui::SameLine(0.0f, 3.0f * scale);
    if (ImGui::Button("+", ImVec2(20.0f * scale, 25.0f * scale))) {
      ++(*inputVal);
      if (useP1 && p1)
        DX11Base::ModifyStat(p1, offset, *inputVal, size);
      else if (!useP1 && gameBase)
        *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
    }

    ImGui::PopID();
  }

  inline void DrawCompactCouncilSection(uintptr_t p1, uintptr_t gameBase,
                                        float scale) {
    if (!p1)
      return;

    BeginCompactCouncilSection();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 ]");

    if (ImGui::BeginTable(
            "CouncilStatLayout",
            2,
            ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_NoSavedSettings)) {
      ImGui::TableSetupColumn("Merit", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("CouncilRight", ImGuiTableColumnFlags_WidthStretch, 1.0f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(1);
      DrawCompactCouncilStat(u8"전략 포인트", 0xED, 1, &v_SP,
                             p1, gameBase, scale, 64.0f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      DrawCompactCouncilStat(u8"공적", 0x100, 2, &v_Merit,
                             p1, gameBase, scale, 30.0f);

      ImGui::TableSetColumnIndex(1);
      DrawCompactCouncilStat(u8"특권", 0xEA, 1, &v_Priv,
                             0, gameBase, scale, 64.0f);

      ImGui::EndTable();
    }

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

    // 검증된 전기 슬롯 순서. 기간과 재발동 쿨다운은 별도 설정이다.
    static const char *const tengiNames[kTengiListEventCount] = {
      u8"결전", u8"이민족습격", u8"악적발호", u8"의심암귀", u8"민심혹란",
      u8"붕벽", u8"여세", u8"피폐", u8"권위고양", u8"보장각성",
      u8"기장각성", u8"궁장각성", u8"병격난무", u8"전승기", u8"중지성성"
    };
    static bool draftAllowed[kTengiListEventCount] = {};
    static int draftMonths[kTengiListEventCount] = {};
    static int draftCooldownYears[kTengiListEventCount] = {};

    ImGui::Spacing();
    if (ImGui::Button(u8"전기 목록 관리##tengiList", ImVec2(160.f * scale, 0.f))) {
      for (int i = 0; i < kTengiListEventCount; ++i) {
        draftAllowed[i] = g_tengiListAllowed[i];
        draftMonths[i] = g_tengiListDurationMonths[i];
        draftCooldownYears[i] = GetTengiListCooldownYears(i);
      }
      ImGui::OpenPopup(u8"전기 목록 관리##popup");
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(u8"체크한 전기 중 발생 조건을 만족한 후보를 선택합니다. 후보가 없으면 조건 충족을 기다립니다. (실험 기능)");
    }

    ImGui::SetNextWindowSize(ImVec2(660.f * scale, 550.f * scale), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(u8"전기 목록 관리##popup", nullptr,
                               ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::TextWrapped(u8"기간: 개월 단위, 0 = 기본값 / 쿨다운: 년 단위, -1 = 기본값, 0 = 대기 없음");
      ImGui::TextWrapped(u8"쿨다운은 마지막 발동일부터 계산합니다. 동일 전기 연속 금지는 별도 규칙입니다.");
      ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                         u8"※ 실험 기능입니다. 결전 주기 단축이 켜져 있으면 결전 쿨다운은 1년이 우선 적용됩니다.");
      ImGui::Spacing();

      const ImGuiTableFlags listFlags =
          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
      if (ImGui::BeginTable("##TengiListTable", 4, listFlags,
                            ImVec2(0.f, 355.f * scale))) {
        ImGui::TableSetupColumn(u8"전기 이름", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn(u8"발생 허용", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(u8"기간(개월)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(u8"쿨다운(년)", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
          ImGui::TableSetColumnIndex(3);
          ImGui::SetNextItemWidth(-1.f);
          if (ImGui::InputInt("##cooldownYears", &draftCooldownYears[i], 0, 0)) {
            if (draftCooldownYears[i] < -1) draftCooldownYears[i] = -1;
            if (draftCooldownYears[i] > 255) draftCooldownYears[i] = 255;
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      if (ImGui::Button(u8"설정 저장", ImVec2(115.f * scale, 0.f))) {
        for (int i = 0; i < kTengiListEventCount; ++i) {
          g_tengiListAllowed[i] = draftAllowed[i];
          g_tengiListDurationMonths[i] = draftMonths[i];
          SetTengiListCooldownYears(i, draftCooldownYears[i]);
        }
        SaveConfig();
        TickTengiListDurations(true);
        DX11Base::AddLog(u8"[전기 목록] 허용 목록·기간·쿨다운 설정 저장 완료");
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
      ImGui::TextColored(
          ImVec4(1, 1, 0, 1),
          u8"만병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
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
      ImGui::TextColored(
          ImVec4(1, 1, 0, 1),
          u8"상병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
      ImGui::EndTooltip();
    }

    if (ImGui::Checkbox(u8"유목기병 습득 조건 해제", &bYumokCondition)) {
      DX11Base::ApplyYumokCondition(bYumokCondition);
      NotifyFeatureToggle(u8"유목기병 습득 조건 해제", bYumokCondition);
      SaveConfig();
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(
          ImVec4(1, 1, 0, 1),
          u8"유목기병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
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
      ImGui::TextColored(
          ImVec4(1, 1, 0, 1),
          u8"평정 기간 진입 시, 모든 장수의 능력치 중 99인 항목을 100으로 올립니다.");
      ImGui::EndTooltip();
    }

    ImGui::SameLine(160.0f * scale);
    if (ImGui::Checkbox(u8"도시 이동 후 평정 지속", &bCouncilContinueAfterMove)) {
      const bool requested = bCouncilContinueAfterMove;
      if (!SetCouncilContinueAfterMove(requested))
        bCouncilContinueAfterMove = IsCouncilContinueAfterMoveApplied();
      NotifyFeatureToggle(u8"도시 이동 후 평정 지속", bCouncilContinueAfterMove);
      SaveConfig();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                         u8"평정 중 주인공의 도시가 바뀌어도 평정을 계속 진행합니다.");
      ImGui::TextUnformatted(u8"- 일반 이동 / 배정 이동");
      ImGui::TextUnformatted(u8"- 자동전투 승리 후 점령지 이동");
      ImGui::TextUnformatted(u8"- 수동전투 승리 후 점령지 이동");
      ImGui::TextUnformatted(u8"- 같은 이동 명령의 다른 장수에게 종료 플래그가 번지는 동작 보정");
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                         u8"주인공의 행동 완료 bit0은 유지하고 평정 종료에 관여하는 bit1만 해제합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 6개 hook 지점의 원본 바이트가 모두 일치할 때만 적용됩니다.");
      ImGui::EndTooltip();
    }

    if (ImGui::Checkbox(u8"세력 도시 재야 무장 처단",
                        &bCouncilExecuteFreeOfficers)) {
      const bool requested = bCouncilExecuteFreeOfficers;
      if (!SetCouncilExecuteFreeOfficers(requested))
        bCouncilExecuteFreeOfficers = IsCouncilExecuteFreeOfficersApplied();
      NotifyFeatureToggle(u8"세력 도시 재야 무장 처단",
                          bCouncilExecuteFreeOfficers);
      SaveConfig();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(
          ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
          u8"군주 평정의 처단 대상에 같은 세력 도시의 재야 무장을 추가합니다.");
      ImGui::TextColored(
          ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
          u8"게임 기본 처단 제외조건, 확인창, 처단 결과 처리는 그대로 유지됩니다.");
      ImGui::TextColored(
          ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
          u8"※ 처단 대상 선택창을 연 상태에서는 체크/해제하지 마세요.");
      ImGui::EndTooltip();
    }

    ImGui::SameLine(160.0f * scale);
    bool rulerTransferProposalEnabled = GetRulerTransferProposalMode() != 0;
    if (ImGui::Checkbox(u8"부하의 군주 이동 제안",
                        &rulerTransferProposalEnabled)) {
      const int requestedMode = rulerTransferProposalEnabled ? 2 : 0;
      if (!SetRulerTransferProposalMode(requestedMode))
        rulerTransferProposalEnabled = IsRulerTransferProposalApplied();
      NotifyFeatureToggle(u8"부하의 군주 이동 제안",
                          rulerTransferProposalEnabled);
      SaveConfig();
      SaveRulerTransferProposalPreference(rulerTransferProposalEnabled);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(
          ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
          u8"군주 직속 군사와 도독이 평정에서 군주 이동을 제안할 수 있게 합니다.");
      ImGui::TextColored(
          ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
          u8"그 외 장수의 제안 조건은 기존 게임 판정을 유지합니다.");
      ImGui::EndTooltip();
    }

    {
      static uintptr_t s_lastHookAddr = 0;
      static uintptr_t s_lastCaptAddr = 0;
      const uintptr_t hookAddr = DX11Base::GetTengiHookAddr();
      const uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();

      if (hookAddr != s_lastHookAddr) {
        if (hookAddr != 0) {
          DX11Base::AddLog(u8"[전기] 훅 지점 발견: %p (+0x%llX)",
                           (void *)hookAddr,
                           (unsigned long long)DX11Base::GetTengiHookOffset());
        }
        s_lastHookAddr = hookAddr;
      }

      if (captAddr != s_lastCaptAddr) {
        if (captAddr != 0)
          DX11Base::AddLog(u8"[전기] 캡처 주소 확보: %p", (void *)captAddr);
        s_lastCaptAddr = captAddr;
      }
    }

    EndCompactCouncilSection();
  }

} // namespace DX11Base
