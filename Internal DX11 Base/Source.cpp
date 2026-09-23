#pragma once
#include "Cheats.h"
#include "Cheats/System/SpeedHack.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Cheats/War/Spell5HealProbe.h"
#include "Config.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuState.h"
#include "debug.h"
#include "pch.h"
#include "showlog.h"
#include <filesystem>

#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

#define TRAITS_PTR_OFFSET 0x57A4B8

#ifndef WM_IME_FIRST
#define WM_IME_FIRST 0x0281
#endif
#ifndef WM_IME_LAST
#define WM_IME_LAST 0x0288
#endif

void ApplyGoldCheat(int amount) {
  uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

  uintptr_t *step1Ptr = (uintptr_t *)(exeBase + 0x0); // 베이??주소
  if (IsBadReadPtr(step1Ptr, sizeof(uintptr_t)))
    return;

  uintptr_t step1 = *step1Ptr;
  uintptr_t *traitsPtr = (uintptr_t *)(step1 + TRAITS_PTR_OFFSET);
  if (IsBadReadPtr(traitsPtr, sizeof(uintptr_t)))
    return;

  uintptr_t traitsBaseAddress = *traitsPtr;
  if (traitsBaseAddress != 0) {
    int *goldPtr = (int *)(traitsBaseAddress + 0x40);
    if (!IsBadWritePtr(goldPtr, sizeof(int))) {
      *goldPtr = amount;
    }
  }
}

typedef BOOL(WINAPI *PGetCursorPos)(LPPOINT);
static PGetCursorPos oGetCursorPos = NULL;

typedef SHORT(WINAPI *PGetAsyncKeyState)(int);
static PGetAsyncKeyState oGetAsyncKeyState = NULL;

typedef SHORT(WINAPI *PGetKeyState)(int);
static PGetKeyState oGetKeyState = NULL;

typedef BOOL(WINAPI *PGetKeyboardState)(PBYTE);
static PGetKeyboardState oGetKeyboardState = NULL;

typedef BOOL(WINAPI *PPeekMessageW)(LPMSG, HWND, UINT, UINT, UINT);
static PPeekMessageW oPeekMessageW = NULL;

typedef BOOL(WINAPI *PPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);
static PPeekMessageA oPeekMessageA = NULL;

SHORT WINAPI hkGetAsyncKeyState(int vKey) {
  if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
    // [�߿�] �ѱ� �Է±�(IME)�� UI�� ����Ʈ/��Ʈ��/��Ʈ ���¸� ��Ȯ�� �����ؾ� ������ ���� �����մϴ�.
    // ���� �ý��� ���Ű���� �������� �ʰ� ���� ���� ��ȯ�մϴ�.
    bool isModifier =
        (vKey == VK_SHIFT || vKey == VK_LSHIFT || vKey == VK_RSHIFT || vKey == VK_CONTROL || vKey == VK_LCONTROL ||
         vKey == VK_RCONTROL || vKey == VK_MENU || vKey == VK_LMENU || vKey == VK_RMENU);

    // ��/��(0x15), ��ƽ(VK_OEM_3) �� ��� ���� Ű(Modifier)�� ���
    if (ImGui::GetIO().WantCaptureKeyboard && !isModifier && vKey != VK_OEM_3 && vKey != 0x15 && vKey != 0xA5) {
      return 0;
    }
  }
  return oGetAsyncKeyState(vKey);
}

SHORT WINAPI hkGetKeyState(int vKey) {
  if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
    bool isModifier =
        (vKey == VK_SHIFT || vKey == VK_LSHIFT || vKey == VK_RSHIFT || vKey == VK_CONTROL || vKey == VK_LCONTROL ||
         vKey == VK_RCONTROL || vKey == VK_MENU || vKey == VK_LMENU || vKey == VK_RMENU);

    if (ImGui::GetIO().WantCaptureKeyboard && !isModifier && vKey != VK_OEM_3 && vKey != 0x15 && vKey != 0xA5) {
      return 0;
    }
  }
  return oGetKeyState(vKey);
}

BOOL WINAPI hkGetKeyboardState(PBYTE lpKeyState) {
  BOOL result = oGetKeyboardState(lpKeyState);
  if (result && DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
    if (ImGui::GetIO().WantCaptureKeyboard) {
      // ������ Ű���� ���¸� �������� ���
      BYTE tilde = lpKeyState[VK_OEM_3];
      BYTE hangul = lpKeyState[0x15];
      BYTE ralt = lpKeyState[0xA5];
      BYTE shift = lpKeyState[VK_SHIFT];
      BYTE lshift = lpKeyState[VK_LSHIFT];
      BYTE rshift = lpKeyState[VK_RSHIFT];
      BYTE ctrl = lpKeyState[VK_CONTROL];
      BYTE lctrl = lpKeyState[VK_LCONTROL];
      BYTE rctrl = lpKeyState[VK_RCONTROL];
      BYTE alt = lpKeyState[VK_MENU];
      BYTE lalt = lpKeyState[VK_LMENU];

      // ��ü�� 0���� �о������, ������ ����� Ű�鸸 ���� (�ý���/IME��)
      memset(lpKeyState, 0, 256);
      lpKeyState[VK_OEM_3] = tilde;
      lpKeyState[0x15] = hangul;
      lpKeyState[0xA5] = ralt;
      lpKeyState[VK_SHIFT] = shift;
      lpKeyState[VK_LSHIFT] = lshift;
      lpKeyState[VK_RSHIFT] = rshift;
      lpKeyState[VK_CONTROL] = ctrl;
      lpKeyState[VK_LCONTROL] = lctrl;
      lpKeyState[VK_RCONTROL] = rctrl;
      lpKeyState[VK_MENU] = alt;
      lpKeyState[VK_LMENU] = lalt;
    }
  }
  return result;
}

// OS ޽   Raw Input Ͽ Ű , Ű
// ޽  ȯ
void HandleMessageCapture(LPMSG lpMsg) {
  if (!DX11Base::g_Engine || !DX11Base::IsAnyUIOpen() || !ImGui::GetCurrentContext())
    return;
  ImGuiIO &io = ImGui::GetIO();

  // 1. Raw Input 인터셉트 (마우스/키보드)
  if (lpMsg->message == WM_INPUT) {
    UINT dwSize = 0;
    GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
    if (dwSize > 0) {
      BYTE buf[1024];
      if (dwSize <= sizeof(buf) &&
          GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, buf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
        RAWINPUT *raw = (RAWINPUT *)buf;

        // 마우스 차단: UI 위에서 마우스가 움직일 때 게임이 마우스를 읽지 못하게 함
        if (raw->header.dwType == RIM_TYPEMOUSE && io.WantCaptureMouse) {
          lpMsg->message = WM_NULL;
          return;
        }

        // 키보드 차단
        if (raw->header.dwType == RIM_TYPEKEYBOARD && io.WantCaptureKeyboard) {
          USHORT vkey = raw->data.keyboard.VKey;
          bool isModifier =
              (vkey == VK_SHIFT || vkey == VK_LSHIFT || vkey == VK_RSHIFT || vkey == VK_CONTROL ||
               vkey == VK_LCONTROL || vkey == VK_RCONTROL || vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU);
          bool isImeKey = (vkey == 0x15 || vkey == 0xA5 || vkey == VK_PROCESSKEY);
          bool isMenuToggle = (vkey == VK_OEM_3);

          if (io.WantTextInput && !isModifier && !isImeKey && !isMenuToggle) {
            lpMsg->message = WM_NULL;
            return;
          }

          // �ؽ�Ʈ �Է��� �ƴϴ���, ����ó�� �����̽�/�齺���̽��� ���� ����.
          if (vkey == VK_SPACE || vkey == VK_BACK) {
            lpMsg->message = WM_NULL;
            return;
          }
          // �� �� Ű�� �ƹ��͵� ���� ���� (�ڿ������� ���)
        }
      }
    }
  }

  // 2. Ÿ���� Ȱ��ȭ: Ű���� �޽��� ���� ��ȯ ���� ����
  //    ���� ��ü �������� TranslateMessage�� ȣ���ϹǷ�,
  //    ���⼭ �������� ȣ���ϸ� WM_CHAR�� �� �� �߻��Ͽ� ����/���ڰ� �� ����
  //    �ԷµǴ� ���װ� �߻��մϴ�. ���� �ش� �κ��� �����Ͽ� ������ �⺻
  //    �޽��� ������ �ñ�ϴ�.
}

BOOL WINAPI hkPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = oPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  if (result && lpMsg && (wRemoveMsg & PM_REMOVE)) {
    HandleMessageCapture(lpMsg);
  }
  return result;
}

BOOL WINAPI hkPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  if (result && lpMsg && (wRemoveMsg & PM_REMOVE)) {
    HandleMessageCapture(lpMsg);
  }
  return result;
}

// [�߰�] �츮�� ����æ ��¥ �Լ�
BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint) {
  BOOL result = oGetCursorPos(lpPoint); // ���� ��ǥ�� ���� ������

  // �޴��� ���� �ְ� "����� ���", "���� ȭ�� Ŭ�� ���"
  // ���°� �ƴ� ��
  // (���� �޴� ������, �Ǵ� ��� ����Ʈâ �ɼ� ����, �Ǵ�
  // �޸� ������ Ȱ��ȭ)
  // + �ӱ��̰� ���콺�� ���� ���� ���� ���� ȭ�� ������ ������
  bool bHardBlock =
      !DX11Base::bIsMenuCollapsed || (DX11Base::bShowOfficerListWin && DX11Base::bBlockClickInOfficerList) ||
      (DX11Base::bShowMemoryEditor && DX11Base::bBlockClickInMemoryEditor) || DX11Base::bShowSpecialtyInfoWin;
  if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse &&
      !DX11Base::bShowDebug && !DX11Base::bAllowGameClick && bHardBlock) {
    lpPoint->x = -1;
    lpPoint->y = -1;
  }
  return result;
}

using namespace DX11Base;

void ClientBGThread() {
  while (g_Running) {
    Menu::Loops();

    if (g_KillSwitch) {
      g_RenderManager->UnhookD3D();
      g_Hooking->Shutdown();
      g_Engine.release(); //  releases all created class instances
      g_Running = false;
    }

    std::this_thread::sleep_for(30ms);
  }
}

namespace DX11Base {
  void Shutdown(bool isTerminating) {
    static bool s_done = false;
    if (s_done)
      return;
    s_done = true;

    g_Running = false;
    g_KillSwitch = true;

    // ���μ��� ���� �ÿ��� �ý����� �޸𸮸� �˾Ƽ� ȸ���ϹǷ�,
    // ������ ���� ũ���ø� �����ϱ� ���� ����ä�⸸ �ּ������� �����մϴ�.
    if (isTerminating) {
      MH_DisableHook(MH_ALL_HOOKS);
      if (g_RenderManager) {
        // D3D ���ҽ� ����(Release)�� �ǳʶٰ� ������ ���ν����� ���� (���� ����)
        if (g_Engine && g_Engine->pGameWindow && g_RenderManager->m_OldWndProc) {
          SetWindowLongPtr(g_Engine->pGameWindow, GWLP_WNDPROC, (LONG_PTR)g_RenderManager->m_OldWndProc);
          g_RenderManager->m_OldWndProc = nullptr;
        }
      }
      return;
    }

    // �Ϲ����� ��ε�(FreeLibrary) ��Ȳ������ ��� ���ҽ��� ������� �����մϴ�.
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    if (g_RenderManager) {
      g_RenderManager->UnhookD3D();
    }

    if (g_Hooking) {
      g_Hooking->Shutdown();
    }
  }
} // namespace DX11Base

DWORD WINAPI MainThread_Initialize(LPVOID dwModule) {
  // [�ű�] ���� �ʱ⿡ �������� �α���� �÷��׸� Ȯ���Ͽ� D3D �� �� ���ʱ�
  // ������ ���Ͽ� ���
  DX11Base::LoadEarlyLogConfig();
  // Install outside loader lock, before battle UI creation and the D3D delay.
  DX11Base::AddLog("[Stratagem5UI] early bridge preparation before startup delay");
  bool stratagemUiBridgeReady = DX11Base::PrepareStratagemFiveUiBridge();

  // This experiment is intentionally armed before any battle UI can be
  // initialized. The setters persist the ON request even when game/Camp data
  // is not ready yet; the guarded battle/UI hooks complete the wiring later.
  // This removes the old "open/close the trick dialog several times" timing
  // dependency while keeping all actual writes behind the existing guards.
  DX11Base::SetSpell5HealProbe(true);
  DX11Base::SetStratagemFiveMetadataTest(true);
  DX11Base::SetStratagemFiveCountTest(true);
  DX11Base::AddLog("[Stratagem5UI] ID5 experiment requests armed before battle UI");

  // ���� DLL ���ϸ� Ȯ��
  char dllPath[MAX_PATH];
  GetModuleFileNameA((HMODULE)dwModule, dllPath, MAX_PATH);
  std::string dllName = std::filesystem::path(dllPath).filename().string();

  // [�߿�] �ʱ�ȭ �������� ���� 10�� ���
  for (int startupTick = 0; startupTick < 100; ++startupTick) {
    Sleep(100);
    if (!stratagemUiBridgeReady)
      stratagemUiBridgeReady = DX11Base::PrepareStratagemFiveUiBridge(false);
  }
  if (!stratagemUiBridgeReady)
    DX11Base::PrepareStratagemFiveUiBridge(); // Report the final refusal once.

  DX11Base::AddLog(u8"========================================");
  DX11Base::AddLog(u8"[System] ġƮ �ε� ���� (DLL: %s)", dllName.c_str());
  DX11Base::AddLog(u8"[System] ����: %s", SAM8_CHEAT_VERSION);
  DX11Base::AddLog(u8"========================================");

  UNREFERENCED_PARAMETER(dwModule);
  // quick debug popup removed

  // ���� ������ DPI �ν� ���¸� ������ �����ϸ� ���콺 ������ ��ǥ(WM_MOUSEMOVE)��
  // ���� �������� GUI ��ǥ ���̿� �ɰ��� ������(��߳�)��
  // �߻��մϴ�. SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  // -----------------------------------------------------------

  g_Engine = std::make_unique<Engine>();
  // initialize cheats subsystem (resolve pointers)
  // try immediately, but also retry in background until game finishes loading
  DX11Base::InitCheats();
  std::thread([]() {
    int attempts = 0;
    while (attempts < 60) { // retry up to ~30 seconds
      if (DX11Base::InitCheats())
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      attempts++;
    }
  }).detach();

  // Config Loading and Initial AutoLoad is now deferred to Menu::Loops() when p1 becomes valid

  g_RenderManager->HookD3D();
  g_Hooking->Initialize();

  // 2. [�߰�] �� �ʱ�ȭ ���Ŀ� �츮���� ��ǥ ���̱� ���� ��ġ�մϴ�.
  // [����] MH_EnableHook�� IAT ����(&GetCursorPos ��)�� ���� �ѱ��
  //        �Ϻ� PC���� �浹 �߻�. MH_CreateHookApi ��� �� MH_ALL_HOOKS��
  //        �Ѳ����� Ȱ��ȭ�ϴ� ������� ����.
  if (oGetCursorPos == NULL) {
    MH_CreateHookApi(L"user32.dll", "GetCursorPos", &hkGetCursorPos, (LPVOID *)&oGetCursorPos);
    MH_CreateHookApi(L"user32.dll", "GetAsyncKeyState", &hkGetAsyncKeyState, (LPVOID *)&oGetAsyncKeyState);
    MH_CreateHookApi(L"user32.dll", "GetKeyState", &hkGetKeyState, (LPVOID *)&oGetKeyState);
    MH_CreateHookApi(L"user32.dll", "GetKeyboardState", &hkGetKeyboardState, (LPVOID *)&oGetKeyboardState);
    MH_CreateHookApi(L"user32.dll", "PeekMessageW", &hkPeekMessageW, (LPVOID *)&oPeekMessageW);
    MH_CreateHookApi(L"user32.dll", "PeekMessageA", &hkPeekMessageA, (LPVOID *)&oPeekMessageA);

    // ��� ��ϵ� ���� �Ѳ����� Ȱ��ȭ (����)
    MH_EnableHook(MH_ALL_HOOKS);
  }

  //	INITIALIZE BACKGROUND THREAD
  std::thread WCMUpdate(ClientBGThread);

  //  RENDER LOOP
  g_Running = true;
  // [����] int�� ULONGLONG���� ���� (64��Ʈ ������)
  static ULONGLONG LastTick = 0;

  while (g_Running) {
    // �� Ű ���� ` (����/��ƽ) Ű�� ����
    if ((GetAsyncKeyState(VK_OEM_3) & 0x8000) && ((GetTickCount64() - LastTick) > 500)) {
      DX11Base::bToggleMenuCollapseRequest = true;
      g_Engine->bShowMenu = true; // ������ �޴��� ǥ�û��¸� ����
      LastTick = GetTickCount64();
    }

    // if (GetAsyncKeyState(VK_END) & 0x8000)
    //{
    //     g_KillSwitch = true;
    // }
    std::this_thread::sleep_for(30ms);
  }

  //  EXIT
  DX11Base::Shutdown(false); // ����� ���� ������ ���� ��Ȳ

  // join() ��⸦ �����Ͽ� ���� �� ����¡ ����
  // if (WCMUpdate.joinable())
  //     WCMUpdate.join();

  FreeLibraryAndExitThread(g_hModule, EXIT_SUCCESS);
  return EXIT_SUCCESS;
}
