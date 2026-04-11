#include "StartSetting.h"
#include "Cheats.h"
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
    if (!root)
      return;

    uintptr_t yearAddr = root + 0x49EB06;
    if (!IsValidPtr(yearAddr, 2))
      return;
    int16_t yearVal = *(int16_t *)yearAddr;

    if (yearVal != 0) {
      g_flag1 = g_flag2 = g_flag3 = g_flag4 = g_flag5 = false;
      g_loggedRelationSwap = false;
      if (!g_loggedYearNotZero) {
        AddLog(u8"[시작설정] 대기: year=%d (0이 아님)", (int)yearVal);
        g_loggedYearNotZero = true;
      }
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
            AddLog(u8"[시작설정][관계] 대기: revenge/enemy 슬롯이 아직 비어있음 (rev=%p, enemy=%p)", (void *)revengeVal,
                   (void *)enemyVal);
            g_loggedRelationWaiting = true;
          }
        } else if (condSwapNeeded && !g_flag1) {
          g_loggedRelationWaiting = false;
          g_loggedRelationAlreadyApplied = false;
          if (!g_loggedRelationSwap) {
            AddLog(u8"[시작설정][관계] swap 전: revengeSlot=%p enemySlot=%p", (void *)revengeAddr, (void *)enemyAddr);
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
            AddLog(u8"[시작설정][관계] 이미 적용 상태 유지 (rev=%p, enemy=%p)", (void *)revengeVal, (void *)enemyVal);
            g_loggedRelationAlreadyApplied = true;
          }
        } else {
          g_flag1 = false;
          g_loggedRelationSwap = false;
          g_loggedRelationWaiting = false;
          g_loggedRelationAlreadyApplied = false;
          if (!g_loggedRelationNoMatch) {
            AddLog(u8"[시작설정][관계] 조건 미충족: revenge=%p enemy=%p 기대(관우=%p, 조홍=%p)", (void *)revengeVal,
                   (void *)enemyVal, (void *)guanyuPosAddr, (void *)caohongPosAddr);
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

} // namespace DX11Base
