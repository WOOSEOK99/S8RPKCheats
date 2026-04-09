// Legacy backup (not compiled):
// Archived SetOfficerCapture/InstallOfficerCave implementation
// extracted from SelectOfficercapture.cpp.
//
// To restore:
// 1) copy functions/variables back into SelectOfficercapture.cpp/.h
// 2) re-enable call sites in DrawSelectedOfficerWindow()
// 3) rebuild and test

namespace DX11BaseLegacyBackup {

/*
  bool g_officerCaptureRunning = false;
  static uintptr_t g_officerHookAddr = 0;
  static uint8_t g_officerOriginal[8] = {};
  static uintptr_t g_officerCaveAddr = 0;
  static bool g_officerApplied = false;

  static bool InstallOfficerCave() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t hookAddr = 0;
    uint8_t targetOffset = 0;
    uint8_t opType = 0;

    struct HookCandidate {
      const char *pattern;
      uint8_t offset;
      uint8_t type;
    } candidates[] = {
        {"45 0F B6 8F A5 00 00 00", 0xA5, 1},
        {"45 0F B6 8F AB 00 00 00", 0xAB, 1},
        {"45 0F B6 8F AE 00 00 00", 0xAE, 1},
    };

    for (const auto &c : candidates) {
      uintptr_t addr = DX11Base::FindPattern(exeBase, exeBase + 0x3000000, c.pattern);
      if (addr) {
        if (*(unsigned char *)addr == 0xE9) {
          AddLog(u8"[CONFLICT] 지점 0x%X는 이미 타 프로그램이 사용 중입니다. 다음 후보 탐색...", c.offset);
          continue;
        }
        hookAddr = addr;
        targetOffset = c.offset;
        opType = c.type;
        break;
      }
    }

    if (!hookAddr)
      return false;

    g_officerHookAddr = hookAddr;
    memcpy(g_officerOriginal, (void *)hookAddr, 8);

    g_officerCaveAddr = AllocNear(hookAddr, 1024);
    if (!g_officerCaveAddr)
      return false;

    unsigned char shellcode[] = {
        0x50,
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x4C, 0x89, 0x38,
        0x58,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    *(uintptr_t *)(shellcode + 3) = (uintptr_t)&g_capturedOfficerBase;

    if (opType == 1) {
      unsigned char instr[] = {0x45, 0x0F, 0xB6, 0x8F, 0x00, 0x00, 0x00, 0x00};
      *(uint32_t *)(instr + 4) = (uint32_t)targetOffset;
      memcpy(shellcode + 15, instr, 8);
    } else if (opType == 2) {
      unsigned char instr[] = {0x45, 0x0F, 0xB7, 0x8F, 0x2E, 0x00, 0x00, 0x00};
      memcpy(shellcode + 15, instr, 8);
    }

    uintptr_t jumpBackAddr = hookAddr + 8;
    *(uintptr_t *)(shellcode + 29) = jumpBackAddr;

    memcpy((void *)g_officerCaveAddr, shellcode, sizeof(shellcode));
    return ApplyJmp(hookAddr, g_officerCaveAddr, 8);
  }

  void SetOfficerCapture(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (enable) {
      if (g_officerApplied)
        return;
      if (g_officerCaptureRunning)
        return;

      g_officerCaptureRunning = true;

      HANDLE hThread = CreateThread(
          nullptr, 0,
          [](LPVOID) -> DWORD {
            if (!g_officerApplied) {
              if (InstallOfficerCave())
                g_officerApplied = true;
            }

            AddLog("[DEBUG] officerCave applied: %d", g_officerApplied);
            g_officerCaptureRunning = false;
            return 0;
          },
          nullptr, 0, nullptr);

      if (hThread)
        CloseHandle(hThread);
    } else {
      g_capturedOfficerBase = 0;

      if (g_officerApplied) {
        RestoreBytes(g_officerHookAddr, g_officerOriginal, 8);
        VirtualFree((LPVOID)g_officerCaveAddr, 0, MEM_RELEASE);
        g_officerCaveAddr = 0;
        g_officerApplied = false;
        g_officerHookAddr = 0;
      }
    }
  }
*/

} // namespace DX11BaseLegacyBackup

