#include "pch.h"
#include "TraitTextEditorWindow.h"

#include "TraitTextEditorData.h"
#include "TraitTextNameHook.h"
#include "TraitTextDescHook.h"
#include "TraitTextSpecialDescHook.h"
#include "../../Cheats.h"
#include "../../Hooking/MinHook.h"
#include "../../Framework/imgui.h"
#include "../../showlog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

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

constexpr uintptr_t kTraitEffectQueryOffset = 0x17AAD60;
constexpr uintptr_t kGameStateOffset = 0xD0;
constexpr uint8_t kCouncilState = 0x05;

void MaintainTraitEffectCouncilGate() {
  const uintptr_t gameData = GetGameBaseFast();
  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (gameData < 0x10000 || !exeBase)
    return;

  uint8_t gameState = 0;
  __try {
    gameState = *reinterpret_cast<const uint8_t *>(gameData + kGameStateOffset);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return;
  }

  const bool shouldEnable = gameState != kCouncilState;
  void *target = reinterpret_cast<void *>(exeBase + kTraitEffectQueryOffset);
  const MH_STATUS status = shouldEnable
      ? MH_EnableHook(target)
      : MH_DisableHook(target);

  const bool ok = status == MH_OK ||
      (shouldEnable && status == MH_ERROR_ENABLED) ||
      (!shouldEnable && status == MH_ERROR_DISABLED);
  if (!ok)
    return;

  static int lastEnabled = -1;
  const int enabled = shouldEnable ? 1 : 0;
  if (lastEnabled == enabled)
    return;

  lastEnabled = enabled;
  AddLog(shouldEnable
             ? u8"[기재JSON/효과] 평정 종료 감지: +17AAD60 메인 효과 판정 ON"
             : u8"[기재JSON/효과] 평정 진입 감지: +17AAD60 메인 효과 판정 OFF");
}

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

bool ApplyAll(std::string& error) {
  for (size_t i = 0; i < GetTraitTextEditRows().size(); ++i) {
    if (!ValidateTraitTextRow(i, &error))
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
  return true;
}

bool RemoveAll(std::string& error) {
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
  // +17AAD60은 전투/내정 효과 호환에는 유지하되,
  // 평정(0x05)에서만 비활성화하여 임면의 기재 보유 오인을 막습니다.
  MaintainTraitEffectCouncilGate();

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
