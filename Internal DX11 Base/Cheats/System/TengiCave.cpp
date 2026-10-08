#include "../../pch.h"
#include "TengiCave.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../NotificationManager.h"
#include "../../showlog.h"
#include <psapi.h>
#include <string>


namespace DX11Base {
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
