#pragma once
#include "pch.h"
#include "Engine.h"
#include "Menu.h"
#include "Cheats.h"
#include "Config.h"
#include "debug.h"
#include "MenuState.h"

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
        // VK_HANGUL(0x15), VK_RMENU(0xA5): 한/영 전환 관련 키는 항상 실제 값 반환
        if (ImGui::GetIO().WantCaptureKeyboard && vKey != VK_OEM_3 && vKey != 0x15 && vKey != 0xA5) {
            return 0;
        }
    }
    return oGetAsyncKeyState(vKey);
}

SHORT WINAPI hkGetKeyState(int vKey) {
    if (DX11Base::g_Engine && DX11Base::IsAnyUIOpen() && ImGui::GetCurrentContext()) {
        // VK_HANGUL(0x15), VK_RMENU(0xA5): 한/영 전환 관련 키는 항상 실제 값 반환
        if (ImGui::GetIO().WantCaptureKeyboard && vKey != VK_OEM_3 && vKey != 0x15 && vKey != 0xA5) {
            return 0;
        }
    }
    return oGetKeyState(vKey);
}

BOOL WINAPI hkGetKeyboardState(PBYTE lpKeyState) {
    BOOL result = oGetKeyboardState(lpKeyState);
    if (result && DX11Base::g_Engine && DX11Base::g_Engine->bShowMenu && ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().WantCaptureKeyboard) {
            BYTE tilde  = lpKeyState[VK_OEM_3];
            BYTE hangul = lpKeyState[0x15]; 
            BYTE ralt   = lpKeyState[0xA5]; // 오른쪽 Alt (한영)
            memset(lpKeyState, 0, 256);
            lpKeyState[VK_OEM_3]  = tilde;
            lpKeyState[0x15]      = hangul; 
            lpKeyState[0xA5]      = ralt;
        }
    }
    return result;
}

// OS가 메시지를 가져갈 때 Raw Input만 제거하여 단축키 차단, 키 메시지는 강제 변환
void HandleMessageCapture(LPMSG lpMsg) {
    if (!DX11Base::g_Engine || !DX11Base::g_Engine->bShowMenu || !ImGui::GetCurrentContext()) return;
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
                    // 스페이스, 백스페이스만 차단 (게임 단축키 충돌 방지)
                    // 한영키(0x15) 등 나머지는 건드리지 않음 → OS가 정상 처리
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
    bool bHardBlock = !DX11Base::bIsMenuCollapsed || (DX11Base::bShowOfficerListWin && DX11Base::bBlockClickInOfficerList) || DX11Base::bShowMemoryEditor;
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

        std::this_thread::sleep_for(1ms);
        std::this_thread::yield();
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
    if (oGetCursorPos == NULL) {
        if (MH_CreateHookApi(L"user32.dll", "GetCursorPos", &hkGetCursorPos, (LPVOID*)&oGetCursorPos) == MH_OK) {
            MH_EnableHook(&GetCursorPos);
        }
        if (MH_CreateHookApi(L"user32.dll", "GetAsyncKeyState", &hkGetAsyncKeyState, (LPVOID*)&oGetAsyncKeyState) == MH_OK) {
            MH_EnableHook(&GetAsyncKeyState);
        }
        if (MH_CreateHookApi(L"user32.dll", "GetKeyState", &hkGetKeyState, (LPVOID*)&oGetKeyState) == MH_OK) {
            MH_EnableHook(&GetKeyState);
        }
        if (MH_CreateHookApi(L"user32.dll", "GetKeyboardState", &hkGetKeyboardState, (LPVOID*)&oGetKeyboardState) == MH_OK) {
            MH_EnableHook(&GetKeyboardState);
        }
        if (MH_CreateHookApi(L"user32.dll", "PeekMessageW", &hkPeekMessageW, (LPVOID*)&oPeekMessageW) == MH_OK) {
            MH_EnableHook(&PeekMessageW);
        }
        if (MH_CreateHookApi(L"user32.dll", "PeekMessageA", &hkPeekMessageA, (LPVOID*)&oPeekMessageA) == MH_OK) {
            MH_EnableHook(&PeekMessageA);
        }
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
        std::this_thread::sleep_for(1ms);
        std::this_thread::yield();
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