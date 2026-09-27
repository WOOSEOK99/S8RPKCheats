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

// 기재 이름 편집은 원본 CT에서 version.dll+5CB0을 전제로 했습니다.
// 현재 우리 환경의 실제 연결 상태를 확인하기 위한 1회성 읽기 전용 진단입니다.
static bool g_nameHookDiagnosticLogged = false;
constexpr uintptr_t kDiagNameGetterOffset = 0x170E2D0;
constexpr uintptr_t kDiagNameGetterPointerOffset = 0x170E2D6;
constexpr uintptr_t kDiagVersionGetterOffset = 0x5CB0;

std::string ReadHexBytes(uintptr_t address, size_t count) {
  std::array<unsigned char, 16> bytes{};
  if (!address || count == 0 || count > bytes.size())
    return "<invalid>";

  SIZE_T bytesRead = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                         bytes.data(), count, &bytesRead) || bytesRead != count) {
    return "<read failed>";
  }

  char text[16 * 3 + 1] = {};
  size_t pos = 0;
  for (size_t i = 0; i < count && pos < sizeof(text); ++i) {
    const int written = std::snprintf(text + pos, sizeof(text) - pos,
                                      i == 0 ? "%02X" : " %02X", bytes[i]);
    if (written <= 0)
      break;
    pos += static_cast<size_t>(written);
  }
  return text;
}

bool ReadPointerDiagnostic(uintptr_t address, uintptr_t& value) {
  value = 0;
  SIZE_T bytesRead = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                           &value, sizeof(value), &bytesRead) &&
         bytesRead == sizeof(value);
}

std::string ModulePathUtf8(HMODULE module) {
  if (!module)
    return "<none>";

  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
    return "<path unavailable>";

  const int needed = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length),
                                          nullptr, 0, nullptr, nullptr);
  if (needed <= 0)
    return "<path convert failed>";

  std::string result(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length),
                      result.data(), needed, nullptr, nullptr);
  return result;
}

void LogNameHookReadOnlyDiagnostic() {
  if (g_nameHookDiagnosticLogged)
    return;
  g_nameHookDiagnosticLogged = true;

  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const HMODULE versionModule = GetModuleHandleW(L"version.dll");
  const uintptr_t versionBase = reinterpret_cast<uintptr_t>(versionModule);

  AddLog(u8"[기재 이름 진단] 읽기 전용 진단 시작");
  AddLog(u8"[기재 이름 진단] gameBase=%p / +170E2D0=%s",
         reinterpret_cast<void*>(gameBase),
         ReadHexBytes(gameBase ? gameBase + kDiagNameGetterOffset : 0, 16).c_str());

  uintptr_t target = 0;
  if (gameBase && ReadPointerDiagnostic(gameBase + kDiagNameGetterPointerOffset, target)) {
    AddLog(u8"[기재 이름 진단] [+170E2D6]=%p / target bytes=%s",
           reinterpret_cast<void*>(target), ReadHexBytes(target, 16).c_str());

    MEMORY_BASIC_INFORMATION mbi{};
    if (target && VirtualQuery(reinterpret_cast<const void*>(target), &mbi, sizeof(mbi)) == sizeof(mbi)) {
      const HMODULE ownerModule = reinterpret_cast<HMODULE>(mbi.AllocationBase);
      AddLog(u8"[기재 이름 진단] target AllocationBase=%p / module=%s",
             mbi.AllocationBase, ModulePathUtf8(ownerModule).c_str());
    } else {
      AddLog(u8"[기재 이름 진단] target VirtualQuery 실패");
    }
  } else {
    AddLog(u8"[기재 이름 진단] +170E2D6 포인터 읽기 실패");
  }

  if (versionBase) {
    AddLog(u8"[기재 이름 진단] version.dll base=%p / path=%s",
           reinterpret_cast<void*>(versionBase), ModulePathUtf8(versionModule).c_str());
    AddLog(u8"[기재 이름 진단] version.dll+5CB0=%p / bytes=%s",
           reinterpret_cast<void*>(versionBase + kDiagVersionGetterOffset),
           ReadHexBytes(versionBase + kDiagVersionGetterOffset, 16).c_str());
  } else {
    AddLog(u8"[기재 이름 진단] version.dll 미로드");
  }
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

  if (!ApplyTraitTextNameHook(&error)) {
    LogNameHookReadOnlyDiagnostic();
    return false;
  }
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
