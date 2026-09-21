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
      if (g_tengiApplied)
        return;
      if (g_tengiRunning)
        return;

      g_tengiRunning = true;

      HANDLE hThread = CreateThread(
          nullptr, 0,
          [](LPVOID) -> DWORD {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
            uintptr_t searchEnd = exeBase + mi.SizeOfImage;

            if (!g_tengiHookAddr)
              g_tengiHookAddr = FindPattern(exeBase, searchEnd, "41 88 87 18 4B 1E 00");

            AddLog("[DEBUG] tengiHook: %p", (void *)g_tengiHookAddr);

            if (g_tengiHookAddr && !g_tengiApplied) {
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
    return *(uint8_t *)g_tengiAddr;
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
    *(uint8_t *)g_tengiAddr = value;

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
        *(uint8_t *)flagAddr = 1;
      }
    }
  }

  // 전기 취소 (플래그 0 설정 및 할당된 이벤트 포인터 주소 초기화)
  void CancelTengi() {
    if (!g_tengiAddr || !IsValidPtr(g_tengiAddr + 0x18, 1))
      return;

    // 1. 발생 플래그(+0x18)를 0으로 초기화
    uintptr_t flagAddr = g_tengiAddr + 0x18;
    *(uint8_t *)flagAddr = 0;

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


  void DumpTengiParameterDiagnostics() {
    struct Entry { int id; const char *name; };
    static const Entry kEntries[] = {
        {1, u8"결전"}, {2, u8"이민족습격"}, {7, u8"악적발호"},
        {8, u8"의심암귀"}, {9, u8"민심혼란"}, {10, u8"붕벽"},
        {12, u8"여세"}, {14, u8"피폐"}, {16, u8"권위고양"},
        {18, u8"보장각성"}, {19, u8"기장각성"}, {20, u8"궁장각성"},
        {21, u8"병격난무"}, {22, u8"전승기"}, {23, u8"중지성성"}};

    constexpr uintptr_t kManagerRva = 0x02E98BC8;
    constexpr uintptr_t kEventTableOffset = 0x58A670;
    constexpr uintptr_t kValueOffset = 0x12;

    uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[전기진단] 게임 EXE base 없음");
      return;
    }

    uintptr_t managerSlot = exeBase + kManagerRva;
    if (!IsValidPtr(managerSlot, sizeof(uintptr_t))) {
      AddLog(u8"[전기진단] manager slot 읽기 불가: %p", reinterpret_cast<void *>(managerSlot));
      return;
    }

    uintptr_t manager = *reinterpret_cast<uintptr_t *>(managerSlot);
    if (!manager || !IsValidPtr(manager, 1)) {
      AddLog(u8"[전기진단] manager 포인터 준비 안 됨: %p", reinterpret_cast<void *>(manager));
      return;
    }

    AddLog(u8"[전기진단] ===== +0x12 읽기 시작 / manager=%p =====", reinterpret_cast<void *>(manager));

    for (const auto &entry : kEntries) {
      uintptr_t slot = manager + kEventTableOffset +
                       static_cast<uintptr_t>(entry.id) * sizeof(uintptr_t);
      if (!IsValidPtr(slot, sizeof(uintptr_t))) {
        AddLog(u8"[전기진단] ID:%d %s | table slot 읽기 불가", entry.id, entry.name);
        continue;
      }

      uintptr_t eventPtr = *reinterpret_cast<uintptr_t *>(slot);
      if (!eventPtr || !IsValidPtr(eventPtr + kValueOffset, sizeof(int16_t))) {
        AddLog(u8"[전기진단] ID:%d %s | event=%p | +12 읽기 불가",
               entry.id, entry.name, reinterpret_cast<void *>(eventPtr));
        continue;
      }

      int16_t value = *reinterpret_cast<const int16_t *>(eventPtr + kValueOffset);
      AddLog(u8"[전기진단] ID:%d %s | event=%p | +12=%d (0x%04X)",
             entry.id, entry.name, reinterpret_cast<void *>(eventPtr),
             static_cast<int>(value),
             static_cast<unsigned int>(static_cast<uint16_t>(value)));
    }

    AddLog(u8"[전기진단] ===== 읽기 완료 / 메모리 변경 없음 =====");
  }

} // namespace DX11Base
