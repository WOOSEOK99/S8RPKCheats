#include "RoadBlock.h"
#include "../../Cheats.h"
#include "../../Config.h"
#include "../../Framework/imgui.h"
#include "../../NotificationManager.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "AIHighHonorSurrenderFix.h"
#include "AIRefusalWarFix.h"
#include <atomic>
#include <mutex>
#include <psapi.h>

namespace DX11Base {

  void AddLog(const char *fmt, ...);
  bool IsValidPtr(uintptr_t addr, SIZE_T size);

  bool bAIWarImprove = false;

  // ───────────────────────────────────────────────
  //  AI 전투 개선
  //  4개의 AI 전쟁 관련 static 패치 + V2.0 공격/항복권고 hook을
  //  하나의 토글로 함께 적용합니다.
  //  기존 +144D24C 2-byte 분기는 V2.0 +144D248 공격 cave가 대체합니다.
  //  주인공 소속 도시 보정, 고의리 군주 항복 억제,
  //  항복권고 실패 후 3개월 공격 우선/재권고 차단을 포함합니다.
  //  모든 주소가 원본/패치 바이트 중 하나와 일치하는지 먼저 검증한 뒤 적용합니다.
  // ───────────────────────────────────────────────
  static const unsigned char kAIWarHeroAggroOriginal[] = {0x75, 0x1E};
  static const unsigned char kAIWarHeroAggroEnabled[] = {0x90, 0x90};

  // SAN8RPK AI V2.0 - 주인공 소속 도시 AI 보정.
  // PLAYER_FORCE_GRACE_RVA: 별도 공격 유예 조건을 제거.
  static const unsigned char kAIWarPlayerForceGraceOriginal[] = {0x0F, 0x45, 0xFD};
  static const unsigned char kAIWarPlayerForceGraceEnabled[] = {0x90, 0x90, 0x90};

  // PLAYER_CITY_THRESHOLD_RVA: 주인공 소속 도시만 다른 공격 임계값 경로를 타는 분기를
  // 일반 AI 경로와 동일하게 건너뛰도록 보정.
  static const unsigned char kAIWarPlayerCityThresholdOriginal[] = {0x74, 0x0D};
  static const unsigned char kAIWarPlayerCityThresholdEnabled[] = {0xEB, 0x0D};

  static const unsigned char kAIWarEmptyCityOriginal[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
  static const unsigned char kAIWarEmptyCityEnabled[] = {0xB8, 0x00, 0x00, 0x00, 0x00};

  struct AIWarPatchSpec {
    uintptr_t offset;
    const unsigned char *original;
    const unsigned char *enabled;
    SIZE_T size;
  };

  static const AIWarPatchSpec kAIWarPatches[] = {
      {0x1464B91, kAIWarHeroAggroOriginal, kAIWarHeroAggroEnabled, sizeof(kAIWarHeroAggroOriginal)},
      {0x1464B83, kAIWarPlayerForceGraceOriginal, kAIWarPlayerForceGraceEnabled, sizeof(kAIWarPlayerForceGraceOriginal)},
      {0x145AEE6, kAIWarPlayerCityThresholdOriginal, kAIWarPlayerCityThresholdEnabled, sizeof(kAIWarPlayerCityThresholdOriginal)},
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

    // OFF 요청에서는 복합 hook부터 안전하게 해제합니다.
    // 하나라도 실패하면 static patch는 건드리지 않아 상태 불일치를 막습니다.
    if (!enable) {
      if (IsAIRefusalWarFixApplied() && !SetAIRefusalWarFix(false)) {
        AddLog(u8"[AI전투] 항복권고 공격전환 hook 해제 실패 - 기존 ON 상태 유지");
        bAIWarImprove = true;
        return;
      }

      if (IsAIHighHonorSurrenderFixApplied() &&
          !SetAIHighHonorSurrenderFix(false)) {
        AddLog(u8"[AI전투] 고의리 항복 hook 해제 실패 - 기존 ON 상태 복구 시도");
        SetAIRefusalWarFix(true);
        bAIWarImprove = true;
        return;
      }
    }

    AIWarPatchSnapshot snapshots[_countof(kAIWarPatches)]{};

    // 먼저 4개 static 주소를 모두 검증합니다. 하나라도 예상과 다르면 아무 것도 쓰지 않습니다.
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

        // OFF 요청에서 static 복구가 실패했다면 앞서 해제한 항복 hook도
        // 가능한 경우 다시 활성화하여 기존 ON 상태를 복원합니다.
        if (!enable) {
          if (!IsAIHighHonorSurrenderFixApplied())
            SetAIHighHonorSurrenderFix(true);
          if (!IsAIRefusalWarFixApplied())
            SetAIRefusalWarFix(true);
        }

        bAIWarImprove = !enable;
        return;
      }
    }

    if (enable) {
      // 먼저 공격/항복권고 실패 처리 cave를 설치합니다.
      // 이 hook이 +144D248을 소유하므로 기존 +144D24C 2-byte 패치는 사용하지 않습니다.
      if (!SetAIRefusalWarFix(true)) {
        for (size_t i = 0; i < _countof(kAIWarPatches); ++i) {
          if (snapshots[i].needsWrite)
            WriteAIWarPatchBytes(snapshots[i].address,
                                 snapshots[i].bytes,
                                 kAIWarPatches[i].size);
        }

        AddLog(u8"[AI전투] 항복권고 공격전환 hook 적용 실패 (static 변경분 롤백)");
        bAIWarImprove = false;
        return;
      }

      if (!SetAIHighHonorSurrenderFix(true)) {
        SetAIRefusalWarFix(false);

        for (size_t i = 0; i < _countof(kAIWarPatches); ++i) {
          if (snapshots[i].needsWrite)
            WriteAIWarPatchBytes(snapshots[i].address,
                                 snapshots[i].bytes,
                                 kAIWarPatches[i].size);
        }

        AddLog(u8"[AI전투] 고의리 항복 hook 적용 실패 (전체 변경분 롤백)");
        bAIWarImprove = false;
        return;
      }

      AddLog(u8"[AI전투] 전투 개선 활성화: +144D248 공격 cave, +1464B91, +1464B83, +145AEE6, +145BDAB, 항복권고");
    } else {
      AddLog(u8"[AI전투] 전투 개선 비활성화: 4개 static + 공격/항복권고 hook 원본 복구");
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
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"AI 세력의 전쟁 행동과 항복권고 판단을 함께 조정합니다.");
      ImGui::TextUnformatted(u8"- 공격 후보 확장 및 기존 목표 우선도 보정");
      ImGui::TextUnformatted(u8"- 주인공 대상 호전성 증가 분기 제거");
      ImGui::TextUnformatted(u8"- 주인공이 군주가 아닐 때 소속 도시의 별도 공격 유예 조건 제거");
      ImGui::TextUnformatted(u8"- 주인공 소속 도시의 공격 임계값을 일반 AI와 같은 경로로 보정");
      ImGui::TextUnformatted(u8"- 일부 군주의 공백지 점령 제한 플래그 무력화");
      ImGui::TextUnformatted(u8"- 의리가 높은 AI 군주는 항복권고를 더 잘 거부하도록 보정");
      ImGui::TextUnformatted(u8"- 항복권고 실패 후 3개월 동안 같은 대상 재권고를 막고 공격 우선도 상승");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ static 패치 또는 관련 hook이 예상 상태와 다르면 전체 적용을 보류합니다.");
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

  static std::atomic<bool> g_road1Enabled{false};
  static std::atomic<bool> g_road2Enabled{false};
  static uint64_t g_road1OrigVal1 = 0;
  static uint64_t g_road1OrigVal2 = 0;
  static uint64_t g_road2OrigVal1 = 0;
  static uint64_t g_road2OrigVal2 = 0;
  static HANDLE g_roadThread = nullptr;
  static HANDLE g_roadStopEvent = nullptr;
  static std::mutex g_roadLifecycleMutex;
  static std::mutex g_roadWriteMutex;

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

    // 이미 차단값이면 보호 변경과 쓰기를 모두 생략합니다.
    if (*(uint64_t *)addr == 0)
      return;

    DWORD old = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old))
      return;
    *(uint64_t *)addr = 0;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static void RestoreAddr(uintptr_t addr, uint64_t origVal) {
    if (!IsValidPtr(addr, 8))
      return;

    if (*(uint64_t *)addr == origVal)
      return;

    DWORD old = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old))
      return;
    *(uint64_t *)addr = origVal;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static DWORD WINAPI RoadBlockThread(LPVOID) {
    while (true) {
      const DWORD waitResult = WaitForSingleObject(g_roadStopEvent, 100);
      if (waitResult == WAIT_OBJECT_0)
        break;
      if (waitResult == WAIT_FAILED) {
        AddLog(u8"[도로차단] stop event 대기 실패. worker를 종료합니다.");
        break;
      }

      if (!g_road1Enabled.load(std::memory_order_acquire) &&
          !g_road2Enabled.load(std::memory_order_acquire))
        break;

      uintptr_t root = ResolveRoadRoot();
      if (!root)
        continue;

      // OFF 복구와 동일 mutex를 사용합니다. 플래그는 mutex 진입 후 다시 확인하므로
      // 복구가 끝난 주소를 이전 worker iteration이 다시 0으로 덮지 못합니다.
      std::lock_guard<std::mutex> writeLock(g_roadWriteMutex);
      if (g_road1Enabled.load(std::memory_order_acquire)) {
        WriteZeroToAddr(root + 0x7E30);
        WriteZeroToAddr(root + 0x8368);
      }
      if (g_road2Enabled.load(std::memory_order_acquire)) {
        WriteZeroToAddr(root + 0x8370);
        WriteZeroToAddr(root + 0x7648);
      }
    }

    g_roadBlockRunning = false;
    return 0;
  }

  static bool StartRoadThreadLocked() {
    if (g_roadThread) {
      if (WaitForSingleObject(g_roadThread, 0) == WAIT_TIMEOUT)
        return true;

      CloseHandle(g_roadThread);
      g_roadThread = nullptr;
      if (g_roadStopEvent) {
        CloseHandle(g_roadStopEvent);
        g_roadStopEvent = nullptr;
      }
    }

    g_roadStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_roadStopEvent) {
      g_roadBlockRunning = false;
      AddLog(u8"[도로차단] stop event 생성 실패");
      return false;
    }

    g_roadThread = CreateThread(nullptr, 0, RoadBlockThread, nullptr, 0, nullptr);
    if (!g_roadThread) {
      CloseHandle(g_roadStopEvent);
      g_roadStopEvent = nullptr;
      g_roadBlockRunning = false;
      AddLog(u8"[도로차단] worker 생성 실패");
      return false;
    }

    g_roadBlockRunning = true;
    return true;
  }

  static void StopRoadThreadIfUnusedLocked() {
    if (g_road1Enabled.load(std::memory_order_acquire) ||
        g_road2Enabled.load(std::memory_order_acquire))
      return;

    if (g_roadStopEvent)
      SetEvent(g_roadStopEvent);

    if (g_roadThread) {
      // 100ms Sleep을 기다리지 않습니다. event가 즉시 worker를 깨우므로
      // 진행 중인 짧은 메모리 iteration만 끝나면 반환합니다.
      WaitForSingleObject(g_roadThread, INFINITE);
      CloseHandle(g_roadThread);
      g_roadThread = nullptr;
    }

    if (g_roadStopEvent) {
      CloseHandle(g_roadStopEvent);
      g_roadStopEvent = nullptr;
    }
    g_roadBlockRunning = false;
  }

  void SetRoadBlock(bool enable) {
    std::lock_guard<std::mutex> lifecycleLock(g_roadLifecycleMutex);

    if (enable) {
      if (g_road1Enabled.load(std::memory_order_acquire))
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root || !IsValidPtr(root + 0x7E30, 8) || !IsValidPtr(root + 0x8368, 8)) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (건녕↔교지)");
        return;
      }

      {
        std::lock_guard<std::mutex> writeLock(g_roadWriteMutex);
        g_road1OrigVal1 = *(uint64_t *)(root + 0x7E30);
        g_road1OrigVal2 = *(uint64_t *)(root + 0x8368);
      }

      g_road1Enabled.store(true, std::memory_order_release);
      if (!StartRoadThreadLocked()) {
        g_road1Enabled.store(false, std::memory_order_release);
        return;
      }
      AddLog(u8"[도로차단] 건녕↔교지 차단 활성화");
    } else {
      if (!g_road1Enabled.exchange(false, std::memory_order_acq_rel))
        return;

      {
        std::lock_guard<std::mutex> writeLock(g_roadWriteMutex);
        uintptr_t root = ResolveRoadRoot();
        if (root) {
          RestoreAddr(root + 0x7E30, g_road1OrigVal1);
          RestoreAddr(root + 0x8368, g_road1OrigVal2);
        }
      }

      StopRoadThreadIfUnusedLocked();
      AddLog(u8"[도로차단] 건녕↔교지 차단 해제");
    }
  }

  void SetRoadBlock2(bool enable) {
    std::lock_guard<std::mutex> lifecycleLock(g_roadLifecycleMutex);

    if (enable) {
      if (g_road2Enabled.load(std::memory_order_acquire))
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root || !IsValidPtr(root + 0x8370, 8) || !IsValidPtr(root + 0x7648, 8)) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (교지↔회계)");
        return;
      }

      {
        std::lock_guard<std::mutex> writeLock(g_roadWriteMutex);
        g_road2OrigVal1 = *(uint64_t *)(root + 0x8370);
        g_road2OrigVal2 = *(uint64_t *)(root + 0x7648);
      }

      g_road2Enabled.store(true, std::memory_order_release);
      if (!StartRoadThreadLocked()) {
        g_road2Enabled.store(false, std::memory_order_release);
        return;
      }
      AddLog(u8"[도로차단] 교지↔회계 차단 활성화");
    } else {
      if (!g_road2Enabled.exchange(false, std::memory_order_acq_rel))
        return;

      {
        std::lock_guard<std::mutex> writeLock(g_roadWriteMutex);
        uintptr_t root = ResolveRoadRoot();
        if (root) {
          RestoreAddr(root + 0x8370, g_road2OrigVal1);
          RestoreAddr(root + 0x7648, g_road2OrigVal2);
        }
      }

      StopRoadThreadIfUnusedLocked();
      AddLog(u8"[도로차단] 교지↔회계 차단 해제");
    }
  }
} // namespace DX11Base
