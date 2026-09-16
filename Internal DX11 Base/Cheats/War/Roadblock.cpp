#include "RoadBlock.h"
#include "../../Cheats.h"
#include "../../Config.h"
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
  //  3개의 AI 전쟁 관련 패치를 하나의 토글로 함께 적용합니다.
  //  모든 주소가 원본/패치 바이트 중 하나와 일치하는지 먼저 검증한 뒤 적용합니다.
  // ───────────────────────────────────────────────
  static const unsigned char kAIWarMultiAttackOriginal[] = {0x74, 0x0A};
  static const unsigned char kAIWarMultiAttackEnabled[] = {0xEB, 0x0A};
  static const unsigned char kAIWarHeroAggroOriginal[] = {0x75, 0x1E};
  static const unsigned char kAIWarHeroAggroEnabled[] = {0x90, 0x90};
  static const unsigned char kAIWarEmptyCityOriginal[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
  static const unsigned char kAIWarEmptyCityEnabled[] = {0xB8, 0x00, 0x00, 0x00, 0x00};

  struct AIWarPatchSpec {
    uintptr_t offset;
    const unsigned char *original;
    const unsigned char *enabled;
    SIZE_T size;
  };

  static const AIWarPatchSpec kAIWarPatches[] = {
      {0x144D24C, kAIWarMultiAttackOriginal, kAIWarMultiAttackEnabled, sizeof(kAIWarMultiAttackOriginal)},
      {0x1464B91, kAIWarHeroAggroOriginal, kAIWarHeroAggroEnabled, sizeof(kAIWarHeroAggroOriginal)},
      {0x145BDAB, kAIWarEmptyCityOriginal, kAIWarEmptyCityEnabled, sizeof(kAIWarEmptyCityOriginal)},
  };

  struct AIWarPatchSnapshot {
    uintptr_t address = 0;
    unsigned char bytes[5]{};
    bool needsWrite = false;
  };

  static bool ReadAIWarPatchBytes(uintptr_t addr, unsigned char *out, SIZE_T size) {
    if (!out || size == 0 || size > 5 || !IsValidPtr(addr, size))
      return false;

    __try {
      memcpy(out, (const void *)addr, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      memset(out, 0, size);
      return false;
    }
    return true;
  }

  static bool WriteAIWarPatchBytes(uintptr_t addr, const unsigned char *bytes, SIZE_T size) {
    if (!bytes || size == 0 || !IsValidPtr(addr, size))
      return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect((LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &oldProtect))
      return false;

    bool success = false;
    __try {
      memcpy((void *)addr, bytes, size);
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }

    DWORD ignored = 0;
    VirtualProtect((LPVOID)addr, size, oldProtect, &ignored);
    if (success)
      FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, size);
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

    AIWarPatchSnapshot snapshots[_countof(kAIWarPatches)]{};

    // 먼저 세 주소를 모두 검증합니다. 하나라도 예상과 다르면 아무 것도 쓰지 않습니다.
    for (size_t i = 0; i < _countof(kAIWarPatches); ++i) {
      const AIWarPatchSpec &spec = kAIWarPatches[i];
      AIWarPatchSnapshot &snapshot = snapshots[i];
      snapshot.address = exeBase + spec.offset;

      if (!ReadAIWarPatchBytes(snapshot.address, snapshot.bytes, spec.size)) {
        AddLog(u8"[AI전투] 패치 주소 읽기 실패: SAN8RPK.exe+%llX", (unsigned long long)spec.offset);
        if (enable)
          bAIWarImprove = false;
        return;
      }

      const bool isOriginal = memcmp(snapshot.bytes, spec.original, spec.size) == 0;
      const bool isEnabled = memcmp(snapshot.bytes, spec.enabled, spec.size) == 0;
      if (!isOriginal && !isEnabled) {
        AddLog(u8"[AI전투] 패치 거부: SAN8RPK.exe+%llX 바이트 불일치", (unsigned long long)spec.offset);
        if (enable)
          bAIWarImprove = false;
        return;
      }

      const unsigned char *wanted = enable ? spec.enabled : spec.original;
      snapshot.needsWrite = memcmp(snapshot.bytes, wanted, spec.size) != 0;
    }

    // 검증을 모두 통과한 뒤에만 실제 쓰기를 시작합니다.
    for (size_t i = 0; i < _countof(kAIWarPatches); ++i) {
      if (!snapshots[i].needsWrite)
        continue;

      const AIWarPatchSpec &spec = kAIWarPatches[i];
      const unsigned char *wanted = enable ? spec.enabled : spec.original;
      if (!WriteAIWarPatchBytes(snapshots[i].address, wanted, spec.size)) {
        // 부분 적용 방지: 이번 호출에서 이미 바꾼 주소들을 호출 전 상태로 되돌립니다.
        for (size_t j = 0; j <= i; ++j) {
          if (snapshots[j].needsWrite)
            WriteAIWarPatchBytes(snapshots[j].address, snapshots[j].bytes, kAIWarPatches[j].size);
        }
        AddLog(u8"[AI전투] 패치 쓰기 실패: SAN8RPK.exe+%llX (변경분 롤백)",
               (unsigned long long)spec.offset);
        bAIWarImprove = !enable;
        return;
      }
    }

    if (enable) {
      AddLog(u8"[AI전투] 전투 개선 활성화: +144D24C, +1464B91, +145BDAB");
    } else {
      AddLog(u8"[AI전투] 전투 개선 비활성화: 3개 주소 원본 복구");
    }
  }

  void DrawAIWarImproveSection(float scale) {
    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));

    if (ImGui::Checkbox(u8"AI 전투 개선", &bAIWarImprove)) {
      const bool requested = bAIWarImprove;
      SetAIWarImprove(requested);
      NotifyFeatureToggle(u8"AI 전투 개선", bAIWarImprove);
      SaveConfig();
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"AI 세력의 전쟁 행동 관련 3개 분기를 함께 조정합니다.");
      ImGui::TextUnformatted(u8"- 한 세력의 복수 공격 분기");
      ImGui::TextUnformatted(u8"- 주인공 대상 호전성 증가 분기 제거");
      ImGui::TextUnformatted(u8"- 일부 군주의 공백지 점령 제한 플래그 무력화");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 세 주소 중 하나라도 예상 바이트와 다르면 전체 패치를 적용하지 않습니다.");
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
