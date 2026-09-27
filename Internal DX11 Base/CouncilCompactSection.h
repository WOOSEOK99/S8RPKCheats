#pragma once

#include "Cheats.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/TengiCave.h"
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
                                     uintptr_t gameBase, float scale) {
    const bool useP1 = (p1 != 0 && offset < 0x5000);
    const uintptr_t targetAddr = useP1 ? p1 : gameBase;

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f, 5.0f * scale);

    if (ImGui::Button("-", ImVec2(22.0f * scale, 25.0f * scale))) {
      --(*inputVal);
      if (useP1 && p1)
        DX11Base::ModifyStat(p1, offset, *inputVal, size);
      else if (!useP1 && gameBase)
        *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
    }

    ImGui::SameLine(0.0f, 4.0f * scale);
    ImGui::SetNextItemWidth(48.0f * scale);
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

    ImGui::SameLine(0.0f, 4.0f * scale);
    if (ImGui::Button("+", ImVec2(22.0f * scale, 25.0f * scale))) {
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

    MenuSections::DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP,
                              p1, gameBase, scale);

    if (ImGui::BeginTable(
            "CouncilMeritPrivilegeRow",
            2,
            ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_NoSavedSettings)) {
      ImGui::TableSetupColumn("Merit", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Privilege", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      DrawCompactCouncilStat(u8"공적", 0x100, 2, &v_Merit,
                             p1, gameBase, scale);

      ImGui::TableSetColumnIndex(1);
      DrawCompactCouncilStat(u8"특권", 0xEA, 1, &v_Priv,
                             0, gameBase, scale);

      ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Checkbox(u8"매 평정 새로운 전기 발생", &bInfTengi)) {
      NotifyFeatureToggle(u8"매 평정 새로운 전기 발생", bInfTengi);
      SaveConfig();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1, 1, 0, 1),
                         u8"매 평정 마다 새로운 전기가 발생합니다.");
      ImGui::EndTooltip();
    }

    ImGui::SameLine(160.0f * scale);

    if (ImGui::Checkbox(u8"중지 성성 취소", &bCancelCastleEvent)) {
      NotifyFeatureToggle(u8"중지 성성 취소", bCancelCastleEvent);
      SaveConfig();
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1, 1, 0, 1),
                         u8"중지 성성이 발생하면 즉시 취소합니다.");
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
      ImGui::TextColored(
          ImVec4(1, 1, 0, 1),
          u8"결전 발생 후 다음 결전의 재발생 대기 주기를 1년으로 단축합니다.");
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                         u8"다른 결전 발생 조건은 그대로 유지됩니다.");
      ImGui::EndTooltip();
    }

    ImGui::SameLine(160.0f * scale);

    if (ImGui::Button(u8"전기발생 즉시 취소", ImVec2(120, 26))) {
      if (DX11Base::GetCapturedTengiAddr() != 0) {
        DX11Base::CancelTengi();
        DX11Base::AddLog(u8"[수동] 전기 취소 (플래그 적용)");
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1, 1, 0, 1),
                         u8"현재 발생된 전기를 즉시 취소합니다.");
      ImGui::EndTooltip();
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
