#include "AIBattleResultMonitor.h"

#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../Civilian/CityData.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"
#include "../Officer/RoninMonitor.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kRecruitReleaseAOffset = 0x01E599BE;
    constexpr uintptr_t kRecruitReleaseBOffset = 0x01E5A82C;

    constexpr uint8_t kOriginalA[2] = {0x74, 0x75}; // je +75
    constexpr uint8_t kOriginalB[2] = {0x75, 0x59}; // jne +59

    constexpr uint8_t kStateCouncil = 0x05;
    constexpr uint8_t kStateDomestic = 0x07;
    constexpr uintptr_t kOfficerStride = 0x3D0;
    constexpr int kOfficerCount = 5102;
    constexpr int kRegisterCount = 16;
    constexpr int kStackCount = 16;
    constexpr int kSnapshotCount = 64;

    const char *kRegisterNames[kRegisterCount] = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"};

    struct BreakpointTarget {
      uintptr_t offset = 0;
      uint8_t original[2]{};
      const char *label = nullptr;
    };

    BreakpointTarget g_targets[2] = {
        {kRecruitReleaseAOffset, {kOriginalA[0], kOriginalA[1]}, "A"},
        {kRecruitReleaseBOffset, {kOriginalB[0], kOriginalB[1]}, "B"},
    };

    struct RawSnapshot {
      volatile LONG ready = 0;
      LONG sequence = 0;
      int targetIndex = -1;
      DWORD threadId = 0;
      uint8_t gameState = 0;
      uint64_t rflags = 0;
      uint64_t regs[kRegisterCount]{};
      uint64_t stack[kStackCount]{};
    };

    struct OfficerCandidate {
      const char *source = nullptr;
      uintptr_t raw = 0;
      uintptr_t base = 0;
      uintptr_t innerOffset = 0;
      uint16_t id = 0;
      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
    };

    std::atomic<bool> g_enabled{false};
    std::atomic<uintptr_t> g_cachedGameBase{0};
    uintptr_t g_exeBase = 0;
    uintptr_t g_targetAddresses[2]{};
    PVOID g_vehHandle = nullptr;
    volatile LONG g_writeSequence = 0;
    RawSnapshot g_snapshots[kSnapshotCount]{};
    thread_local int g_singleStepTarget = -1;

    uintptr_t g_rosterBase = 0;
    uintptr_t g_cityBase = 0;
    uint8_t g_lastRelevantState = 0;
    std::vector<std::string> g_pendingPopupLines;

    bool ReadBytes(uintptr_t address, void *out, size_t size) {
      if (!out || !size || address <= 0x10000)
        return false;
      __try {
        memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool ReadByte(uintptr_t address, uint8_t *out) {
      return ReadBytes(address, out, sizeof(*out));
    }

    bool ReadWord(uintptr_t address, uint16_t *out) {
      return ReadBytes(address, out, sizeof(*out));
    }

    bool ReadPtr(uintptr_t address, uintptr_t *out) {
      return ReadBytes(address, out, sizeof(*out)) && *out > 0x10000;
    }

    bool WriteByte(uintptr_t address, uint8_t value) {
      if (address <= 0x10000)
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address), 1,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool ok = false;
      __try {
        *reinterpret_cast<volatile uint8_t *>(address) = value;
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), 1, oldProtect, &ignored);
      if (ok) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(address), 1);
      }
      return ok;
    }

    bool TargetHasOriginalBytes(int index) {
      if (index < 0 || index >= 2)
        return false;
      uint8_t current[2]{};
      return ReadBytes(g_targetAddresses[index], current, sizeof(current)) &&
             memcmp(current, g_targets[index].original, sizeof(current)) == 0;
    }

    void CaptureSnapshot(int targetIndex, PCONTEXT ctx) {
      if (!ctx || targetIndex < 0 || targetIndex >= 2)
        return;

      const LONG seq = InterlockedIncrement(&g_writeSequence);
      RawSnapshot &snap = g_snapshots[(seq - 1) % kSnapshotCount];
      InterlockedExchange(&snap.ready, 0);

      snap.sequence = seq;
      snap.targetIndex = targetIndex;
      snap.threadId = GetCurrentThreadId();
      snap.rflags = ctx->EFlags;

      snap.regs[0] = ctx->Rax;
      snap.regs[1] = ctx->Rbx;
      snap.regs[2] = ctx->Rcx;
      snap.regs[3] = ctx->Rdx;
      snap.regs[4] = ctx->Rsi;
      snap.regs[5] = ctx->Rdi;
      snap.regs[6] = ctx->Rbp;
      snap.regs[7] = ctx->Rsp;
      snap.regs[8] = ctx->R8;
      snap.regs[9] = ctx->R9;
      snap.regs[10] = ctx->R10;
      snap.regs[11] = ctx->R11;
      snap.regs[12] = ctx->R12;
      snap.regs[13] = ctx->R13;
      snap.regs[14] = ctx->R14;
      snap.regs[15] = ctx->R15;

      memset(snap.stack, 0, sizeof(snap.stack));
      if (ctx->Rsp > 0x10000) {
        __try {
          const uint64_t *stack = reinterpret_cast<const uint64_t *>(ctx->Rsp);
          for (int i = 0; i < kStackCount; ++i)
            snap.stack[i] = stack[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          memset(snap.stack, 0, sizeof(snap.stack));
        }
      }

      snap.gameState = 0;
      const uintptr_t gameBase = g_cachedGameBase.load(std::memory_order_relaxed);
      if (gameBase > 0x10000) {
        __try {
          snap.gameState = *reinterpret_cast<const uint8_t *>(gameBase + 0xD0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          snap.gameState = 0;
        }
      }

      InterlockedExchange(&snap.ready, 1);
    }

    LONG CALLBACK AIBattleResultVeh(PEXCEPTION_POINTERS ep) {
      if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

      const DWORD code = ep->ExceptionRecord->ExceptionCode;
      PCONTEXT ctx = ep->ContextRecord;

      if (code == EXCEPTION_BREAKPOINT && g_enabled.load(std::memory_order_relaxed)) {
        const uintptr_t address =
            reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);

        int targetIndex = -1;
        for (int i = 0; i < 2; ++i) {
          if (address == g_targetAddresses[i]) {
            targetIndex = i;
            break;
          }
        }

        if (targetIndex >= 0) {
          CaptureSnapshot(targetIndex, ctx);

          // 원래 2-byte 조건분기의 첫 바이트만 잠시 복구하고 한 명령만 실행합니다.
          if (!WriteByte(address, g_targets[targetIndex].original[0]))
            return EXCEPTION_CONTINUE_SEARCH;

          ctx->Rip = address;
          ctx->EFlags |= 0x100u; // Trap Flag: 원본 명령 1개 실행 후 single-step
          g_singleStepTarget = targetIndex;
          return EXCEPTION_CONTINUE_EXECUTION;
        }
      }

      if (code == EXCEPTION_SINGLE_STEP && g_singleStepTarget >= 0) {
        const int targetIndex = g_singleStepTarget;
        g_singleStepTarget = -1;

        if (g_enabled.load(std::memory_order_relaxed) &&
            targetIndex >= 0 && targetIndex < 2) {
          WriteByte(g_targetAddresses[targetIndex], 0xCC);
        }

        ctx->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
      }

      return EXCEPTION_CONTINUE_SEARCH;
    }

    bool ResolveRosterBase() {
      if (g_rosterBase > 0x10000)
        return true;
      if (!g_exeBase)
        return false;

      uintptr_t base = 0;
      if (!TryResolveOfficerRosterArrayBase(g_exeBase, &base) ||
          base <= 0x10000)
        return false;

      g_rosterBase = base;
      return true;
    }

    uintptr_t ResolveCityBase() {
      if (g_cityBase > 0x10000)
        return g_cityBase;
      if (!g_exeBase)
        return 0;

      uintptr_t pp1 = 0;
      uintptr_t pp2 = 0;
      uintptr_t base = 0;
      if (!ReadPtr(g_exeBase + 0x34C8630, &pp1) ||
          !ReadPtr(pp1, &pp2) ||
          !ReadPtr(pp2, &base))
        return 0;

      g_cityBase = base;
      return g_cityBase;
    }

    bool ResolveOfficerCandidate(const char *source, uintptr_t raw,
                                 OfficerCandidate *out) {
      if (!out || !ResolveRosterBase() || raw < g_rosterBase)
        return false;

      const uintptr_t totalSize =
          static_cast<uintptr_t>(kOfficerCount) * kOfficerStride;
      if (raw >= g_rosterBase + totalSize)
        return false;

      const uintptr_t delta = raw - g_rosterBase;
      const uintptr_t index = delta / kOfficerStride;
      const uintptr_t base = g_rosterBase + index * kOfficerStride;
      const uintptr_t inner = raw - base;

      uint16_t id = 0;
      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
      if (!ReadWord(base + 0x08, &id) || id < 1 || id > kOfficerCount ||
          !ReadByte(base + 0x10, &status)) {
        return false;
      }

      // 세력/도시는 0일 수 있으므로 실패해도 후보 자체는 유지합니다.
      ReadBytes(base + 0x18, &forcePtr, sizeof(forcePtr));
      ReadBytes(base + 0x20, &cityPtr, sizeof(cityPtr));

      out->source = source;
      out->raw = raw;
      out->base = base;
      out->innerOffset = inner;
      out->id = id;
      out->status = status;
      out->forcePtr = forcePtr;
      out->cityPtr = cityPtr;
      return true;
    }

    std::string OfficerName(uint16_t id) {
      auto it = g_officerNames.find(static_cast<int>(id));
      if (it != g_officerNames.end() && !it->second.empty())
        return it->second;
      return u8"ID " + std::to_string(static_cast<unsigned>(id));
    }

    std::string ForceName(uintptr_t forcePtr) {
      if (forcePtr <= 0x10000)
        return u8"무세력";

      uintptr_t lordPtr = 0;
      uint16_t lordId = 0;
      if (ReadPtr(forcePtr + 0xC0, &lordPtr) &&
          ReadWord(lordPtr + 0x08, &lordId) &&
          lordId >= 1 && lordId <= kOfficerCount) {
        return OfficerName(lordId) + u8" 세력";
      }

      char buf[48]{};
      sprintf_s(buf, "0x%llX",
                static_cast<unsigned long long>(forcePtr));
      return std::string(u8"세력 ") + buf;
    }

    std::string CityName(uintptr_t cityPtr) {
      const uintptr_t cityBase = ResolveCityBase();
      if (cityBase <= 0x10000 || cityPtr < cityBase)
        return u8"도시 미확인";

      const uintptr_t delta = cityPtr - cityBase;
      if ((delta % 0x2A0) != 0)
        return u8"도시 미확인";

      const int index = static_cast<int>(delta / 0x2A0);
      if (index < 0 || index >= g_CityCount)
        return u8"도시 미확인";
      return g_CityList[index].cityname;
    }

    void AddUniqueCandidate(std::vector<OfficerCandidate> &out,
                            const OfficerCandidate &candidate) {
      for (const auto &existing : out) {
        if (existing.base == candidate.base)
          return;
      }
      out.push_back(candidate);
    }

    std::vector<OfficerCandidate> FindCandidates(const RawSnapshot &snap) {
      std::vector<OfficerCandidate> result;
      result.reserve(8);

      for (int i = 0; i < kRegisterCount; ++i) {
        OfficerCandidate candidate;
        if (ResolveOfficerCandidate(kRegisterNames[i],
                                    static_cast<uintptr_t>(snap.regs[i]),
                                    &candidate)) {
          AddUniqueCandidate(result, candidate);
        }
      }

      static const char *kStackLabels[kStackCount] = {
          "STK+00", "STK+08", "STK+10", "STK+18",
          "STK+20", "STK+28", "STK+30", "STK+38",
          "STK+40", "STK+48", "STK+50", "STK+58",
          "STK+60", "STK+68", "STK+70", "STK+78"};

      for (int i = 0; i < kStackCount; ++i) {
        OfficerCandidate candidate;
        if (ResolveOfficerCandidate(kStackLabels[i],
                                    static_cast<uintptr_t>(snap.stack[i]),
                                    &candidate)) {
          AddUniqueCandidate(result, candidate);
        }
      }

      return result;
    }

    std::string BuildPopupLine(const RawSnapshot &snap,
                               const std::vector<OfficerCandidate> &candidates) {
      const int targetIndex = snap.targetIndex;
      const bool zf = (snap.rflags & 0x40u) != 0;
      const bool jumpTaken =
          targetIndex == 0 ? zf : !zf;

      std::ostringstream line;
      line << (targetIndex == 0 ? "A(+1E599BE)" : "B(+1E5A82C)")
           << " " << (jumpTaken ? "JUMP" : "FALL");

      if (candidates.empty()) {
        line << u8" : 장수 포인터 후보 없음";
        return line.str();
      }

      line << " : ";
      const size_t limit = (std::min<size_t>)(candidates.size(), 4);
      for (size_t i = 0; i < limit; ++i) {
        if (i)
          line << " / ";
        line << candidates[i].source << "="
             << OfficerName(candidates[i].id)
             << "#" << candidates[i].id;
      }
      if (candidates.size() > limit)
        line << u8" / ...";
      return line.str();
    }

    void ProcessSnapshot(const RawSnapshot &snap) {
      if (snap.targetIndex < 0 || snap.targetIndex >= 2)
        return;

      LoadOfficerNames();
      const auto candidates = FindCandidates(snap);
      const bool zf = (snap.rflags & 0x40u) != 0;
      const bool jumpTaken =
          snap.targetIndex == 0 ? zf : !zf;

      AddLog(
          u8"[AI전투결과DBG] %s +%llX State:0x%02X ZF:%d 원본분기:%s Thread:%lu 후보:%zu",
          g_targets[snap.targetIndex].label,
          static_cast<unsigned long long>(g_targets[snap.targetIndex].offset),
          static_cast<unsigned>(snap.gameState),
          zf ? 1 : 0,
          jumpTaken ? "JUMP" : "FALL",
          static_cast<unsigned long>(snap.threadId),
          candidates.size());

      if (candidates.empty()) {
        AddLog(u8"[AI전투결과DBG]   직접 장수 포인터 후보 없음 (레지스터 + 스택 0x80 검사)");
      } else {
        for (const auto &candidate : candidates) {
          AddLog(
              u8"[AI전투결과DBG]   %s=%p -> %s(ID:%u) +%llX 상태:0x%02X / %s / %s",
              candidate.source,
              reinterpret_cast<void *>(candidate.raw),
              OfficerName(candidate.id).c_str(),
              static_cast<unsigned>(candidate.id),
              static_cast<unsigned long long>(candidate.innerOffset),
              static_cast<unsigned>(candidate.status),
              ForceName(candidate.forcePtr).c_str(),
              CityName(candidate.cityPtr).c_str());
        }
      }

      if (g_pendingPopupLines.size() < 24)
        g_pendingPopupLines.push_back(BuildPopupLine(snap, candidates));
    }

    void DrainSnapshots() {
      for (auto &snap : g_snapshots) {
        if (InterlockedCompareExchange(&snap.ready, 0, 0) != 1)
          continue;

        RawSnapshot local{};
        local.sequence = snap.sequence;
        local.targetIndex = snap.targetIndex;
        local.threadId = snap.threadId;
        local.gameState = snap.gameState;
        local.rflags = snap.rflags;
        memcpy(local.regs, snap.regs, sizeof(local.regs));
        memcpy(local.stack, snap.stack, sizeof(local.stack));

        if (InterlockedExchange(&snap.ready, 0) == 1)
          ProcessSnapshot(local);
      }
    }

    uint8_t ReadRelevantState(uintptr_t gameBase) {
      if (gameBase <= 0x10000)
        return 0;

      uint8_t state = 0;
      if (!ReadByte(gameBase + 0xD0, &state))
        return 0;
      return (state == kStateCouncil || state == kStateDomestic) ? state : 0;
    }

    void HandleStateTransition(uint8_t state) {
      if (!state)
        return;

      if (!g_lastRelevantState) {
        g_lastRelevantState = state;
        return;
      }
      if (state == g_lastRelevantState)
        return;

      const uint8_t previous = g_lastRelevantState;
      g_lastRelevantState = state;

      AddLog(u8"[AI전투결과DBG] 상태 전환 0x%02X -> 0x%02X",
             static_cast<unsigned>(previous),
             static_cast<unsigned>(state));

      if (previous == kStateCouncil && state == kStateDomestic) {
        AddLog(u8"[AI전투결과DBG] 평정 종료 -> 내정 진입 / 누적 메시지 이벤트 %zu개",
               g_pendingPopupLines.size());

        if (!g_pendingPopupLines.empty()) {
          RoninMonitor_QueueSharedNotice(
              u8" [ AI 전투 결과 진단 ]",
              g_pendingPopupLines);
          g_pendingPopupLines.clear();
        }
      }
    }

    bool RestoreTargetByte(int index) {
      if (index < 0 || index >= 2 || !g_targetAddresses[index])
        return false;

      uint8_t current = 0;
      if (!ReadByte(g_targetAddresses[index], &current))
        return false;

      if (current == g_targets[index].original[0])
        return true;
      if (current != 0xCC) {
        AddLog(u8"[AI전투결과DBG] 원복 거부: %s +%llX 첫 바이트가 예상값이 아님 (0x%02X)",
               g_targets[index].label,
               static_cast<unsigned long long>(g_targets[index].offset),
               static_cast<unsigned>(current));
        return false;
      }
      return WriteByte(g_targetAddresses[index], g_targets[index].original[0]);
    }
  } // namespace

  bool IsAIBattleResultMonitorApplied() {
    return g_enabled.load(std::memory_order_relaxed);
  }

  bool SetAIBattleResultMonitor(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[AI전투결과DBG] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    g_exeBase = exeBase;
    g_targetAddresses[0] = exeBase + g_targets[0].offset;
    g_targetAddresses[1] = exeBase + g_targets[1].offset;

    if (enable) {
      if (g_enabled.load(std::memory_order_relaxed))
        return true;

      if (!TargetHasOriginalBytes(0) || !TargetHasOriginalBytes(1)) {
        AddLog(u8"[AI전투결과DBG] 적용 거부: 메시지 분기 원본 바이트 불일치");
        return false;
      }

      if (!g_vehHandle) {
        g_vehHandle = AddVectoredExceptionHandler(1, AIBattleResultVeh);
        if (!g_vehHandle) {
          AddLog(u8"[AI전투결과DBG] VEH 등록 실패");
          return false;
        }
      }

      g_rosterBase = 0;
      g_cityBase = 0;
      g_lastRelevantState = 0;
      g_pendingPopupLines.clear();
      g_enabled.store(true, std::memory_order_relaxed);

      if (!WriteByte(g_targetAddresses[0], 0xCC) ||
          !WriteByte(g_targetAddresses[1], 0xCC)) {
        g_enabled.store(false, std::memory_order_relaxed);
        RestoreTargetByte(0);
        RestoreTargetByte(1);
        AddLog(u8"[AI전투결과DBG] 소프트웨어 브레이크포인트 설치 실패");
        return false;
      }

      AddLog(u8"[AI전투결과DBG] Step 1 활성화: +1E599BE / +1E5A82C 원본 분기 보존 진단");
      return true;
    }

    if (!g_enabled.load(std::memory_order_relaxed)) {
      RestoreTargetByte(0);
      RestoreTargetByte(1);
      return true;
    }

    g_enabled.store(false, std::memory_order_relaxed);
    const bool a = RestoreTargetByte(0);
    const bool b = RestoreTargetByte(1);
    g_lastRelevantState = 0;
    g_pendingPopupLines.clear();

    if (!a || !b) {
      AddLog(u8"[AI전투결과DBG] 비활성화 중 원본 바이트 복구 실패");
      return false;
    }

    AddLog(u8"[AI전투결과DBG] Step 1 비활성화 - 원본 분기 복구");
    return true;
  }

  void AIBattleResultMonitor_Tick(uintptr_t gameBase) {
    g_cachedGameBase.store(gameBase, std::memory_order_relaxed);

    if (!g_enabled.load(std::memory_order_relaxed)) {
      g_lastRelevantState = 0;
      return;
    }

    DrainSnapshots();
    HandleStateTransition(ReadRelevantState(gameBase));
  }

} // namespace DX11Base
