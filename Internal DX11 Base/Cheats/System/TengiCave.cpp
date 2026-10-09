#include "../../pch.h"
#include "TengiCave.h"
#include "MonthCapture.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../NotificationManager.h"
#include "../../showlog.h"
#include <psapi.h>
#include <atomic>
#include <string>


namespace DX11Base {
  void TickTengiAllowedFilter();

  // ───────────────────────────────────────────────
  //  전기 주소 캡처 cave
  //  원본: mov [r15+0x1E4B18], al  (7바이트)
  //  r15+0x1E4B18 = 전기 주소를 g_tengiAddr에 저장
  // ───────────────────────────────────────────────

  uintptr_t g_tengiAddr = 0;
  bool g_tengiRunning = false;

  static uintptr_t g_tengiHookAddr = 0;
  static uint8_t g_tengiOriginal[7] = {};
  static uintptr_t g_tengiCaveAddr = 0;
  static bool g_tengiApplied = false;
  static bool g_tengiInstallRequested = false;
  static bool g_tengiDeferredLogged = false;

  namespace {
    constexpr uintptr_t kTengiV0860Rva = 0x1338830;
    constexpr uint8_t kTengiV0860Bytes[7] = {0x41, 0x88, 0x87, 0x18, 0x4B, 0x1E, 0x00};

    bool ResolveKnownTengiHook(uintptr_t exeBase, uintptr_t searchEnd, uintptr_t *outHook) {
      if (!outHook || searchEnd <= exeBase)
        return false;
      const uintptr_t imageSize = searchEnd - exeBase;
      if (imageSize < kTengiV0860Rva + sizeof(kTengiV0860Bytes))
        return false;

      const uintptr_t candidate = exeBase + kTengiV0860Rva;
      if (!IsValidPtr(candidate, sizeof(kTengiV0860Bytes)))
        return false;
      if (memcmp((const void *)candidate, kTengiV0860Bytes,
                 sizeof(kTengiV0860Bytes)) != 0)
        return false;

      *outHook = candidate;
      return true;
    }
  }

  static bool InstallTengiCave(uintptr_t hookAddr) {
    g_tengiCaveAddr = AllocNear(hookAddr, 128);
    if (!g_tengiCaveAddr)
      return false;

    uint8_t *cave = (uint8_t *)g_tengiCaveAddr;
    int idx = 0;

    // push rax
    cave[idx++] = 0x50;

    // lea rax, [r15+0x1E4B18]
    // REX.W + REX.B = 0x49, ModRM = 0x87
    cave[idx++] = 0x49;
    cave[idx++] = 0x8D;
    cave[idx++] = 0x87;
    cave[idx++] = 0x18;
    cave[idx++] = 0x4B;
    cave[idx++] = 0x1E;
    cave[idx++] = 0x00;

    // mov [g_tengiAddr], rax
    cave[idx++] = 0x48;
    cave[idx++] = 0xA3;
    *(uintptr_t *)&cave[idx] = (uintptr_t)&g_tengiAddr;
    idx += 8;

    // pop rax
    cave[idx++] = 0x58;

    // 원본: mov [r15+0x1E4B18], al
    uint8_t orig[] = {0x41, 0x88, 0x87, 0x18, 0x4B, 0x1E, 0x00};
    memcpy(&cave[idx], orig, 7);
    idx += 7;

    // 복귀 점프
    uintptr_t retAddr = hookAddr + 7;
    cave[idx++] = 0xFF;
    cave[idx++] = 0x25;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    cave[idx++] = 0x00;
    *(uintptr_t *)&cave[idx] = retAddr;
    idx += 8;

    return ApplyJmp(hookAddr, g_tengiCaveAddr, 7);
  }

  void SetTengiCapture(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (enable) {
      g_tengiInstallRequested = true;

      // 전기 캡처는 무한 전기 또는 전기취소 위젯에서만 필요합니다.
      // 설정 로드가 상시 SetTengiCapture(true)를 호출하더라도 실제 사용 전에는 cave를 만들지 않습니다.
      if (!bInfTengi && !bShowWidgetTengi) {
        if (!g_tengiDeferredLogged) {
          AddLog(u8"[Tengi] 기능 미사용 상태: 캡처 후크 설치 보류");
          g_tengiDeferredLogged = true;
        }
        return;
      }

      if (g_tengiApplied)
        return;
      if (g_tengiRunning)
        return;

      g_tengiDeferredLogged = false;
      g_tengiRunning = true;

      HANDLE hThread = CreateThread(
          nullptr, 0,
          [](LPVOID) -> DWORD {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
            uintptr_t searchEnd = exeBase + mi.SizeOfImage;

            if (!g_tengiHookAddr) {
              if (ResolveKnownTengiHook(exeBase, searchEnd, &g_tengiHookAddr)) {
                AddLog(u8"[Tengi] V0.860 고정 RVA 검증 성공: +0x%llX",
                       (unsigned long long)kTengiV0860Rva);
              } else {
                g_tengiHookAddr = FindPattern(exeBase, searchEnd, "41 88 87 18 4B 1E 00");
              }
            }

            AddLog("[DEBUG] tengiHook: %p", (void *)g_tengiHookAddr);

            if (g_tengiInstallRequested && g_tengiHookAddr && !g_tengiApplied) {
              memcpy(g_tengiOriginal, (void *)g_tengiHookAddr, 7);
              if (InstallTengiCave(g_tengiHookAddr)) {
                g_tengiApplied = true;
                AddLog("[DEBUG] Tengi hook installed at: %p", (void *)g_tengiHookAddr);
              }
            }

            AddLog("[DEBUG] tengiCave applied: %d", g_tengiApplied);
            g_tengiRunning = false;
            return 0;
          },
          nullptr, 0, nullptr);

      if (hThread)
        CloseHandle(hThread);

    } else {
      g_tengiInstallRequested = false;
      g_tengiDeferredLogged = false;
      g_tengiAddr = 0;

      if (g_tengiApplied) {
        RestoreBytes(g_tengiHookAddr, g_tengiOriginal, 7);
        VirtualFree((LPVOID)g_tengiCaveAddr, 0, MEM_RELEASE);
        g_tengiCaveAddr = 0;
        g_tengiApplied = false;
        g_tengiHookAddr = 0;
      }
    }
  }

  uintptr_t GetTengiHookAddr() { return g_tengiHookAddr; }
  uintptr_t GetTengiHookOffset() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    return g_tengiHookAddr ? (g_tengiHookAddr - exeBase) : 0;
  }
  uintptr_t GetCapturedTengiAddr() { return g_tengiAddr; }

  // 전기 읽기
  uint8_t GetTengi() {
    if (!g_tengiAddr || !IsValidPtr(g_tengiAddr, 1))
      return 0;
    return *(uint8_t*)g_tengiAddr;
  }

  // 전기 쓰기
  void SetTengi(uint8_t value) {
    if (!g_tengiAddr)
      return;

    static uintptr_t s_trustedTengiAddr = 0;
    static uint64_t s_lastTengiValidMs = 0;
    const uint64_t now = GetTickCount64();
    if (g_tengiAddr != s_trustedTengiAddr || (now - s_lastTengiValidMs) >= 500ull) {
      if (!IsValidPtr(g_tengiAddr, 1))
        return;
      s_trustedTengiAddr = g_tengiAddr;
      s_lastTengiValidMs = now;
    }

    // 전기 게이지 값 쓰기
    *(uint8_t*)g_tengiAddr = value;

    // 전기 발생 플래그 동기화 (오프셋 +0x18 -> xxA8)
    uintptr_t flagAddr = g_tengiAddr + 0x18;
    static uintptr_t s_trustedTengiForFlag = 0;
    static uint64_t s_lastFlagValidMs = 0;
    bool flagOk = true;
    if (g_tengiAddr != s_trustedTengiForFlag || (now - s_lastFlagValidMs) >= 500ull) {
      flagOk = IsValidPtr(flagAddr, 1);
      if (flagOk) {
        s_trustedTengiForFlag = g_tengiAddr;
        s_lastFlagValidMs = now;
      }
    }
    if (flagOk && value >= 100) {
      // [2026-04-05] 크래시 방지: +0x08과 +0x10에 이벤트 포인터가 들어왔을 때만 1 셋팅
      uintptr_t ptr1 = *(uintptr_t *)(g_tengiAddr + 0x08);
      uintptr_t ptr2 = *(uintptr_t *)(g_tengiAddr + 0x10);
      if (ptr1 != 0 && ptr2 != 0) {
        *(uint8_t*)flagAddr = 1;
      }
    }
  }

  // 전기 취소 (플래그 0 설정 및 할당된 이벤트 포인터 주소 초기화)
  void CancelTengi() {
    if (!g_tengiAddr || !IsValidPtr(g_tengiAddr + 0x18, 1))
      return;

    // 1. 발생 플래그(+0x18)를 0으로 초기화
    uintptr_t flagAddr = g_tengiAddr + 0x18;
    *(uint8_t*)flagAddr = 0;

    // 2. 전기 이벤트 포인터 1 (+0x08): 유저 요청대로 포인터 0으로 초기화 (64비트 크기인 8바이트를 0으로 밀어 6바이트
    // 모두 0 처리)
    if (IsValidPtr(g_tengiAddr + 0x08, 8)) {
      *(uint64_t *)(g_tengiAddr + 0x08) = 0;
    }

    // 3. 전기 이벤트 포인터 2 (+0x10): 유저 요청대로 포인터 0으로 초기화
    if (IsValidPtr(g_tengiAddr + 0x10, 8)) {
      *(uint64_t *)(g_tengiAddr + 0x10) = 0;
    }
  }

  // --- [신규] 전기 주소 캡처 감시 및 알림 발생 ---
  void TengiCave_Tick() {
      TickTengiAllowedFilter();
      static bool s_notifiedForThisSession = false;

      // 시작 설정 로드에서는 캡처 요청만 기록하고, 실제 기능을 사용할 때 최초 설치합니다.
      if (g_tengiInstallRequested && !g_tengiApplied && !g_tengiRunning &&
          (bInfTengi || bShowWidgetTengi)) {
          SetTengiCapture(true);
      }
      
      // 1. 주소가 캡처되었고 아직 알림을 주지 않았을 때
      if (g_tengiAddr != 0 && !s_notifiedForThisSession) {
          DX11Base::AddNotification(u8"전기 취소 활성화 됨");
          DX11Base::AddLog(u8"[Tengi] 전기 주소 캡처 완료: 0x%llX", (unsigned long long)g_tengiAddr);
          s_notifiedForThisSession = true;
      }
      
      // 2. 주소가 0이 된 경우 (해제 등) 초기화하여 재캡처 시 다시 알림 발생 가능하게 함
      if (g_tengiAddr == 0) {
          s_notifiedForThisSession = false;
      }
  }

  namespace {
    // version.dll 정적 분석: 이름 순서의 이벤트 슬롯 ID (연속 번호가 아님).
    constexpr int kManagedEventSlots[kTengiListEventCount] = {
        1, 2, 7, 8, 9, 10, 12, 14, 16, 18, 19, 20, 21, 22, 23};

    struct TengiDurationOverride {
      uintptr_t address = 0;
      uint16_t original = 0;
      uint16_t written = 0;
      bool active = false;
    };
    TengiDurationOverride s_durationOverrides[kTengiListEventCount] = {};
    ULONGLONG s_lastTengiDurationPollMs = 0;

    bool TryReadEventPointer(uintptr_t address, uintptr_t *value) {
      if (!value || !IsValidPtr(address, sizeof(uintptr_t)))
        return false;
      __try {
        *value = *reinterpret_cast<const uintptr_t *>(address);
        return *value > 0x10000;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *value = 0;
        return false;
      }
    }

    bool TryReadEventDuration(uintptr_t address, uint16_t *value) {
      if (!value || !IsValidPtr(address, sizeof(uint16_t)))
        return false;
      __try {
        *value = *reinterpret_cast<const uint16_t *>(address);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool TryWriteEventDuration(uintptr_t address, uint16_t value) {
      if (!IsValidPtr(address, sizeof(uint16_t)))
        return false;
      DWORD oldProtect = 0, ignored = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address),
                          sizeof(uint16_t), PAGE_READWRITE, &oldProtect))
        return false;
      bool success = true;
      __try {
        *reinterpret_cast<uint16_t *>(address) = value;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }
      VirtualProtect(reinterpret_cast<LPVOID>(address),
                     sizeof(uint16_t), oldProtect, &ignored);
      uint16_t actual = 0;
      return success && TryReadEventDuration(address, &actual) &&
             actual == value;
    }

    // 외부 version.dll 강제 발동 경로:
    // [SAN8RPK.exe + 0x2E98BC8] => 이벤트 관리자,
    // [관리자 + 0x58A670 + 슬롯 * 8] => 전기 객체, 객체 + 0x12 => 지속 개월(u16).
    // 전기 발동·취소 플래그에는 접근하지 않는다.
    uintptr_t ResolveEventDurationAddress(int slot) {
      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      uintptr_t manager = 0;
      uintptr_t eventObject = 0;
      if (!exeBase ||
          !TryReadEventPointer(exeBase + 0x2E98BC8, &manager) ||
          !TryReadEventPointer(manager + 0x58A670 +
                               static_cast<uintptr_t>(slot) * 8, &eventObject))
        return 0;
      const uintptr_t duration = eventObject + 0x12;
      return IsValidPtr(duration, sizeof(uint16_t)) ? duration : 0;
    }
  } // namespace

  namespace {
    // Layout from SAN8RPK.pdb B18A027E-19F4-4C35-8749-5B6F0FF814D8, age 1.
    // DataCenter::m_worldData +0x72C8; WorldData::m_Momentum +0x1E4B18,
    // m_activeTurningPoint +0x1E4B20. Root-relative gauge: +0x1EBDE0.
    // MultiTypeBuffer<TurningPoint,16>::m_isConstructed is buffer +0x10;
    // it describes object lifetime, not a pending-event flag.
    // TurningPoint::m_pData +8; TurningPointData::m_ID +8,
    // m_lastTriggeredDate +0x36; +0x12 is an effect definition value,
    // not remaining time (also saved/restored by version.dll +0x5E260/510).
    constexpr uintptr_t kCycleGaugeOffset = 0x1EBDE0;
    constexpr GUID kCyclePdbGuid = {0xB18A027E, 0x19F4, 0x4C35,
        {0x87, 0x49, 0x5B, 0x6F, 0x0F, 0xF8, 0x14, 0xD8}};

    bool HasVerifiedCycleLayout() {
      const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!base || !IsValidPtr(base, sizeof(IMAGE_DOS_HEADER)))
        return false;
      __try {
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
          return false;
        const uintptr_t ntAddress = base + dos->e_lfanew;
        if (!IsValidPtr(ntAddress, sizeof(IMAGE_NT_HEADERS64)))
          return false;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(ntAddress);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
          return false;
        const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        const DWORD size = nt->OptionalHeader.SizeOfImage;
        if (!dir.VirtualAddress || dir.VirtualAddress >= size ||
            dir.Size > size - dir.VirtualAddress ||
            !IsValidPtr(base + dir.VirtualAddress, dir.Size))
          return false;
        const auto *entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + dir.VirtualAddress);
        for (DWORD i = 0; i < dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
          const auto &entry = entries[i];
          if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < 24 ||
              !entry.AddressOfRawData || entry.AddressOfRawData >= size ||
              entry.SizeOfData > size - entry.AddressOfRawData ||
              !IsValidPtr(base + entry.AddressOfRawData, 24))
            continue;
          const uint8_t *cv = reinterpret_cast<const uint8_t *>(base + entry.AddressOfRawData);
          if (memcmp(cv, "RSDS", 4) == 0 &&
              memcmp(cv + 4, &kCyclePdbGuid, sizeof(GUID)) == 0 &&
              *reinterpret_cast<const DWORD *>(cv + 20) == 1)
            return true;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {}
      return false;
    }


    // PDB: TurningPointTrigger::CanTrigger(TURNINGPOINT_ID,bool) and
    // TurningPointTrigger::CanTrigger(TurningPointData*,bool).
    // Values below are PE RVAs (.text section RVA 0x1000 + CodeView offsets).
    constexpr uintptr_t kCanTriggerByIdRva = 0x132E2F0;
    constexpr uintptr_t kCanTriggerByDataRva = 0x132E340;
    constexpr uintptr_t kFinalTriggerRva = 0x132E920;
    using CanTriggerById = bool (__fastcall *)(int, bool);
    using CanTriggerByData = bool (__fastcall *)(void *, bool);
    using FinalTrigger = bool (__fastcall *)(void *, bool);
    CanTriggerById s_originalCanTriggerById = nullptr;
    CanTriggerByData s_originalCanTriggerByData = nullptr;
    FinalTrigger s_originalFinalTrigger = nullptr;
    bool s_allowedFilterInstalled = false;
    bool s_allowedFilterAttempted = false;
    std::atomic<uint32_t> s_allowedFilterMask{(1u << kTengiListEventCount) - 1u};
    std::atomic<uint32_t> s_rejectedCandidateCount{0};
    std::atomic<uint32_t> s_rejectedFinalCount{0};
    std::atomic<uint32_t> s_seenFinalCount{0};
    std::atomic<uint32_t> s_lastBlockedFinalId{0};
    std::atomic<uint32_t> s_redirectedFinalCount{0};
    std::atomic<uint32_t> s_lastRedirectId{0};
    std::atomic<bool> s_blockedRequestPending{false};

    // All enabled is the legacy no-filter setting. With a restricted list,
    // unknown IDs are rejected rather than silently ignoring the allowlist.
    bool IsTengiIdAllowed(int id) {
      const uint32_t all = (1u << kTengiListEventCount) - 1u;
      const uint32_t mask = s_allowedFilterMask.load(std::memory_order_relaxed);
      if (mask == all)
        return true;
      for (int i = 0; i < kTengiListEventCount; ++i) {
        if (kManagedEventSlots[i] == id)
          return (mask & (1u << i)) != 0;
      }
      return false;
    }

    bool __fastcall CanTriggerByIdDetour(int id, bool special) {
      if (!IsTengiIdAllowed(id)) {
        s_rejectedCandidateCount.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      return s_originalCanTriggerById ? s_originalCanTriggerById(id, special) : false;
    }

    bool __fastcall CanTriggerByDataDetour(void *data, bool special) {
      // Never reinterpret a pointer that the original game has not validated.
      // Null is handed to the original function rather than dereferenced.
      if (data && IsValidPtr(reinterpret_cast<uintptr_t>(data) + 8, sizeof(uint16_t))) {
        uint16_t id = 0;
        bool readable = false;
        __try {
          id = *reinterpret_cast<const uint16_t *>(
              reinterpret_cast<uintptr_t>(data) + 8);
          readable = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (readable && !IsTengiIdAllowed(id)) {
          s_rejectedCandidateCount.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
      }
      return s_originalCanTriggerByData ? s_originalCanTriggerByData(data, special) : false;
    }

    // This is the actual effect-activation path. The PDB mangled symbol
    // ?Trigger@TurningPointTrigger@turning_point@san8r@@SA_NQEAVTurningPointData@3@_N@Z
    // confirms a static bool function, taking (TurningPointData*, bool).
    bool __fastcall FinalTriggerDetour(void *data, bool special) {
      s_seenFinalCount.fetch_add(1, std::memory_order_relaxed);
      const uint32_t fullMask = (1u << kTengiListEventCount) - 1u;
      if (s_allowedFilterMask.load(std::memory_order_relaxed) != fullMask) {
        if (!data || !IsValidPtr(reinterpret_cast<uintptr_t>(data) + 8, sizeof(uint16_t))) {
          s_rejectedFinalCount.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
        uint16_t id = 0;
        __try {
          id = *reinterpret_cast<const uint16_t *>(reinterpret_cast<uintptr_t>(data) + 8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          s_rejectedFinalCount.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
        if (!IsTengiIdAllowed(id)) {
          s_lastBlockedFinalId.store(id, std::memory_order_relaxed);
          const uintptr_t manager = GetScenarioDataCenterAddress();
          if (manager && s_originalCanTriggerByData && s_originalFinalTrigger) {
            const uint32_t mask = s_allowedFilterMask.load(std::memory_order_relaxed);
            for (int i = 0; i < kTengiListEventCount; ++i) {
              if ((mask & (1u << i)) == 0)
                continue;
              const int slot = kManagedEventSlots[i];
              uintptr_t candidate = 0;
              if (!TryReadEventPointer(manager + 0x58A670 +
                         static_cast<uintptr_t>(slot) * 8, &candidate) ||
                  !IsValidPtr(candidate, 0x10))
                continue;
              uint16_t candidateId = 0;
              __try {
                candidateId = *reinterpret_cast<const uint16_t *>(candidate + 8);
              } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
              }
              if (candidateId != slot ||
                  !s_originalCanTriggerByData(reinterpret_cast<void *>(candidate), special))
                continue;
              if (s_originalFinalTrigger(reinterpret_cast<void *>(candidate), special)) {
                s_redirectedFinalCount.fetch_add(1, std::memory_order_relaxed);
                s_lastRedirectId.store(static_cast<uint32_t>(slot), std::memory_order_relaxed);
                return true;
              }
            }
          }
          s_rejectedFinalCount.fetch_add(1, std::memory_order_relaxed);
          s_blockedRequestPending.store(true, std::memory_order_release);
          return false;
        }
      }
      return s_originalFinalTrigger ? s_originalFinalTrigger(data, special) : false;
    }

    bool IsExecutableTengiHookTarget(uintptr_t address) {
      MEMORY_BASIC_INFORMATION mem{};
      if (!IsValidPtr(address, 16) ||
          VirtualQuery(reinterpret_cast<const void *>(address), &mem, sizeof(mem)) != sizeof(mem) ||
          mem.State != MEM_COMMIT || (mem.Protect & PAGE_GUARD) != 0)
        return false;
      return (mem.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    bool InstallTengiAllowedFilter() {
      if (!HasVerifiedCycleLayout())
        return false;
      const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      const uintptr_t byId = base + kCanTriggerByIdRva;
      const uintptr_t byData = base + kCanTriggerByDataRva;
      const uintptr_t finalTrigger = base + kFinalTriggerRva;
      if (!IsExecutableTengiHookTarget(byId) || !IsExecutableTengiHookTarget(byData) ||
          !IsExecutableTengiHookTarget(finalTrigger))
        return false;
      const MH_STATUS init = MH_Initialize();
      if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return false;

      void *idOriginal = nullptr, *dataOriginal = nullptr, *finalOriginal = nullptr;
      const MH_STATUS createdId = MH_CreateHook(
          reinterpret_cast<void *>(byId),
          reinterpret_cast<void *>(&CanTriggerByIdDetour), &idOriginal);
      if (createdId != MH_OK)
        return false;
      const MH_STATUS createdData = MH_CreateHook(
          reinterpret_cast<void *>(byData),
          reinterpret_cast<void *>(&CanTriggerByDataDetour), &dataOriginal);
      if (createdData != MH_OK) {
        MH_RemoveHook(reinterpret_cast<void *>(byId));
        return false;
      }
      const MH_STATUS createdFinal = MH_CreateHook(
          reinterpret_cast<void *>(finalTrigger),
          reinterpret_cast<void *>(&FinalTriggerDetour), &finalOriginal);
      if (createdFinal != MH_OK) {
        MH_RemoveHook(reinterpret_cast<void *>(byData));
        MH_RemoveHook(reinterpret_cast<void *>(byId));
        AddLog(u8"[전기 허용 목록] 최종 Trigger 후크 생성 실패: %s", MH_StatusToString(createdFinal));
        return false;
      }
      s_originalFinalTrigger = reinterpret_cast<FinalTrigger>(finalOriginal);
      s_originalCanTriggerById = reinterpret_cast<CanTriggerById>(idOriginal);
      s_originalCanTriggerByData = reinterpret_cast<CanTriggerByData>(dataOriginal);
      if (MH_EnableHook(reinterpret_cast<void *>(byId)) != MH_OK) {
        MH_RemoveHook(reinterpret_cast<void *>(finalTrigger));
        MH_RemoveHook(reinterpret_cast<void *>(byData));
        MH_RemoveHook(reinterpret_cast<void *>(byId));
        s_originalCanTriggerById = nullptr;
        s_originalCanTriggerByData = nullptr;
        return false;
      }
      if (MH_EnableHook(reinterpret_cast<void *>(byData)) != MH_OK) {
        MH_DisableHook(reinterpret_cast<void *>(byId));
        MH_RemoveHook(reinterpret_cast<void *>(finalTrigger));
        MH_RemoveHook(reinterpret_cast<void *>(byData));
        MH_RemoveHook(reinterpret_cast<void *>(byId));
        s_originalCanTriggerById = nullptr;
        s_originalCanTriggerByData = nullptr;
        return false;
      }
      const MH_STATUS finalStatus = MH_EnableHook(reinterpret_cast<void *>(finalTrigger));
      if (finalStatus != MH_OK) {
        MH_DisableHook(reinterpret_cast<void *>(byData));
        MH_DisableHook(reinterpret_cast<void *>(byId));
        MH_RemoveHook(reinterpret_cast<void *>(finalTrigger));
        MH_RemoveHook(reinterpret_cast<void *>(byData));
        MH_RemoveHook(reinterpret_cast<void *>(byId));
        s_originalCanTriggerById = nullptr;
        s_originalCanTriggerByData = nullptr;
        s_originalFinalTrigger = nullptr;
        AddLog(u8"[전기 허용 목록] 최종 Trigger 후크 활성화 실패: %s", MH_StatusToString(finalStatus));
        return false;
      }
      return true;
    }

    struct TengiCycleSnapshot {
      uintptr_t manager = 0, data = 0;
      int month = -1, start = -1;
      uint16_t id = 0, duration = 0;
      bool constructed = false;
    };

    bool ReadCycleSnapshot(TengiCycleSnapshot *out) {
      __try {
        unsigned short year = 0;
        uint8_t month = 0;
        out->manager = GetScenarioDataCenterAddress();
        if (!out->manager || !ReadScenarioDate(&year, &month) ||
            !year || month < 1 || month > 12 ||
            out->manager != GetScenarioDataCenterAddress())
          return false;
        out->month = static_cast<int>(year) * 12 + month - 1;
        const uintptr_t buffer = out->manager + kCycleGaugeOffset + 8;
        if (!IsValidPtr(buffer, 17))
          return false;
        const uint8_t constructed = *reinterpret_cast<const uint8_t *>(buffer + 16);
        if (constructed > 1)
          return false;
        out->constructed = constructed != 0;
        if (!out->constructed)
          return true; // Destroyed storage: ignore stale buffer pointers.
        if (!TryReadEventPointer(buffer + 8, &out->data) || !IsValidPtr(out->data, 0x3A))
          return false;
        out->id = *reinterpret_cast<const uint16_t *>(out->data + 8);
        uintptr_t registered = 0;
        if (out->id < 1 || out->id > 100 ||
            !TryReadEventPointer(out->manager + 0x58A670 + out->id * 8, &registered) ||
            registered != out->data)
          return false; // Verify actual event ID against the game slot table.
        const unsigned short startYear = *reinterpret_cast<const unsigned short *>(out->data + 0x36);
        const uint8_t startMonth = *reinterpret_cast<const uint8_t *>(out->data + 0x38);
        if (!startYear || startMonth < 1 || startMonth > 12)
          return false;
        out->start = static_cast<int>(startYear) * 12 + startMonth - 1;
        return out->start <= out->month && TryReadEventDuration(out->data + 0x12, &out->duration);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    struct TengiCycleState {
      uintptr_t session = 0, manager = 0, data = 0;
      int lastMonth = -1, handledCouncil = -1, start = -1, earliest = -1;
      bool pending = false, needsEventObservation = false;
    };
    TengiCycleState s_cycle;

    bool RequestCycleTengi(const TengiCycleSnapshot &snapshot) {
      const uintptr_t gauge = snapshot.manager + kCycleGaugeOffset;
      if (!IsValidPtr(gauge, 25))
        return false;
      __try {
        if (*reinterpret_cast<const uint8_t *>(gauge + 24) != 0)
          return false;
        // SetTengi(100) also writes the construction flag. Raise only momentum
        // here; the game must construct the event and process normal expiration.
        *reinterpret_cast<uint8_t *>(gauge) = 100;
        return *reinterpret_cast<const uint8_t *>(gauge) == 100;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
  } // namespace


  // Install once after the game initializes its code. Updates to preferences
  // only change an atomic mask; the game selection logic is never polled/patched.
  void TickTengiAllowedFilter() {
    static ULONGLONG lastAttemptMs = 0;
    static ULONGLONG lastReportMs = 0;
    uint32_t mask = 0;
    for (int i = 0; i < kTengiListEventCount; ++i)
      if (g_tengiListAllowed[i])
        mask |= 1u << i;
    s_allowedFilterMask.store(mask, std::memory_order_relaxed);

    if (!s_allowedFilterAttempted) {
      const ULONGLONG now = GetTickCount64();
      if (lastAttemptMs && now - lastAttemptMs < 2000)
        return;
      lastAttemptMs = now;
      if (!GetScenarioDataCenterAddress())
        return;
      s_allowedFilterAttempted = true;
      s_allowedFilterInstalled = InstallTengiAllowedFilter();
      AddLog(u8"[전기 허용 목록] CanTrigger 두 함수 + 최종 Trigger 후크 %s (현재 허용 마스크=0x%04X)",
             s_allowedFilterInstalled ? u8"설치 성공" : u8"설치 실패, 원본 유지",
             static_cast<unsigned>(mask));
    }

    const ULONGLONG now = GetTickCount64();
    if (s_allowedFilterInstalled && (!lastReportMs || now - lastReportMs >= 5000)) {
      lastReportMs = now;
      const uint32_t count =
          s_rejectedCandidateCount.exchange(0, std::memory_order_relaxed);
      const uint32_t finalCount =
          s_rejectedFinalCount.exchange(0, std::memory_order_relaxed);
      const uint32_t seenCount =
          s_seenFinalCount.exchange(0, std::memory_order_relaxed);
      const uint32_t redirected =
          s_redirectedFinalCount.exchange(0, std::memory_order_relaxed);
      if (count || finalCount || seenCount || redirected)
        AddLog(u8"[전기 허용 목록] 후보 차단 %u, Trigger 진입 %u, 최종 차단 %u(ID %u), 허용 전기 대체 성공 %u(ID %u)",
               static_cast<unsigned>(count), static_cast<unsigned>(seenCount),
               static_cast<unsigned>(finalCount),
               static_cast<unsigned>(s_lastBlockedFinalId.load(std::memory_order_relaxed)),
               static_cast<unsigned>(redirected),
               static_cast<unsigned>(s_lastRedirectId.load(std::memory_order_relaxed)));
    }
  }

  void TickTengiCycle(uintptr_t sessionP1) {
    static ULONGLONG lastPoll = 0;
    static const bool verifiedLayout = HasVerifiedCycleLayout();
    static bool unavailableLogged = false;
    const ULONGLONG now = GetTickCount64();
    if (s_cycle.session != sessionP1) {
      s_cycle = {};
      lastPoll = 0;
    }
    if (!verifiedLayout) {
      if (bInfTengi && !unavailableLogged) {
        AddLog(u8"[Tengi 주기] 검증된 게임 레이아웃 불일치: 강제 발생 차단");
        unavailableLogged = true;
      }
      return;
    }
    if (!sessionP1) {
      s_cycle = {};
      return;
    }
    if (lastPoll && now - lastPoll < 200)
      return;
    lastPoll = now;
    TengiCycleSnapshot snapshot;
    if (!ReadCycleSnapshot(&snapshot)) {
      s_cycle = {}; // Lost observations cannot establish an idle duration barrier.
      s_cycle.session = sessionP1;
      s_cycle.needsEventObservation = true;
      if (bInfTengi && !unavailableLogged) {
        AddLog(u8"[Tengi 주기] 날짜/전기 상태 확인 불가: 강제 발생 차단");
        unavailableLogged = true;
      }
      return;
    }
    unavailableLogged = false;
    if (s_blockedRequestPending.exchange(false, std::memory_order_acq_rel) &&
        !snapshot.constructed) {
      const uintptr_t gauge = snapshot.manager + kCycleGaugeOffset;
      if (IsValidPtr(gauge, 25)) {
        __try {
          // Only release a rejected request while the active-event storage is empty.
          if (*reinterpret_cast<const uint8_t *>(gauge) == 100 &&
              *reinterpret_cast<const uint8_t *>(gauge + 24) == 0) {
            *reinterpret_cast<uint8_t *>(gauge) = 0;
            s_cycle.pending = false;
            AddLog(u8"[전기 허용 목록] 허용된 전기 후보 없음: 대기 중인 게이지 100 초기화");
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
      }
    }
    if (s_cycle.session != sessionP1 || s_cycle.manager != snapshot.manager ||
        snapshot.month < s_cycle.lastMonth) {
      const bool uncertain = s_cycle.session == sessionP1 &&
                             s_cycle.needsEventObservation;
      s_cycle = {};
      s_cycle.session = sessionP1;
      s_cycle.manager = snapshot.manager;
      s_cycle.needsEventObservation = uncertain;
      s_cycle.handledCouncil = snapshot.month; // Never force during load/attach month.
    }
    s_cycle.lastMonth = snapshot.month;
    if (snapshot.constructed &&
        (s_cycle.data != snapshot.data || s_cycle.start != snapshot.start)) {
      s_cycle.data = snapshot.data;
      s_cycle.start = snapshot.start;
      int duration = snapshot.duration; // Setting 0 uses the game definition.
      for (int i = 0; i < kTengiListEventCount; ++i) {
        if (kManagedEventSlots[i] == snapshot.id && g_tengiListAllowed[i] &&
            g_tengiListDurationMonths[i] > 0 && g_tengiListDurationMonths[i] <= 120) {
          duration = g_tengiListDurationMonths[i];
          break;
        }
      }
      s_cycle.earliest = snapshot.start + duration;
      s_cycle.pending = false; // Only a real constructed event confirms occurrence.
      s_cycle.needsEventObservation = false;
      AddLog(u8"[Tengi 주기] 실제 전기 확인: ID %u, 시작 %d/%d, 기간 %d개월",
             static_cast<unsigned int>(snapshot.id), snapshot.start / 12,
             snapshot.start % 12 + 1, duration);
    }
    if (snapshot.month % 3 != 0 || s_cycle.handledCouncil == snapshot.month)
      return;
    s_cycle.handledCouncil = snapshot.month; // Also consume skipped/OFF councils.
    if (!bInfTengi || snapshot.constructed || s_cycle.pending ||
        s_cycle.needsEventObservation || snapshot.month < s_cycle.earliest)
      return;
    bool anyAllowed = false;
    for (int i = 0; i < kTengiListEventCount; ++i)
      anyAllowed = anyAllowed || g_tengiListAllowed[i];
    if (!anyAllowed)
      return; // Nothing permitted: do not request momentum or mark a pending event.
    if (RequestCycleTengi(snapshot)) {
      s_cycle.pending = true; // Request is not proof of actual occurrence.
      AddLog(u8"[Tengi 주기] %d/%d 발생 요청 1회: 실제 객체 생성 대기",
             snapshot.month / 12, snapshot.month % 12 + 1);
    }
  }

  // 별도 훅 없이 설정된 기간만 변경한다. 목록 허용/차단은 아직 구현되지 않았다.
  // 이 함수는 UI가 닫혀 있어도 렌더 유지보수 게이트(200ms)에서 호출된다.
  void TickTengiListDurations(bool immediate) {
    bool requested = false;
    for (int i = 0; i < kTengiListEventCount; ++i) {
      if ((g_tengiListAllowed[i] && g_tengiListDurationMonths[i] > 0) ||
          s_durationOverrides[i].active) {
        requested = true;
        break;
      }
    }
    if (!requested)
      return;

    const ULONGLONG now = GetTickCount64();
    if (!immediate && s_lastTengiDurationPollMs != 0 &&
        now - s_lastTengiDurationPollMs < 2000)
      return;
    s_lastTengiDurationPollMs = now;

    int applied = 0, restored = 0, unavailable = 0, instant = 0, failed = 0;
    for (int i = 0; i < kTengiListEventCount; ++i) {
      TengiDurationOverride &saved = s_durationOverrides[i];
      const int months = g_tengiListAllowed[i] ?
                             g_tengiListDurationMonths[i] : 0;
      const int slot = kManagedEventSlots[i];
      const uintptr_t address = ResolveEventDurationAddress(slot);
      if (!address) {
        if (months > 0)
          ++unavailable;
        // 시나리오 로딩으로 메모리가 재할당되었으면 기존 주소를 재사용하지 않는다.
        saved = {};
        continue;
      }
      if (saved.address != address) {
        saved = {};
        saved.address = address;
      }
      uint16_t current = 0;
      if (!TryReadEventDuration(address, &current)) {
        ++failed;
        continue;
      }

      if (months <= 0) {
        if (saved.active && current == saved.written) {
          if (TryWriteEventDuration(address, saved.original))
            ++restored;
          else
            ++failed;
        }
        saved.active = false;
        continue;
      }

      if (months > 120) {
        ++failed;
        continue;
      }
      // 원래 즉발(0개월)인 이벤트는 종류를 바꾸지 않도록 덮어쓰지 않는다.
      if (!saved.active && current == 0) {
        ++instant;
        continue;
      }

      if (!saved.active) {
        saved.original = current;
        saved.active = true;
      }
      if (current == static_cast<uint16_t>(months)) {
        saved.written = current;
        continue;
      }
      if (TryWriteEventDuration(address, static_cast<uint16_t>(months))) {
        saved.written = static_cast<uint16_t>(months);
        ++applied;
      } else {
        ++failed;
      }
    }
    if (applied || restored || (immediate && (failed || unavailable || instant))) {
      AddLog(u8"[전기 목록][기간 실험] 적용 %d, 원복 %d, 미확보 %d, 즉발 제외 %d, 실패 %d (발생 허용 필터 미구현)",
             applied, restored, unavailable, instant, failed);
    }
  }

} // namespace DX11Base
