#include "pch.h"
#include "TraitTextEditorWindow.h"

#include "TraitTextEditorData.h"
#include "TraitTextNameHook.h"
#include "TraitTextDescHook.h"
#include "TraitTextSpecialDescHook.h"
#include "../../Framework/imgui.h"
#include "../../showlog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace DX11Base {
namespace {

static bool g_open = false;
static bool g_loadedOnce = false;
static int g_selected = 0;
static std::array<char, 128> g_nameBuf{};
static std::array<char, 4096> g_descBuf{};
static std::string g_status;

// Step 6 자동 적용 상태. 메뉴 렌더링이 시작된 뒤 잠시 기다렸다가 저장값을 적용합니다.
static bool g_autoLoaded = false;
static bool g_autoFinished = false;
static ULONGLONG g_autoFirstTick = 0;
static ULONGLONG g_autoLastAttempt = 0;
static int g_autoAttempts = 0;

// T06: 동일한 편집 내용을 다시 적용할 때 세 후크를 해제/재생성하지 않습니다.
// code cave는 게임 UI가 이전 문자열 주소를 보유할 가능성 때문에 즉시 해제할 수 없으므로,
// 동일 설정 재적용을 건너뛰는 것이 가장 안전한 첫 번째 누적 메모리 방어입니다.
static bool g_lastApplySnapshotValid = false;
static std::vector<std::pair<std::string, std::string>> g_lastAppliedTexts;
static bool g_lastNameHookApplied = false;
static bool g_lastDescHookApplied = false;
static bool g_lastSpecialDescHookApplied = false;

void CopyToBuffer(const std::string& text, char* dst, size_t size) {
  if (!dst || size == 0)
    return;
  std::snprintf(dst, size, "%s", text.c_str());
  dst[size - 1] = '\0';
}

void LoadSelectionBuffers() {
  auto& rows = GetTraitTextEditRows();
  if (rows.empty()) {
    g_nameBuf[0] = '\0';
    g_descBuf[0] = '\0';
    return;
  }
  g_selected = std::clamp(g_selected, 0, static_cast<int>(rows.size()) - 1);
  CopyToBuffer(rows[g_selected].newName, g_nameBuf.data(), g_nameBuf.size());
  CopyToBuffer(rows[g_selected].newDesc, g_descBuf.data(), g_descBuf.size());
}

void StoreSelectionBuffers() {
  auto& rows = GetTraitTextEditRows();
  if (rows.empty() || g_selected < 0 || g_selected >= static_cast<int>(rows.size()))
    return;
  rows[g_selected].newName = g_nameBuf.data();
  rows[g_selected].newDesc = g_descBuf.data();
}

std::vector<std::pair<std::string, std::string>> CaptureAppliedTextSnapshot() {
  const auto& rows = GetTraitTextEditRows();
  std::vector<std::pair<std::string, std::string>> snapshot;
  snapshot.reserve(rows.size());
  for (const auto& row : rows)
    snapshot.emplace_back(row.newName, row.newDesc);
  return snapshot;
}

bool HookStateMatchesLastApply() {
  return IsTraitTextNameHookApplied() == g_lastNameHookApplied &&
         IsTraitTextDescHookApplied() == g_lastDescHookApplied &&
         IsTraitTextSpecialDescHookApplied() == g_lastSpecialDescHookApplied;
}

void InvalidateLastApplySnapshot() {
  g_lastApplySnapshotValid = false;
}

bool ApplyAll(std::string& error) {
  for (size_t i = 0; i < GetTraitTextEditRows().size(); ++i) {
    if (!ValidateTraitTextRow(i, &error))
      return false;
  }

  auto currentSnapshot = CaptureAppliedTextSnapshot();
  if (g_lastApplySnapshotValid &&
      currentSnapshot == g_lastAppliedTexts &&
      HookStateMatchesLastApply()) {
    error.clear();
    AddLog(u8"[기재 문구/T06] 동일 설정 재적용 생략");
    return true;
  }

  // 여기부터는 실제 후크 상태가 바뀔 수 있으므로, 중간 실패 시 이전 snapshot으로
  // 잘못 생략하지 않도록 먼저 무효화합니다.
  InvalidateLastApplySnapshot();

  if (!ApplyTraitTextNameHook(&error))
    return false;
  if (!ApplyTraitTextDescHook(&error)) {
    std::string ignored;
    RemoveTraitTextNameHook(&ignored);
    return false;
  }
  if (!ApplyTraitTextSpecialDescHook(&error)) {
    std::string ignored;
    RemoveTraitTextDescHook(&ignored);
    RemoveTraitTextNameHook(&ignored);
    return false;
  }

  g_lastAppliedTexts = std::move(currentSnapshot);
  g_lastNameHookApplied = IsTraitTextNameHookApplied();
  g_lastDescHookApplied = IsTraitTextDescHookApplied();
  g_lastSpecialDescHookApplied = IsTraitTextSpecialDescHookApplied();
  g_lastApplySnapshotValid = true;
  return true;
}

bool RemoveAll(std::string& error) {
  // 해제 성공/실패와 무관하게 이후 ApplyAll은 실제 상태를 다시 확인하고 적용해야 합니다.
  InvalidateLastApplySnapshot();

  std::string firstError;
  bool ok = true;
  std::string e;

  if (!RemoveTraitTextSpecialDescHook(&e)) {
    ok = false;
    firstError = e;
  }
  e.clear();
  if (!RemoveTraitTextDescHook(&e)) {
    ok = false;
    if (firstError.empty()) firstError = e;
  }
  e.clear();
  if (!RemoveTraitTextNameHook(&e)) {
    ok = false;
    if (firstError.empty()) firstError = e;
  }

  if (!ok)
    error = firstError;
  return ok;
}

void SetStatus(const std::string& text) {
  g_status = text;
  if (!text.empty())
    AddLog(u8"[기재 문구] %s", text.c_str());
}

} // namespace

void OpenTraitTextEditorWindow() {
  g_open = true;
  if (!g_loadedOnce) {
    std::string error;
    if (!LoadTraitTextEdits(&error) && !error.empty())
      SetStatus(std::string("저장값 불러오기 실패: ") + error);
    g_loadedOnce = true;
    LoadSelectionBuffers();
  }
}

bool IsTraitTextEditorWindowOpen() {
  return g_open;
}

void TickTraitTextEditorAutoApply() {
  if (g_autoFinished)
    return;

  if (!g_autoLoaded) {
    std::string error;
    if (!LoadTraitTextEdits(&error)) {
      g_autoFinished = true;
      if (!error.empty())
        AddLog(u8"[기재 문구/Step6] 저장값 자동 로드 실패: %s", error.c_str());
      return;
    }
    g_autoLoaded = true;
    g_loadedOnce = true;
    if (!HasTraitTextEdits()) {
      g_autoFinished = true;
      return;
    }
  }

  const ULONGLONG now = GetTickCount64();
  if (g_autoFirstTick == 0) {
    g_autoFirstTick = now;
    return;
  }

  // D3D/메뉴가 뜬 뒤 3초를 기다리고, 이후 최대 30회만 재시도합니다.
  if (now - g_autoFirstTick < 3000ull || now - g_autoLastAttempt < 1000ull)
    return;
  g_autoLastAttempt = now;
  ++g_autoAttempts;

  std::string error;
  if (ApplyAll(error)) {
    g_autoFinished = true;
    AddLog(u8"[기재 문구/Step6] 저장된 이름/설명 자동 적용 완료");
    return;
  }

  if (g_autoAttempts >= 30) {
    g_autoFinished = true;
    AddLog(u8"[기재 문구/Step6] 자동 적용 중단(30회 실패): %s", error.c_str());
  }
}

void DrawTraitTextEditorWindow(float scale) {
  if (!g_open)
    return;

  ImGui::SetNextWindowSize(ImVec2(820.0f * scale, 620.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"기재 이름/설명 편집기", &g_open)) {
    ImGui::End();
    return;
  }

  auto& rows = GetTraitTextEditRows();
  if (rows.empty()) {
    ImGui::TextUnformatted(u8"기재 데이터가 없습니다.");
    ImGui::End();
    return;
  }

  ImGui::BeginChild("##trait_list", ImVec2(220.0f * scale, -42.0f * scale), true);
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const bool modified = !rows[i].newName.empty() || !rows[i].newDesc.empty();
    char label[256] = {};
    std::snprintf(label, sizeof(label), "%02d  %s%s", i + 1,
                  rows[i].oldName.c_str(), modified ? "  O" : "");
    if (ImGui::Selectable(label, g_selected == i)) {
      StoreSelectionBuffers();
      g_selected = i;
      LoadSelectionBuffers();
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginGroup();

  const auto& row = rows[g_selected];
  ImGui::Text(u8"기재: %s", row.oldName.c_str());
  ImGui::Separator();

  ImGui::TextUnformatted(u8"기존 설명");
  ImGui::BeginChild("##old_desc", ImVec2(0, 120.0f * scale), true);
  ImGui::TextWrapped("%s", row.oldDesc.c_str());
  ImGui::EndChild();

  ImGui::Spacing();
  ImGui::TextUnformatted(u8"새 기재명 (최대 5글자)");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##new_name", g_nameBuf.data(), g_nameBuf.size()))
    StoreSelectionBuffers();

  ImGui::Spacing();
  ImGui::TextUnformatted(u8"새 설명");
  if (ImGui::InputTextMultiline("##new_desc", g_descBuf.data(), g_descBuf.size(),
                                ImVec2(-1, 200.0f * scale)))
    StoreSelectionBuffers();

  ImGui::TextDisabled(u8"%%d 등의 형식 토큰을 유지할 경우 원문과 종류/순서가 같아야 합니다. %% 표시는 %%%% 사용.");

  ImGui::Spacing();
  if (ImGui::Button(u8"적용", ImVec2(90.0f * scale, 28.0f * scale))) {
    StoreSelectionBuffers();
    std::string error;
    if (ApplyAll(error)) {
      g_autoFinished = true;
      SetStatus(u8"적용 완료");
    } else {
      SetStatus(std::string(u8"적용 실패: ") + error);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"적용 해제", ImVec2(90.0f * scale, 28.0f * scale))) {
    std::string error;
    g_autoFinished = true;
    if (RemoveAll(error))
      SetStatus(u8"적용 해제 완료");
    else
      SetStatus(std::string(u8"적용 해제 실패: ") + error);
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"선택 초기화", ImVec2(100.0f * scale, 28.0f * scale))) {
    rows[g_selected].newName.clear();
    rows[g_selected].newDesc.clear();
    LoadSelectionBuffers();
    SetStatus(u8"선택 항목 초기화");
  }

  if (ImGui::Button(u8"저장", ImVec2(90.0f * scale, 28.0f * scale))) {
    StoreSelectionBuffers();
    std::string error;
    if (SaveTraitTextEdits(&error))
      SetStatus(u8"trait_texts.ini 저장 완료");
    else
      SetStatus(std::string(u8"저장 실패: ") + error);
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"전체 기본값", ImVec2(100.0f * scale, 28.0f * scale))) {
    ResetTraitTextEdits();
    LoadSelectionBuffers();
    SetStatus(u8"전체 편집값 초기화");
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"닫기", ImVec2(80.0f * scale, 28.0f * scale)))
    g_open = false;

  if (!g_status.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", g_status.c_str());
  }

  ImGui::EndGroup();
  ImGui::End();
}

} // namespace DX11Base
