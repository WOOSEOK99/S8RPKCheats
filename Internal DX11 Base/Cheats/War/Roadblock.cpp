#include "RoadBlock.h"
#include "../../Cheats.h"
#include "../../Framework/imgui.h"
#include "../../NotificationManager.h"
#include "../../pch.h"
#include "../../showlog.h"
#include <psapi.h>

namespace DX11Base {

  void AddLog(const char *fmt, ...);
  bool IsValidPtr(uintptr_t addr, SIZE_T size);

  bool bAIWarImprove = false;

  // ───────────────────────────────────────────────
  //  AI 전투 개선
  //  SAN8RPK.exe+0x144D24C : 74 0A (JE) -> EB 0A (JMP)
  //  현재 게임 버전에서 직접 확인한 원본 바이트가 일치할 때만 적용합니다.
  // ───────────────────────────────────────────────
  static constexpr uintptr_t kAIWarImproveOffset = 0x144D24C;

  static bool ReadAIWarImproveBytes(uintptr_t addr, unsigned char &op, unsigned char &disp) {
    op = 0;
    disp = 0;
    if (!IsValidPtr(addr, 2))
      return false;

    __try {
      op = *(unsigned char *)addr;
      disp = *(unsigned char *)(addr + 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      op = 0;
      disp = 0;
      return false;
    }
    return true;
  }

  static bool WriteAIWarImproveOpcode(uintptr_t addr, unsigned char opcode) {
    if (!IsValidPtr(addr, 2))
      return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect((LPVOID)addr, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
      return false;

    bool success = false;
    __try {
      *(unsigned char *)addr = opcode;
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }

    DWORD ignored = 0;
    VirtualProtect((LPVOID)addr, 1, oldProtect, &ignored);
    if (success)
      FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, 2);
    return success;
  }

  void SetAIWarImprove(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase) {
      AddLog(u8"[AI전투] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      if (enable)
        bAIWarImprove = false;
      return;
    }

    const uintptr_t patchAddr = exeBase + kAIWarImproveOffset;
    unsigned char op = 0;
    unsigned char disp = 0;
    if (!ReadAIWarImproveBytes(patchAddr, op, disp)) {
      AddLog(u8"[AI전투] 패치 주소 읽기 실패: %p", (void *)patchAddr);
      if (enable)
        bAIWarImprove = false;
      return;
    }

    // 두 번째 바이트(점프 거리)는 원본/패치 모두 0x0A여야 합니다.
    if (disp != 0x0A || (op != 0x74 && op != 0xEB)) {
      AddLog(u8"[AI전투] 패치 거부: 예상 바이트 불일치 (현재 %02X %02X / 기대 74 0A 또는 EB 0A)",
             (unsigned int)op, (unsigned int)disp);
      if (enable)
        bAIWarImprove = false;
      return;
    }

    const unsigned char wanted = enable ? 0xEB : 0x74;
    if (op == wanted)
      return;

    if (!WriteAIWarImproveOpcode(patchAddr, wanted)) {
      AddLog(u8"[AI전투] 패치 쓰기 실패: %p", (void *)patchAddr);
      if (enable)
        bAIWarImprove = false;
      return;
    }

    if (enable)
      AddLog(u8"[AI전투] 전투 개선 활성화 (SAN8RPK.exe+0x144D24C: 74 -> EB)");
    else
      AddLog(u8"[AI전투] 전투 개선 비활성화 (원본 74 복구)");
  }

  void DrawAIWarImproveSection(float scale) {
    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));

    if (ImGui::Checkbox(u8"AI 전투 개선", &bAIWarImprove)) {
      const bool requested = bAIWarImprove;
      SetAIWarImprove(requested);
      NotifyFeatureToggle(u8"AI 전투 개선", bAIWarImprove);
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                         u8"컴퓨터 세력의 군단이 전쟁 행동에서 빠지는 현상을 완화합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 게임 버전이 달라 원본 바이트가 일치하지 않으면 안전하게 적용하지 않습니다.");
      ImGui::EndTooltip();
    }

    ImGui::EndGroup();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    const float pad = 6.0f * scale;
    ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                        IM_COL32(255, 165, 0, 140), 8.0f, 0, 1.2f);
    ImGui::Spacing();
  }

  // ───────────────────────────────────────────────
  //  도로 차단 - 건녕 ↔ 교지
  //  root + 0x7E30 = 건녕→교지
  //  root + 0x8368 = 교지→건녕
  //
  //  도로 차단 - 교지 ↔ 회계
  //  root + 0x8370 = 교지→회계
  //  root + 0x7648 = 회계→교지
  //
  //  100ms마다 0 유지
  // ───────────────────────────────────────────────

  bool g_roadBlockRunning = false;

  static bool g_road1Enabled = false;
  static bool g_road2Enabled = false;
  static uint64_t g_road1OrigVal1 = 0;
  static uint64_t g_road1OrigVal2 = 0;
  static uint64_t g_road2OrigVal1 = 0;
  static uint64_t g_road2OrigVal2 = 0;
  static HANDLE g_roadThread = nullptr;

  static uintptr_t ResolveRoadRoot() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    uintptr_t p = *(uintptr_t *)(exeBase + 0x034C8630);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x8);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x10);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;

    return p;
  }

  static void WriteZeroToAddr(uintptr_t addr) {
    if (!IsValidPtr(addr, 8))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
    *(uint64_t *)addr = 0;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static void RestoreAddr(uintptr_t addr, uint64_t origVal) {
    if (!IsValidPtr(addr, 8))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
    *(uint64_t *)addr = origVal;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static DWORD WINAPI RoadBlockThread(LPVOID) {
    while (g_road1Enabled || g_road2Enabled) {
      Sleep(100);
      if (!g_road1Enabled && !g_road2Enabled)
        break;

      uintptr_t root = ResolveRoadRoot();
      if (!root)
        continue;

      if (g_road1Enabled) {
        WriteZeroToAddr(root + 0x7E30);
        WriteZeroToAddr(root + 0x8368);
      }
      if (g_road2Enabled) {
        WriteZeroToAddr(root + 0x8370);
        WriteZeroToAddr(root + 0x7648);
      }
    }

    g_roadBlockRunning = false;
    return 0;
  }

  static void StartRoadThread() {
    if (!g_roadThread) {
      g_roadBlockRunning = true;
      g_roadThread = CreateThread(nullptr, 0, RoadBlockThread, nullptr, 0, nullptr);
    }
  }

  static void StopRoadThread() {
    if (g_roadThread) {
      if (!g_road1Enabled && !g_road2Enabled) {
        WaitForSingleObject(g_roadThread, 500);
        CloseHandle(g_roadThread);
        g_roadThread = nullptr;
      }
    }
  }

  void SetRoadBlock(bool enable) {
    if (enable) {
      if (g_road1Enabled)
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (건녕↔교지)");
        return;
      }

      g_road1OrigVal1 = *(uint64_t *)(root + 0x7E30);
      g_road1OrigVal2 = *(uint64_t *)(root + 0x8368);

      g_road1Enabled = true;
      StartRoadThread();
      AddLog(u8"[도로차단] 건녕↔교지 차단 활성화");
    } else {
      if (!g_road1Enabled)
        return;
      g_road1Enabled = false;

      uintptr_t root = ResolveRoadRoot();
      if (root) {
        RestoreAddr(root + 0x7E30, g_road1OrigVal1);
        RestoreAddr(root + 0x8368, g_road1OrigVal2);
      }
      StopRoadThread();
      AddLog(u8"[도로차단] 건녕↔교지 차단 해제");
    }
  }

  void SetRoadBlock2(bool enable) {
    if (enable) {
      if (g_road2Enabled)
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (교지↔회계)");
        return;
      }

      g_road2OrigVal1 = *(uint64_t *)(root + 0x8370);
      g_road2OrigVal2 = *(uint64_t *)(root + 0x7648);

      g_road2Enabled = true;
      StartRoadThread();
      AddLog(u8"[도로차단] 교지↔회계 차단 활성화");
    } else {
      if (!g_road2Enabled)
        return;
      g_road2Enabled = false;

      uintptr_t root = ResolveRoadRoot();
      if (root) {
        RestoreAddr(root + 0x8370, g_road2OrigVal1);
        RestoreAddr(root + 0x7648, g_road2OrigVal2);
      }
      StopRoadThread();
      AddLog(u8"[도로차단] 교지↔회계 차단 해제");
    }
  }
} // namespace DX11Base
