#include "StartSetting.h"
#include "Cheats.h"
#include "MemoryUtils.h"
#include "MonthCapture.h"
#include "NotificationManager.h"
#include "pch.h"
#include "showlog.h"


#include <psapi.h>

namespace DX11Base {

  // 시나리오 시작 설정 자동 적용
  bool g_startSettingEnabled = false;
  static bool g_startSettingRunning = false;
  static HANDLE g_startSettingThread = nullptr;

  static bool g_flag1 = false;
  static bool g_flag2 = false;
  static bool g_flag3 = false;
  static bool g_flag4 = false;
  static bool g_flag5 = false;
  static bool g_loggedRelationSwap = false;
  static bool g_loggedRelationNoMatch = false;
  static bool g_loggedRelationWaiting = false;
  static bool g_loggedRelationAlreadyApplied = false;
  static bool g_loggedYearNotZero = false;

  static void LogOfficerPtrBrief(const char *tag, uintptr_t ptr) {
    if (!ptr || !IsValidPtr(ptr, 0x20)) {
      AddLog(u8"[시작설정][관계] %s: ptr=%p (invalid)", tag, (void *)ptr);
      return;
    }
    uint16_t id = *(uint16_t *)(ptr + 0x08);
    uint8_t st = *(uint8_t *)(ptr + 0x10);
    AddLog(u8"[시작설정][관계] %s: ptr=%p id=%u st=0x%02X", tag, (void *)ptr, (unsigned)id, (unsigned)st);
  }

  static uintptr_t ResolveRoot() {
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

  static void WriteQword(uintptr_t addr, uint64_t val) {
    if (!IsValidPtr(addr, 8))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
    *(uint64_t *)addr = val;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static void WriteByte(uintptr_t addr, uint8_t val) {
    if (!IsValidPtr(addr, 1))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &old);
    *(uint8_t *)addr = val;
    VirtualProtect((LPVOID)addr, 1, old, &tmp);
  }

  static void RunOnce() {
    uintptr_t root = ResolveRoot();
    if (!root) {
      return;
    }

    uintptr_t yearAddr = root + 0x49EB06;
    if (!IsValidPtr(yearAddr, 2)) {
      return;
    }
    int16_t yearVal = *(int16_t *)yearAddr;

    if (yearVal != 0) {
      g_flag1 = g_flag2 = g_flag3 = g_flag4 = g_flag5 = false;
      g_loggedRelationSwap = false;
      return;
    }
    g_loggedYearNotZero = false;

    {
      uintptr_t revengeAddr = root + 0x4172D0;
      uintptr_t enemyAddr = root + 0x4172D8;
      uintptr_t guanyuPosAddr = root + 0x1F7200;
      uintptr_t caohongPosAddr = root + 0x24DDC0;

      if (IsValidPtr(revengeAddr, 8) && IsValidPtr(enemyAddr, 8) && IsValidPtr(guanyuPosAddr, 8) &&
          IsValidPtr(caohongPosAddr, 8)) {
        uint64_t revengeVal = *(uint64_t *)revengeAddr;
        uint64_t enemyVal = *(uint64_t *)enemyAddr;
        bool slotsReady = (revengeVal != 0 && enemyVal != 0);
        bool condSwapNeeded = (revengeVal == guanyuPosAddr && enemyVal == caohongPosAddr);
        bool condAlreadySwapped = (revengeVal == caohongPosAddr && enemyVal == guanyuPosAddr);
        if (!slotsReady) {
          g_flag1 = false;
          g_loggedRelationSwap = false;
          g_loggedRelationNoMatch = false;
          g_loggedRelationAlreadyApplied = false;
          if (!g_loggedRelationWaiting) {
            g_loggedRelationWaiting = true;
          }
        } else if (condSwapNeeded && !g_flag1) {
          g_loggedRelationWaiting = false;
          g_loggedRelationAlreadyApplied = false;
          if (!g_loggedRelationSwap) {
            LogOfficerPtrBrief("revenge(current)", revengeVal);
            LogOfficerPtrBrief("enemy(current)", enemyVal);
            LogOfficerPtrBrief("target(caohongPos)", guanyuPosAddr);
            LogOfficerPtrBrief("target(guanyuPos)", caohongPosAddr);
          }

          WriteQword(revengeAddr, caohongPosAddr);
          WriteQword(enemyAddr, guanyuPosAddr);
          g_flag1 = true;

          if (!g_loggedRelationSwap) {
            uint64_t revengeAfter = IsValidPtr(revengeAddr, 8) ? *(uint64_t *)revengeAddr : 0;
            uint64_t enemyAfter = IsValidPtr(enemyAddr, 8) ? *(uint64_t *)enemyAddr : 0;
            LogOfficerPtrBrief("revenge(after)", (uintptr_t)revengeAfter);
            LogOfficerPtrBrief("enemy(after)", (uintptr_t)enemyAfter);
            g_loggedRelationSwap = true;
          }
        } else if (condAlreadySwapped) {
          g_flag1 = true;
          g_loggedRelationWaiting = false;
          g_loggedRelationNoMatch = false;
          if (!g_loggedRelationAlreadyApplied) {
            g_loggedRelationAlreadyApplied = true;
          }
        } else {
          g_flag1 = false;
          g_loggedRelationSwap = false;
          g_loggedRelationWaiting = false;
          g_loggedRelationAlreadyApplied = false;
          if (!g_loggedRelationNoMatch) {
            g_loggedRelationNoMatch = true;
          }
        }
      } else {
        g_flag1 = false;
        g_loggedRelationSwap = false;
        g_loggedRelationNoMatch = false;
        g_loggedRelationWaiting = false;
        g_loggedRelationAlreadyApplied = false;
      }
    }

    {
      uintptr_t lordAddr = root + 0xC72A0;
      uintptr_t zhugeAddr = root + 0x23CF10;
      uintptr_t bowTechAddr = root + 0xC73E7;
      uintptr_t siegeTechAddr = root + 0xC73E8;

      if (IsValidPtr(lordAddr, 8) && IsValidPtr(zhugeAddr, 8) && IsValidPtr(bowTechAddr, 1) &&
          IsValidPtr(siegeTechAddr, 1)) {
        uint64_t lordVal = *(uint64_t *)lordAddr;
        bool cond = (lordVal == zhugeAddr);
        if (cond && !g_flag2) {
          WriteByte(bowTechAddr, 0x03);
          WriteByte(siegeTechAddr, 0x04);
          g_flag2 = true;
        } else if (!cond) {
          g_flag2 = false;
        }
      } else {
        g_flag2 = false;
      }
    }

    {
      uintptr_t zStatusAddr = root + 0x1F2200;
      uintptr_t bStatusAddr = root + 0x24DA00;
      uintptr_t cStatusAddr = root + 0x27E790;

      if (IsValidPtr(zStatusAddr, 1) && IsValidPtr(bStatusAddr, 1) && IsValidPtr(cStatusAddr, 1)) {
        uint8_t zStatus = *(uint8_t *)zStatusAddr;
        uint8_t bStatus = *(uint8_t *)bStatusAddr;
        bool cond = (zStatus == 200 && bStatus == 200);


        if (cond && !g_flag3) {
          WriteByte(cStatusAddr, 88);
          g_flag3 = true;
        } else if (!cond) {
          g_flag3 = false;
        }
      } else {
        g_flag3 = false;
      }
    }

    {
      uintptr_t baseAddr1 = root + 0xC6A0A;
      uintptr_t baseAddr2 = root + 0xC6919;
      const int gap = 2456;

      if (IsValidPtr(baseAddr1, 1) && IsValidPtr(baseAddr2, 1)) {
        uintptr_t slots1[4] = {baseAddr1, baseAddr1 + gap, baseAddr1 + gap * 2, baseAddr1 + gap * 3};
        uintptr_t slots2[4] = {baseAddr2, baseAddr2 + gap, baseAddr2 + gap * 2, baseAddr2 + gap * 3};

        bool cond = true;
        for (int i = 0; i < 4; i++) {
          if (!IsValidPtr(slots1[i], 1) || *(uint8_t *)slots1[i] != 23) {
            cond = false;
            break;
          }
        }

        if (cond && !g_flag4) {
          for (int i = 0; i < 4; i++) {
            WriteByte(slots1[i], 0x00);
            WriteQword(slots1[i] + 6, 0);
            WriteQword(slots1[i] + 14, 0);
          }
          for (int i = 0; i < 4; i++) {
            WriteByte(slots2[i] + 0, 0x00);
            WriteByte(slots2[i] + 1, 0x00);
            WriteByte(slots2[i] + 2, 0x00);
            WriteByte(slots2[i] + 3, 0x00);
          }
          g_flag4 = true;
        } else if (!cond) {
          g_flag4 = false;
        }
      } else {
        g_flag4 = false;
      }
    }

    {
      uintptr_t statusAddr1 = root + 0x24DA00;
      uintptr_t statusAddr2 = root + 0x251700;
      uintptr_t aForceAddr = root + 0x1FB2E8;
      uintptr_t aCityAddr = root + 0x1FB2F0;
      uintptr_t bForceAddr = root + 0x24DA08;
      uintptr_t bCityAddr = root + 0x24DA10;

      if (IsValidPtr(statusAddr1, 1) && IsValidPtr(statusAddr2, 1) && IsValidPtr(aForceAddr, 8) &&
          IsValidPtr(aCityAddr, 8) && IsValidPtr(bForceAddr, 8) && IsValidPtr(bCityAddr, 8)) {
        uint8_t status1 = *(uint8_t *)statusAddr1;
        uint8_t status2 = *(uint8_t *)statusAddr2;
        bool cond = (status1 == 200 && status2 == 200);

        if (cond && !g_flag5) {
          uint64_t bForce = *(uint64_t *)bForceAddr;
          uint64_t bCity = *(uint64_t *)bCityAddr;
          WriteQword(aForceAddr, bForce);
          WriteQword(aCityAddr, bCity);
          g_flag5 = true;
        } else if (!cond) {
          g_flag5 = false;
        }
      } else {
        g_flag5 = false;
      }
    }
  }

  static DWORD WINAPI StartSettingThread(LPVOID) {
    while (g_startSettingEnabled) {
      Sleep(200);
      if (!g_startSettingEnabled)
        break;
      RunOnce();
      extern void UpdateUndiscoveredToRonin();
      UpdateUndiscoveredToRonin();
    }
    g_startSettingRunning = false;
    return 0;
  }

  void SetStartSetting(bool enable) {
    if (enable) {
      if (g_startSettingEnabled)
        return;

      g_flag1 = g_flag2 = g_flag3 = g_flag4 = g_flag5 = false;
      g_loggedRelationSwap = false;
      g_loggedRelationNoMatch = false;
      g_loggedRelationWaiting = false;
      g_loggedRelationAlreadyApplied = false;
      g_loggedYearNotZero = false;
      g_startSettingEnabled = true;
      g_startSettingRunning = true;

      RunOnce();

      g_startSettingThread = CreateThread(nullptr, 0, StartSettingThread, nullptr, 0, nullptr);
      if (!g_startSettingThread) {
        g_startSettingEnabled = false;
        g_startSettingRunning = false;
        AddLog(u8"[시작설정] 스레드 생성 실패");
      } else {
        AddLog(u8"[시작설정] 활성화");
      }
    } else {
      g_startSettingEnabled = false;

      if (g_startSettingThread) {
        WaitForSingleObject(g_startSettingThread, 1000);
        CloseHandle(g_startSettingThread);
        g_startSettingThread = nullptr;
      }

      g_flag1 = g_flag2 = g_flag3 = g_flag4 = g_flag5 = false;
      g_loggedRelationSwap = false;
      g_loggedRelationNoMatch = false;
      g_loggedRelationWaiting = false;
      g_loggedRelationAlreadyApplied = false;
      g_loggedYearNotZero = false;
      AddLog(u8"[시작설정] 비활성화");
    }
  }

  // 모든 미발견 무장 재야로 변경 및 나이 보정
  extern bool bUndiscoveredToRonin;
  void SetUndiscoveredToRonin(bool enable) {
    if (enable) {
      AddLog(u8"[시작설정] 미발견 무장 보정 기능 활성화 시도");
      SetStartSetting(true);
    }
  }

  void UpdateUndiscoveredToRonin() {
    if (!bUndiscoveredToRonin)
      return;

    // ReadScenarioDate는 메뉴에서도 동작함 (MonthCapture 경로 사용)
    unsigned short currentYear = 0;
    uint8_t currentMonth = 0;
    if (!ReadScenarioDate(&currentYear, &currentMonth)) {
      static uint64_t s_logFail = 0;
      if (GetTickCount64() - s_logFail > 10000) {
        AddLog(u8"[미발견보정] ReadScenarioDate 실패");
        s_logFail = GetTickCount64();
      }
      return;
    }

    static bool s_waitForStart = false;
    static bool s_seenMenu = false;
    static bool s_logged = false;

    // 메뉴 화면 (year >= 100) 감지: s_seenMenu 기록만
    // 단, 이미 대기 중이면 리셋하면 안 됨 (시나리오 연도도 >= 100일 수 있음)
    if (currentYear >= 100) {
      if (!s_seenMenu) {
        AddLog(u8"[미발견보정] 메뉴 확인 (year=%d)", (int)currentYear);
        s_seenMenu = true;
      }
      if (!s_waitForStart) {
        // 아직 대기 전 → 메뉴 상태이므로 그냥 리턴
        return;
      }
      // s_waitForStart == true → 시나리오 연도가 확인된 것이므로 아래 트리거 로직으로 fall-through
    }

    // 1. 시나리오 선택 전 초기화 상태 (0년 0월) 감지 - 반드시 메뉴를 거친 뒤에만
    if (currentYear == 0 && currentMonth == 0) {
      if (s_seenMenu && !s_waitForStart) {
        AddLog(u8"[미발견보정] 대기 상태 진입 (year=0, month=0, 메뉴 확인됨)");
        s_logged = false;
        s_waitForStart = true;
      }
      return;
    }

    // 2. 대기 없이 연도가 바뀐 경우 스킵 (게임 첫 로딩 184년 등)
    if (!s_waitForStart) {
      if (!s_logged) {
        AddLog(u8"[미발견보정] 스킵 (year=%d month=%d, 0->0 단계 미통과)", (int)currentYear, (int)currentMonth);
        s_logged = true;
      }
      return;
    }

    // 3. 트리거 발동: 0년에서 실제 시나리오 연도로 바뀐 순간
    AddLog(u8"[미발견보정] 트리거! year=%d month=%d", (int)currentYear, (int)currentMonth);

    // 이제 ResolveRoot 시도 (시나리오 로드 후이므로 잡혀야 함)
    uintptr_t root = ResolveRoot();
    if (!root) {
      AddLog(u8"[미발견보정] root 없음 - 다음 루프 재시도");
      // s_waitForStart는 true로 유지하여 다음 루프에서 재시도
      return;
    }

    int targetYear = (int)currentYear;
    s_waitForStart = false;
    s_logged = false;

    AddNotification(u8"미발견 무장이 발견되었습니다. 재야로 변경중입니다. 잠시 기다려주세요");

    uintptr_t baseAddr = root + 0x1d4560;
    if (!IsValidPtr(baseAddr, 0x100)) {
      AddLog(u8"[미발견보정] baseAddr 무효 root=%p base=%p", (void *)root, (void *)baseAddr);
      return;
    }

    AddLog(u8"[미발견보정] 보정 시작 baseAddr=%p targetYear=%d", (void *)baseAddr, targetYear);

    const int stride = 0x3D0;
    const int maxOfficers = 1000;
    int countModified = 0;

    for (int i = 0; i < maxOfficers; i++) {
      uintptr_t offPtr = baseAddr + (i * stride);
      if (!IsValidPtr(offPtr, 0x40))
        break;

      uint16_t id = *(uint16_t *)(offPtr + 0x08);
      if (id == 0 || id > 2000)
        continue;

      uint8_t *pStatus = (uint8_t *)(offPtr + 0x10);
      uint8_t status = *pStatus;

      bool modified = false;
      if (status == 0x68 || status == 0x78) {
        WriteByte((uintptr_t)pStatus, 0x58);
        modified = true;
      }

      uint16_t *pAppearYear = (uint16_t *)(offPtr + 0x32);
      uint16_t *pBirthYear  = (uint16_t *)(offPtr + 0x34);
      uint16_t *pDeathYear  = (uint16_t *)(offPtr + 0x36);

      if (IsValidPtr((uintptr_t)pAppearYear, 6)) {
        if (*pAppearYear > targetYear && *pAppearYear < 400) {
          *(uint16_t *)pAppearYear = (uint16_t)targetYear;
          modified = true;
        }
        if (*pBirthYear >= (targetYear - 15) && *pBirthYear < 400) {
          *(uint16_t *)pBirthYear = (uint16_t)(targetYear - 20);
          modified = true;
        }
        if (*pDeathYear <= targetYear && *pDeathYear != 0) {
          *(uint16_t *)pDeathYear = (uint16_t)(targetYear + 50);
          modified = true;
        }
      }

      if (modified)
        countModified++;
    }

    AddLog(u8"[미발견보정] 완료: %d명 수정", countModified);
    AddNotification(std::to_string(countModified) + u8"명의 미발견 무장이 재야로 변경되었습니다.");
  }

  } // namespace DX11Base
