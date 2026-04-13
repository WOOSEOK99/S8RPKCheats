#pragma once
#include "pch.h"
#include "Engine.h"
#include "Menu.h"
#include "Cheats.h"
#include "Config.h"
#include "debug.h"
#include "MenuState.h"
#include "Cheats/SpeedHack.h"

#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

// --- 異붽???硫붾え由??섏젙 濡쒖쭅 ---
#define TRAITS_PTR_OFFSET 0x57A4B8 

#ifndef WM_IME_FIRST
#define WM_IME_FIRST 0x0281
#endif
#ifndef WM_IME_LAST
#define WM_IME_LAST 0x0288
#endif

void ApplyGoldCheat(int amount) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

    // ?덉쟾?섍쾶 泥?踰덉㎏ ?ъ씤???쎄린
    uintptr_t* step1Ptr = (uintptr_t*)(exeBase + 0x0); // 踰좎씠??二쇱냼
    if (IsBadReadPtr(step1Ptr, sizeof(uintptr_t))) return;

    uintptr_t step1 = *step1Ptr;
    uintptr_t* traitsPtr = (uintptr_t*)(step1 + TRAITS_PTR_OFFSET);
    if (IsBadReadPtr(traitsPtr, sizeof(uintptr_t))) return;

    uintptr_t traitsBaseAddress = *traitsPtr;
    if (traitsBaseAddress != 0) {
        // 湲?Gold) ?ㅽ봽???곸슜
        int* goldPtr = (int*)(traitsBaseAddress + 0x40);
        if (!IsBadWritePtr(goldPtr, sizeof(int))) {
            *goldPtr = amount;
        }
    }
}


typedef BOOL(WINAPI* PGetCursorPos)(LPPOINT);
static PGetCursorPos oGetCursorPos = NULL;

typedef SHORT(WINAPI* PGetAsyncKeyState)(int);
static PGetAsyncKeyState oGetAsyncKeyState = NULL;

typedef SHORT(WINAPI* PGetKeyState)(int);
static PGetKeyState oGetKeyState = NULL;

typedef BOOL(WINAPI* PGetKeyboardState)(PBYTE);
static PGetKeyboardState oGetKeyboardState = NULL;

typedef BOOL(WINAPI* PPeekMessageW)(LPMSG, HWND, UINT, UINT, UINT);
static PPeekMessageW oPeekMessageW = NULL;

typedef BOOL(WINAPI* PPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);
static PPeekMessageA oPeekMessageA = NULL;

SHORT WINAPI hkGetAsyncKeyState(int vKey) {
    if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
        // [중요] 한글 입력기(IME)와 UI가 쉬프트/컨트롤/알트 상태를 정확히 인지해야 쌍자음 등이 가능합니다.
        // 따라서 시스템 기능키들은 차단하지 않고 실제 값을 반환합니다.
        bool isModifier = (vKey == VK_SHIFT || vKey == VK_LSHIFT || vKey == VK_RSHIFT ||
                           vKey == VK_CONTROL || vKey == VK_LCONTROL || vKey == VK_RCONTROL ||
                           vKey == VK_MENU || vKey == VK_LMENU || vKey == VK_RMENU);
        
        // 한/영(0x15), 백틱(VK_OEM_3) 및 모든 수정 키(Modifier)는 통과
        if (ImGui::GetIO().WantCaptureKeyboard && !isModifier && vKey != VK_OEM_3 && vKey != 0x15 && vKey != 0xA5) {
            return 0;
        }
    }
    return oGetAsyncKeyState(vKey);
}

SHORT WINAPI hkGetKeyState(int vKey) {
    if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
        bool isModifier = (vKey == VK_SHIFT || vKey == VK_LSHIFT || vKey == VK_RSHIFT ||
                           vKey == VK_CONTROL || vKey == VK_LCONTROL || vKey == VK_RCONTROL ||
                           vKey == VK_MENU || vKey == VK_LMENU || vKey == VK_RMENU);

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
            // 보존할 키들의 상태를 수동으로 백업
            BYTE tilde  = lpKeyState[VK_OEM_3];
            BYTE hangul = lpKeyState[0x15]; 
            BYTE ralt   = lpKeyState[0xA5];
            BYTE shift  = lpKeyState[VK_SHIFT];
            BYTE lshift = lpKeyState[VK_LSHIFT];
            BYTE rshift = lpKeyState[VK_RSHIFT];
            BYTE ctrl   = lpKeyState[VK_CONTROL];
            BYTE lctrl  = lpKeyState[VK_LCONTROL];
            BYTE rctrl  = lpKeyState[VK_RCONTROL];
            BYTE alt    = lpKeyState[VK_MENU];
            BYTE lalt   = lpKeyState[VK_LMENU];

            // 전체를 0으로 밀어버리되, 위에서 백업한 키들만 복구 (시스템/IME용)
            memset(lpKeyState, 0, 256);
            lpKeyState[VK_OEM_3]  = tilde;
            lpKeyState[0x15]      = hangul; 
            lpKeyState[0xA5]      = ralt;
            lpKeyState[VK_SHIFT]  = shift;
            lpKeyState[VK_LSHIFT] = lshift;
            lpKeyState[VK_RSHIFT] = rshift;
            lpKeyState[VK_CONTROL]= ctrl;
            lpKeyState[VK_LCONTROL]= lctrl;
            lpKeyState[VK_RCONTROL]= rctrl;
            lpKeyState[VK_MENU]   = alt;
            lpKeyState[VK_LMENU]  = lalt;
        }
    }
    return result;
}

// OS가 메시지를 가져갈 때 Raw Input만 제거하여 단축키 차단, 키 메시지는 강제 변환
void HandleMessageCapture(LPMSG lpMsg) {
    if (!DX11Base::g_Engine || !DX11Base::IsAnyUIOpen() || !ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();

    // 1. 단축키 방지: Raw Input 인터셉트
    if (lpMsg->message == WM_INPUT && io.WantCaptureKeyboard) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0) {
            BYTE buf[1024];
            if (dwSize <= sizeof(buf) && GetRawInputData((HRAWINPUT)lpMsg->lParam, RID_INPUT, buf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)buf;
                if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    USHORT vkey = raw->data.keyboard.VKey;
                    bool isModifier = (vkey == VK_SHIFT || vkey == VK_LSHIFT || vkey == VK_RSHIFT ||
                                       vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL ||
                                       vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU);
                    bool isImeKey = (vkey == 0x15 || vkey == 0xA5 || vkey == VK_PROCESSKEY);
                    bool isMenuToggle = (vkey == VK_OEM_3);

                    // 텍스트 입력 중에는 게임 단축키가 절대 먹지 않도록 거의 모든 키를 차단.
                    // 단, IME/수정키/메뉴 토글 키는 시스템 처리에 맡긴다.
                    if (io.WantTextInput && !isModifier && !isImeKey && !isMenuToggle) {
                        lpMsg->message = WM_NULL;
                        return;
                    }

                    // 텍스트 입력이 아니더라도, 기존처럼 스페이스/백스페이스는 차단 유지.
                    if (vkey == VK_SPACE || vkey == VK_BACK) {
                        lpMsg->message = WM_NULL; 
                        return;
                    }
                    // 그 외 키는 아무것도 하지 않음 (자연스럽게 통과)
                }
            }
        }
    }

    // 2. 타이핑 활성화: 키보드 메시지 강제 변환 로직 제거
    //    게임 자체 루프에서 TranslateMessage를 호출하므로, 
    //    여기서 수동으로 호출하면 WM_CHAR가 두 번 발생하여 문자/숫자가 두 번씩 입력되는 버그가 발생합니다.
    //    따라서 해당 부분을 삭제하여 게임의 기본 메시지 펌프에 맡깁니다.
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

// [추가] 우리가 가로챈 가짜 함수
BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint) {
    BOOL result = oGetCursorPos(lpPoint); // 실제 좌표를 먼저 가져옴

    // 메뉴가 켜져 있고 "디버그 모드", "게임 화면 클릭 허용" 상태가 아닐 때 
    // (메인 메뉴 펼쳐짐, 또는 장수 리스트창 옵션 켜짐, 또는 메모리 에디터 활성화)
    // + 임구이가 마우스를 점유 중일 때만 게임 화면 밖으로 거짓말
    bool bHardBlock = !DX11Base::bIsMenuCollapsed || (DX11Base::bShowOfficerListWin && DX11Base::bBlockClickInOfficerList) || (DX11Base::bShowMemoryEditor && DX11Base::bBlockClickInMemoryEditor) || DX11Base::bShowSpecialtyInfoWin;
    if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext() && 
        ImGui::GetIO().WantCaptureMouse &&
        !DX11Base::bShowDebug && !DX11Base::bAllowGameClick && bHardBlock) {
        lpPoint->x = -1;
        lpPoint->y = -1;
    }
    return result;
}

using namespace DX11Base;

void ClientBGThread()
{
    while (g_Running)
    {
        Menu::Loops();

        if (g_KillSwitch)
        {
            g_D3D11Window->UnhookD3D();
            g_Hooking->Shutdown();
            g_Engine.release();     //  releases all created class instances
            g_Running = false;

        }

        std::this_thread::sleep_for(8ms);
    }
}

DWORD WINAPI MainThread_Initialize(LPVOID dwModule) {

    UNREFERENCED_PARAMETER(dwModule);
    // quick debug popup removed

    // 원본 게임의 DPI 인식 상태를 강제로 변경하면 마우스 포인터 좌표(WM_MOUSEMOVE)와 
    // 실제 렌더링된 GUI 좌표 사이에 심각한 오프셋(어긋남)이 발생합니다.
    // SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    // -----------------------------------------------------------

    g_Engine = std::make_unique<Engine>();
    // initialize cheats subsystem (resolve pointers)
    // try immediately, but also retry in background until game finishes loading
    DX11Base::InitCheats();
    std::thread([]() {
        int attempts = 0;
        while (attempts < 60) { // retry up to ~30 seconds
            if (DX11Base::InitCheats()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            attempts++;
        }
    }).detach();

    // [추가] 파일에서 이전 설정값만 읽어옴 (지연 적용은 Menu::Loops에서 수행)
    DX11Base::LoadConfig();

    if (DX11Base::bAutoLoadMenu) {
        g_Engine->bShowMenu = true;
    }

    g_D3D11Window->HookD3D();
    g_Hooking->Initialize();

    // 2. [추가] 훅 초기화 직후에 우리만의 좌표 속이기 훅을 설치합니다.
    // [수정] MH_EnableHook에 IAT 스텁(&GetCursorPos 등)을 직접 넘기면
    //        일부 PC에서 충돌 발생. MH_CreateHookApi 등록 후 MH_ALL_HOOKS로
    //        한꺼번에 활성화하는 방식으로 변경.
    if (oGetCursorPos == NULL) {
        MH_CreateHookApi(L"user32.dll", "GetCursorPos",      &hkGetCursorPos,      (LPVOID*)&oGetCursorPos);
        MH_CreateHookApi(L"user32.dll", "GetAsyncKeyState",  &hkGetAsyncKeyState,  (LPVOID*)&oGetAsyncKeyState);
        MH_CreateHookApi(L"user32.dll", "GetKeyState",       &hkGetKeyState,       (LPVOID*)&oGetKeyState);
        MH_CreateHookApi(L"user32.dll", "GetKeyboardState",  &hkGetKeyboardState,  (LPVOID*)&oGetKeyboardState);
        MH_CreateHookApi(L"user32.dll", "PeekMessageW",      &hkPeekMessageW,      (LPVOID*)&oPeekMessageW);
        MH_CreateHookApi(L"user32.dll", "PeekMessageA",      &hkPeekMessageA,      (LPVOID*)&oPeekMessageA);

        // 모든 등록된 훅을 한꺼번에 활성화 (안전)
        MH_EnableHook(MH_ALL_HOOKS);
    }

    //	INITIALIZE BACKGROUND THREAD
    std::thread WCMUpdate(ClientBGThread);

    //  RENDER LOOP
    g_Running = true;
    // [수정] int를 ULONGLONG으로 변경 (64비트 정수형)
    static ULONGLONG LastTick = 0;

    while (g_Running)
    {
        // 탭 키 위의 ` (물결/백틱) 키로 변경
        if ((GetAsyncKeyState(VK_OEM_3) & 0x8000) && ((GetTickCount64() - LastTick) > 500))
        {
            DX11Base::bToggleMenuCollapseRequest = true;
            g_Engine->bShowMenu = true; // 무조건 메뉴는 표시상태를 유지
            LastTick = GetTickCount64();
        }

        //if (GetAsyncKeyState(VK_END) & 0x8000)
        //{
        //    g_KillSwitch = true;
        //}
        std::this_thread::sleep_for(8ms);
    }


    //  EXIT
    WCMUpdate.join();
    FreeLibraryAndExitThread(g_hModule, EXIT_SUCCESS);
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) {
        return 1;
    }
    if (MH_Uninitialize() != MH_OK) {
        return 1;
    }
    return EXIT_SUCCESS;
}