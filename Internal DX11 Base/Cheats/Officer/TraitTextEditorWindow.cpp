#include "pch.h"
#include "TraitTextEditorWindow.h"

#include "CustomTraitDisplay.h"
#include "TraitConfigRuntime.h"
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
static bool g_selectedEmbedded = false;
static std::array<char, 128> g_nameBuf{};
static std::array<char, 4096> g_descBuf{};
static std::string g_status;

// Step 6 자동 적용 상태. 메뉴 렌더링이 시작된 뒤 잠시 기다렸다가 저장값을 적용합니다.
static bool g_autoLoaded = false;
static bool g_autoFinished = false;
static ULONGLONG g_autoFirstTick = 0;
static ULONGLONG g_autoLastAttempt = 0;
static int g_autoAttempts = 0;

constexpr const char* kEmbeddedVersionDllUnsupportedError =
    u8"version.dll 사용 중에는 내장 기본기재 문구 편집을 적용할 수 없습니다.";

void CopyToBuffer(const std::string& text, char* dst, size_t size) {
  if (!dst || size == 0)
    return;
  std::snprintf(dst, size, "%s", text.c_str());
  dst[size - 1] = '\0';
}

std::vector<TraitTextEditRow>& SelectedRows() {
  return g_selectedEmbedded ? GetEmbeddedTraitTextEditRows() : GetTraitTextEditRows();
}

void LoadSelectionBuffers() {
  auto& rows = SelectedRows();
  if (rows.empty()) {
    if (g_selectedEmbedded && !GetTraitTextEditRows().empty()) {
      g_selectedEmbedded = false;
      g_selected = 0;
      LoadSelectionBuffers();
      return;
    }
    g_nameBuf[0] = '\0';
    g_descBuf[0] = '\0';
    return;
  }
  g_selected = std::clamp(g_selected, 0, static_cast<int>(rows.size()) - 1);
  CopyToBuffer(rows[g_selected].newName, g_nameBuf.data(), g_nameBuf.size());
  CopyToBuffer(rows[g_selected].newDesc, g_descBuf.data(), g_descBuf.size());
}

void StoreSelectionBuffers() {
  auto& rows = SelectedRows();
  if (rows.empty() || g_selected < 0 || g_selected >= static_cast<int>(rows.size()))
    return;
  rows[g_selected].newName = g_nameBuf.data();
  rows[g_selected].newDesc = g_descBuf.data();
}

bool ApplyEmbeddedRows(std::string& error) {
  std::vector<EmbeddedTraitTextOverride> overrides;
  auto& rows = GetEmbeddedTraitTextEditRows();
  overrides.reserve(rows.size());

  for (size_t i = 0; i < rows.size(); ++i) {
    if (!ValidateEmbeddedTraitTextRow(i, &error))
      return false;

    const auto& row = rows[i];
    const bool nameChanged = !row.newName.empty() && row.newName != row.oldName;
    const bool descChanged = !row.newDesc.empty() && row.newDesc != row.oldDesc;
    if (!nameChanged && !descChanged)
      continue;

    EmbeddedTraitTextOverride item;
    item.traitId = row.traitId;
    if (nameChanged)
      item.name = row.newName;
    if (descChanged)
      item.desc = row.newDesc;
    overrides.push_back(std::move(item));
  }

  if (!ApplyEmbeddedTraitTextOverrides(overrides, &error))
    return false;

  // 주인공/모든 무장 편집 UI는 게임의 이름 getter가 아닌 CustomTraitDisplay 캐시를
  // 사용하므로, 실제 적용 성공 후 같은 ID 기반 편집값을 표시 오버레이에도 반영합니다.
  ClearCustomTraitDisplayTextOverrides();
  for (const auto& item : overrides)
    SetCustomTraitDisplayTextOverride(static_cast<uint16_t>(item.traitId), item.name, item.desc);
  return true;
}

bool ApplyAll(std::string& error, bool keepLegacyHooksOnUnsupportedEmbedded = false) {
  for (size_t i = 0; i < GetTraitTextEditRows().size(); ++i) {
    if (!ValidateTraitTextRow(i, &error))
      return false;
  }
  for (size_t i = 0; i < GetEmbeddedTraitTextEditRows().size(); ++i) {
    if (!ValidateEmbeddedTraitTextRow(i, &error))
      return false;
  }

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
  if (!ApplyEmbeddedRows(error)) {
    if (keepLegacyHooksOnUnsupportedEmbedded &&
        error == kEmbeddedVersionDllUnsupportedError) {
      return false;
    }
    std::string ignored;
    RemoveTraitTextSpecialDescHook(&ignored);
    RemoveTraitTextDescHook(&ignored);
    RemoveTraitTextNameHook(&ignored);
    return false;
  }
  return true;
}

bool RemoveAll(std::string& error) {
  std::string firstError;
  bool ok = true;
  std::string e;

  if (!ClearEmbeddedTraitTextOverrides(&e)) {
    ok = false;
    firstError = e;
  }
  ClearCustomTraitDisplayTextOverrides();
  e.clear();
  if (!RemoveTraitTextSpecialDescHook(&e)) {
    ok = false;
    if (firstError.empty()) firstError = e;
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
    std::string legacyError;
    std::string embeddedError;
    const bool legacyLoaded = LoadTraitTextEdits(&legacyError);
    const bool embeddedLoaded = LoadEmbeddedTraitTextEdits(&embeddedError);
    if (!legacyLoaded && !embeddedLoaded && (!legacyError.empty() || !embeddedError.empty())) {
      SetStatus(std::string("저장값 불러오기 실패: ") +
                (!legacyError.empty() ? legacyError : embeddedError));
    }
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
    std::string legacyError;
    std::string embeddedError;
    const bool legacyLoaded = LoadTraitTextEdits(&legacyError);
    const bool embeddedLoaded = LoadEmbeddedTraitTextEdits(&embeddedError);
    if (!legacyLoaded && !embeddedLoaded) {
      g_autoFinished = true;
      const std::string& error = !legacyError.empty() ? legacyError : embeddedError;
      if (!error.empty())
        AddLog(u8"[기재 문구/Step6] 저장값 자동 로드 실패: %s", error.c_str());
      return;
    }
    g_autoLoaded = true;
    g_loadedOnce = true;
    if (!HasTraitTextEdits() && !HasEmbeddedTraitTextEdits()) {
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
  if (ApplyAll(error, true)) {
    g_autoFinished = true;
    AddLog(u8"[기재 문구/Step6] 저장된 이름/설명 자동 적용 완료");
    return;
  }

  if (error == kEmbeddedVersionDllUnsupportedError) {
    g_autoFinished = true;
    AddLog(u8"[기재 문구/Step6] version.dll 사용 중: 내장 기본기재 문구 자동 적용 생략");
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

  ImGui::SetNextWindowSize(ImVec2(900.0f * scale, 650.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"기재 이름/설명 편집기", &g_open)) {
    ImGui::End();
    return;
  }

  auto& legacyRows = GetTraitTextEditRows();
  auto& embeddedRows = GetEmbeddedTraitTextEditRows();
  if (legacyRows.empty() && embeddedRows.empty()) {
    ImGui::TextUnformatted(u8"기재 데이터가 없습니다.");
    ImGui::End();
    return;
  }

  auto& selectedRows = SelectedRows();
  if (selectedRows.empty()) {
    g_selectedEmbedded = !g_selectedEmbedded;
    g_selected = 0;
  }
  {
    auto& activeRows = SelectedRows();
    g_selected = std::clamp(g_selected, 0, static_cast<int>(activeRows.size()) - 1);
  }

  ImGui::BeginChild("##trait_list", ImVec2(260.0f * scale, -42.0f * scale), true);
  ImGui::TextDisabled(u8"게임 기본 기재 (기존 72개)");
  for (int i = 0; i < static_cast<int>(legacyRows.size()); ++i) {
    const bool modified = !legacyRows[i].newName.empty() || !legacyRows[i].newDesc.empty();
    char label[320] = {};
    std::snprintf(label, sizeof(label), "%02d  %s%s##legacy_%d", i + 1,
                  legacyRows[i].oldName.c_str(), modified ? "  O" : "", i);
    if (ImGui::Selectable(label, !g_selectedEmbedded && g_selected == i)) {
      StoreSelectionBuffers();
      g_selectedEmbedded = false;
      g_selected = i;
      LoadSelectionBuffers();
    }
  }

  ImGui::Separator();
  ImGui::TextDisabled(u8"내장 기본기재 (ID 기반)");
  for (int i = 0; i < static_cast<int>(embeddedRows.size()); ++i) {
    const bool modified = !embeddedRows[i].newName.empty() || !embeddedRows[i].newDesc.empty();
    char label[320] = {};
    std::snprintf(label, sizeof(label), "ID %03d  %s%s##embedded_%d",
                  embeddedRows[i].traitId, embeddedRows[i].oldName.c_str(),
                  modified ? "  O" : "", embeddedRows[i].traitId);
    if (ImGui::Selectable(label, g_selectedEmbedded && g_selected == i)) {
      StoreSelectionBuffers();
      g_selectedEmbedded = true;
      g_selected = i;
      LoadSelectionBuffers();
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginGroup();

  auto& activeRows = SelectedRows();
  g_selected = std::clamp(g_selected, 0, static_cast<int>(activeRows.size()) - 1);
  const auto& row = activeRows[g_selected];
  if (g_selectedEmbedded)
    ImGui::Text(u8"기재: %s  (내장 ID %d)", row.oldName.c_str(), row.traitId);
  else
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
  if (g_selectedEmbedded)
    ImGui::TextDisabled(u8"내장 기본기재 편집은 version.dll이 없을 때만 ID 기준으로 적용됩니다.");

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
    activeRows[g_selected].newName.clear();
    activeRows[g_selected].newDesc.clear();
    LoadSelectionBuffers();
    SetStatus(u8"선택 항목 초기화");
  }

  if (ImGui::Button(u8"저장", ImVec2(90.0f * scale, 28.0f * scale))) {
    StoreSelectionBuffers();
    std::string error;
    if (!SaveTraitTextEdits(&error)) {
      SetStatus(std::string(u8"저장 실패: ") + error);
    } else if (!SaveEmbeddedTraitTextEdits(&error)) {
      SetStatus(std::string(u8"내장 기재 저장 실패: ") + error);
    } else {
      SetStatus(u8"기본/내장 기재 편집값 저장 완료");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"전체 기본값", ImVec2(100.0f * scale, 28.0f * scale))) {
    ResetTraitTextEdits();
    ResetEmbeddedTraitTextEdits();
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