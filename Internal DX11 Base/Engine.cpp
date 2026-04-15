#include "Engine.h"
#include "Cheats/Officer/RoninMonitor.h"
#include "Cheats/System/SpeedHack.h"
#include "Fonts.h"
#include "Framework/imgui.h"
#include "Framework/imgui_impl_dx11.h"
#include "Framework/imgui_impl_win32.h"
#include "Menu.h"
#include "MenuState.h"
#include "debug.h"
#include "pch.h"
#include "showlog.h"
#include <imm.h>
#include <windowsx.h>

#pragma comment(lib, "imm32.lib")

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static uint64_t *MethodsTable{nullptr};

namespace DX11Base {
  // 모든 UI 창 중 하나라도 열려 있는지 확인하는 헬퍼 함수
  bool IsAnyUIOpen() {
    return (g_Engine && g_Engine->bShowMenu) || bShowOfficerDetail || bShowSelectedOfficerWin || bShowOfficerListWin ||
           bShowMemoryEditor;
  }

  Engine::Engine() {
    g_D3D11Window = std::make_unique<D3D11Window>();
    g_Hooking = std::make_unique<Hooking>();
  }

  Engine::~Engine() {
    g_Hooking.reset();
    g_D3D11Window.reset();
  }

  D3D11Window::D3D11Window() {}

  D3D11Window::~D3D11Window() {
    if (bInitImGui) {
      ImGui_ImplDX11_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
      bInitImGui = false;
    }

    if (m_RenderTargetView) {
      m_RenderTargetView->Release();
      m_RenderTargetView = nullptr;
    }
    if (m_DeviceContext) {
      m_DeviceContext->Release();
      m_DeviceContext = nullptr;
    }
    if (m_Device) {
      m_Device->Release();
      m_Device = nullptr;
    }
    bInit = false;
  }

  LRESULT D3D11Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool bAnyUIOpen = IsAnyUIOpen();
    bool bWantKbd = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;

    if (bAnyUIOpen) {
      // 1. IME 전처리: 조합창(미리보기) 강제 표시 설정
      if (msg == WM_IME_SETCONTEXT) {
        lParam |= ISC_SHOWUICOMPOSITIONWINDOW;
        return DefWindowProc(hWnd, msg, TRUE, lParam);
      }

      // 2. 한/영 키 처리
      if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (wParam == 0x15 || wParam == 0xA5) {
          HIMC hIMC = ImmGetContext(hWnd);
          if (hIMC) {
            DWORD dwConv, dwSent;
            if (ImmGetConversionStatus(hIMC, &dwConv, &dwSent)) {
              if (dwConv & IME_CMODE_NATIVE)
                dwConv = IME_CMODE_ALPHANUMERIC;
              else
                dwConv = IME_CMODE_NATIVE | IME_CMODE_ROMAN;
              ImmSetConversionStatus(hIMC, dwConv, dwSent);
            }
            ImmReleaseContext(hWnd, hIMC);
          }
          DefWindowProc(hWnd, msg, wParam, lParam);
          return 0;
        }
      }

      // 3. IME 입력 완성 처리 (한글 씹힘 방지 + 미리보기 지원)
      if (msg == WM_IME_COMPOSITION) {
        if (lParam & GCS_RESULTSTR) {
          HIMC himc = ImmGetContext(hWnd);
          if (himc) {
            int byteLen = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
            if (byteLen > 0 && byteLen < 256) {
              wchar_t buf[64] = {};
              ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf, sizeof(buf));
              ImGuiIO &io = ImGui::GetIO();
              for (int i = 0; i < byteLen / (int)sizeof(wchar_t); i++) {
                if (buf[i] && buf[i] >= 0x0080) {
                  io.AddInputCharacterUTF16((unsigned short)buf[i]);
                }
              }
            }
            ImmReleaseContext(hWnd, himc);
          }
        }
        // [중요] DefWindowProc를 호출해야 OS 조합창(미리보기)이 뜹니다.
        // ImGui_ImplWin32_WndProcHandler는 여기서 처리하지 않고 아래에서 통합 관리
        return DefWindowProc(hWnd, msg, wParam, lParam);
      }

      // 4. 중복 입력 방지: 이미 GCS_RESULTSTR에서 처리한 글자(WM_IME_CHAR, WM_CHAR)는 차단
      if (msg == WM_IME_CHAR)
        return 0;
      if (msg == WM_CHAR) {
        if (wParam >= 0x0080)
          return 0; // 한글/특수문자는 위에서 처리됨
      }

      // 5. 기타 키보드 메시지 ImGui 전달 및 게임 차단
      if ((msg >= 0x0100 && msg <= 0x010F) || (msg >= 0x0281 && msg <= 0x0291) || msg == WM_CHAR) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        // IME/시스템 키는 DefWindowProc로 전달
        return DefWindowProc(hWnd, msg, wParam, lParam);
      }

      // 6. 마우스 차단
      bool bHardBlock = !DX11Base::bIsMenuCollapsed || DX11Base::bShowOfficerDetail ||
                        DX11Base::bShowSelectedOfficerWin ||
                        (DX11Base::bShowOfficerListWin && DX11Base::bBlockClickInOfficerList) ||
                        (DX11Base::bShowMemoryEditor && DX11Base::bBlockClickInMemoryEditor);
      if (!DX11Base::bAllowGameClick && !DX11Base::bShowDebug && bHardBlock) {
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
          if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;
          return 1;
        }
      }
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
      return true;

    if (g_D3D11Window && g_D3D11Window->m_OldWndProc)
      return CallWindowProc(g_D3D11Window->m_OldWndProc, hWnd, msg, wParam, lParam);

    return DefWindowProc(hWnd, msg, wParam, lParam);
  }
#if false // 기존 코드
  HRESULT APIENTRY D3D11Window::SwapChain_Present_hook(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
    // g_D3D11Window->Overlay(pSwapChain);
    // 수정
    if (IsAnyUIOpen() || ImGui::GetIO().WantTextInput) {
      g_D3D11Window->Overlay(pSwapChain);
    }
    return g_D3D11Window->IDXGISwapChain_Present_stub(pSwapChain, SyncInterval, Flags);
  }
#endif

  // HRESULT APIENTRY D3D11Window::SwapChain_Present_hook(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
  //   // 🔥 UI 있을 때만 Overlay 실행
  //   if (IsAnyUIOpen()) {
  //     g_D3D11Window->Overlay(pSwapChain);
  //   }

  //   return g_D3D11Window->IDXGISwapChain_Present_stub(pSwapChain, SyncInterval, Flags);
  // }

  HRESULT APIENTRY D3D11Window::SwapChain_Present_hook(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
    using namespace DX11Base;

    if (IsAnyUIOpen()) {
      g_D3D11Window->Overlay(pSwapChain);
    }

    return g_D3D11Window->IDXGISwapChain_Present_stub(pSwapChain, SyncInterval, Flags);
  }

  HRESULT APIENTRY D3D11Window::SwapChain_ResizeBuffers_hook(IDXGISwapChain *p, UINT bufferCount, UINT Width,
                                                             UINT Height, DXGI_FORMAT fmt, UINT scFlags) {
    g_D3D11Window->m_pSwapChain = p;
    if (g_D3D11Window->m_RenderTargetView) {
      g_D3D11Window->m_RenderTargetView->Release();
      g_D3D11Window->m_RenderTargetView = nullptr;
    }

    HRESULT result = g_D3D11Window->IDXGISwapChain_ResizeBuffers_stub(p, bufferCount, Width, Height, fmt, scFlags);

    ID3D11Texture2D *backBuffer;
    p->GetBuffer(0, __uuidof(ID3D11Texture2D *), (LPVOID *)&backBuffer);
    if (backBuffer) {
      g_D3D11Window->m_Device->CreateRenderTargetView(backBuffer, 0, &g_D3D11Window->m_RenderTargetView);
      backBuffer->Release();
    }

    if (g_D3D11Window->bInitImGui) {
      ImGuiIO &io = ImGui::GetIO();
      io.DisplaySize = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
    }

    return result;
  }

  bool D3D11Window::HookD3D() {
    if (GetD3DContext()) {
      AddLog(u8"[System] D3D11 VTable 가로채기 시작...");
      Hooking::CreateHook((void *)MethodsTable[IDXGI_PRESENT], &SwapChain_Present_hook,
                          (void **)&IDXGISwapChain_Present_stub);
      // Hooking::CreateHook((void *)MethodsTable[IDXGI_RESIZE_BUFFERS], &SwapChain_ResizeBuffers_hook,
      //                     (void **)&IDXGISwapChain_ResizeBuffers_stub);
      bInit = true;

      // 🔥 여기 추가
      DX11Base::SpeedHack_Sleep_Install();

      AddLog(u8"[Success] Direct3D 11 후킹 성공!");
      return true;
    }
    AddLog(u8"[Error] D3D11 컨텍스트를 가져오지 못했습니다.");
    return false;
  }

  void D3D11Window::UnhookD3D() {
    if (g_Engine && g_Engine->pGameWindow && m_OldWndProc) {
      SetWindowLongPtr(g_Engine->pGameWindow, GWLP_WNDPROC, (LONG_PTR)m_OldWndProc);
      m_OldWndProc = nullptr;
    }
    if (MethodsTable) {
      Hooking::DisableHook((void *)MethodsTable[IDXGI_PRESENT]);
      Hooking::DisableHook((void *)MethodsTable[IDXGI_RESIZE_BUFFERS]);
    }
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    if (MethodsTable) {
      free(MethodsTable);
      MethodsTable = nullptr;
    }
  }

  bool D3D11Window::GetD3DContext() {
    if (!InitWindow())
      return false;

    D3D_FEATURE_LEVEL FeatureLevel;
    const D3D_FEATURE_LEVEL FeatureLevels[] = {D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_11_0};

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    ZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));
    SwapChainDesc.BufferDesc.Width = 100;
    SwapChainDesc.BufferDesc.Height = 100;
    SwapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.SampleDesc.Quality = 0;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = 1;
    SwapChainDesc.OutputWindow = WindowHwnd;
    SwapChainDesc.Windowed = 1;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IDXGISwapChain *SwapChain;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, FeatureLevels, 1, D3D11_SDK_VERSION,
                                      &SwapChainDesc, &SwapChain, &Device, &FeatureLevel, &Context) < 0) {
      DeleteWindow();
      return false;
    }

    MethodsTable = (uint64_t *)::calloc(255, sizeof(uint64_t));
    memcpy(MethodsTable, *(uint64_t **)SwapChain, 18 * sizeof(uint64_t));
    memcpy(MethodsTable + 18, *(uint64_t **)Device, 43 * sizeof(uint64_t));
    memcpy(MethodsTable + 18 + 43, *(uint64_t **)Context, 144 * sizeof(uint64_t));

    SwapChain->Release();
    Device->Release();
    Context->Release();
    DeleteWindow();
    return true;
  }

  bool D3D11Window::InitWindow() {
    WindowClass.cbSize = sizeof(WNDCLASSEX);
    WindowClass.style = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = DefWindowProc;
    WindowClass.cbClsExtra = 0;
    WindowClass.cbWndExtra = 0;
    WindowClass.hInstance = GetModuleHandle(NULL);
    WindowClass.lpszClassName = L"MJ";
    RegisterClassEx(&WindowClass);
    WindowHwnd = CreateWindow(WindowClass.lpszClassName, L"DX11 Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL,
                              NULL, WindowClass.hInstance, NULL);
    return (WindowHwnd != NULL);
  }

  bool D3D11Window::DeleteWindow() {
    DestroyWindow(WindowHwnd);
    UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
    return true;
  }

  bool D3D11Window::InitImGui(IDXGISwapChain *swapChain) {
    m_pSwapChain = swapChain;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), (void **)&m_Device))) {
      ImGui::CreateContext();
      ImGuiIO &io = ImGui::GetIO();

      DXGI_SWAP_CHAIN_DESC Desc;
      swapChain->GetDesc(&Desc);
      g_Engine->pGameWindow = Desc.OutputWindow;

      UINT dpi = GetDpiForWindow(g_Engine->pGameWindow);
      float scale = (dpi == 0) ? 1.0f : (float)dpi / 96.0f;
      m_DpiScale = scale;

      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.IniFilename = 0;
      io.ConfigWindowsMoveFromTitleBarOnly = true;

      ImGui::StyleColorsDark();
      ImGuiStyle &style = ImGui::GetStyle();

      float roundingVal = 7.0f * scale;

      style.WindowRounding = roundingVal;
      style.FrameRounding = roundingVal;
      style.PopupRounding = roundingVal;
      style.GrabRounding = roundingVal;
      style.TabRounding = roundingVal;
      style.ScrollbarRounding = roundingVal;

      style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.07f, 1.0f);

      ImVec4 textNormal = ImVec4(0.98f, 0.95f, 0.85f, 1.0f);
      style.Colors[ImGuiCol_Text] = textNormal;

      ImVec4 bronzeNormal = ImVec4(0.20f, 0.15f, 0.05f, 1.0f);
      ImVec4 goldBright = ImVec4(1.00f, 0.88f, 0.35f, 1.0f);
      ImVec4 goldYellow = ImVec4(1.00f, 1.00f, 0.65f, 1.0f);

      style.Colors[ImGuiCol_Button] = bronzeNormal;
      style.Colors[ImGuiCol_ButtonHovered] = goldBright;
      style.Colors[ImGuiCol_ButtonActive] = goldYellow;

      style.Colors[ImGuiCol_CheckMark] = goldBright;
      style.Colors[ImGuiCol_SliderGrab] = goldBright;
      style.Colors[ImGuiCol_SliderGrabActive] = goldYellow;

      style.Colors[ImGuiCol_Header] = ImVec4(goldBright.x, goldBright.y, goldBright.z, 0.25f);
      style.Colors[ImGuiCol_HeaderHovered] = ImVec4(goldBright.x, goldBright.y, goldBright.z, 0.75f);
      style.Colors[ImGuiCol_HeaderActive] = ImVec4(goldBright.x, goldBright.y, goldBright.z, 1.00f);

      style.Colors[ImGuiCol_Tab] = ImVec4(bronzeNormal.x, bronzeNormal.y, bronzeNormal.z, 0.70f);
      style.Colors[ImGuiCol_TabHovered] = goldBright;
      style.Colors[ImGuiCol_TabActive] = ImVec4(goldBright.x, goldBright.y, goldBright.z, 0.85f);
      style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(goldBright.x, goldBright.y, goldBright.z, 0.40f);

      style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.10f, 0.05f, 1.0f);
      style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.20f, 0.10f, 1.0f);
      style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.30f, 0.15f, 1.0f);

      style.ScaleAllSizes(scale);

      m_Device->GetImmediateContext(&m_DeviceContext);

      float baseFontSize = 16.5f * scale;
      ImFontConfig font;
      font.PixelSnapH = true;
      io.Fonts->AddFontFromMemoryTTF((void *)RudaBold, sizeof(RudaBold), baseFontSize, &font);

      ImFontConfig koFont;
      koFont.MergeMode = true;
      koFont.PixelSnapH = true;
      static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2000, 0x2BFF, 0xAC00, 0xD7A3, 0};

      const char *fontPath = "C:\\Windows\\Fonts\\malgun.ttf";
      if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(fontPath, baseFontSize, &koFont, ranges);
      } else {
        // 폰트가 없을 경우 기본 폰트로 병합 시도 (실패 방지)
        DX11Base::AddLog(u8"[Warn] 맑은 고딕 폰트를 찾을 수 없습니다: %s", fontPath);
      }
      io.Fonts->Build();

      ID3D11Texture2D *BackBuffer;
      swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&BackBuffer);
      m_Device->CreateRenderTargetView(BackBuffer, NULL, &m_RenderTargetView);
      BackBuffer->Release();

      ImGui_ImplWin32_Init(g_Engine->pGameWindow);
      ImGui_ImplDX11_Init(m_Device, m_DeviceContext);

      io.ImeWindowHandle = g_Engine->pGameWindow;
      m_OldWndProc = (WNDPROC)SetWindowLongPtr(g_Engine->pGameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);

      bInitImGui = true;
      AddLog(u8"[Success] ImGui 초기화 완료 (폰트 스케일: %.2f)", scale);
      return true;
    }
    AddLog(u8"[Error] DXGISwapChain 장치 획득 실패.");
    return false;
  }

  void D3D11Window::Overlay(IDXGISwapChain *pSwapChain) {
    if (!bInitImGui)
      InitImGui(pSwapChain);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    static bool s_prevAnyUIOpen = false;
    static bool s_prevWantText = false;
    bool currAnyUIOpen = IsAnyUIOpen();
    bool currWantText = ImGui::GetIO().WantTextInput;

    if (currWantText && !s_prevWantText) {
      // [핵심] 한국어 레이아웃 강제 활성화 및 시스템 포커스 가로채기 (윈도우 키 효과 시뮬레이션)
      ActivateKeyboardLayout((HKL)0x0412, KLF_SETFORPROCESS);

      // 1. 강제 활성화 신호 전송
      SendMessage(g_Engine->pGameWindow, WM_ACTIVATE, WA_ACTIVE, 0);
      SetFocus(g_Engine->pGameWindow);

      // 2. IME 관련 동기화
      ImmAssociateContextEx(g_Engine->pGameWindow, NULL, IACE_DEFAULT);
      SendMessage(g_Engine->pGameWindow, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)GetKeyboardLayout(0));
      SendMessage(g_Engine->pGameWindow, WM_IME_SETCONTEXT, TRUE, ISC_SHOWUICOMPOSITIONWINDOW);

      HIMC hIMC = ImmGetContext(g_Engine->pGameWindow);
      if (hIMC) {
        // IME 후보창 위치를 현재 임구이 커서 위치로 동기화 (OS 포커스 유도)
        COMPOSITIONFORM cf;
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = (LONG)ImGui::GetCursorScreenPos().x;
        cf.ptCurrentPos.y = (LONG)ImGui::GetCursorScreenPos().y;
        ImmSetCompositionWindow(hIMC, &cf);

        ImmSetOpenStatus(hIMC, TRUE);
        ImmReleaseContext(g_Engine->pGameWindow, hIMC);
      }
    }

    s_prevAnyUIOpen = currAnyUIOpen;
    s_prevWantText = currWantText;

    // Menu::Render();

    // // 2026-04-04 재야장수 모니터링 알림상 렌더링 (RoninMonitor 모듈)
    // RoninMonitor_Draw();

    if (currAnyUIOpen) {
      Menu::Render();
    }
    RoninMonitor_Draw();

    ImGui::EndFrame();
    ImGui::Render();
    m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }

  Hooking::Hooking() { MH_Initialize(); }
  Hooking::~Hooking() { MH_Uninitialize(); }
  void Hooking::Initialize() { MH_EnableHook(MH_ALL_HOOKS); }
  void Hooking::Shutdown() { MH_DisableHook(MH_ALL_HOOKS); }
  bool Hooking::CreateHook(LPVOID lpTarget, LPVOID pDetour, LPVOID *pOrig) {
    if (MH_CreateHook(lpTarget, pDetour, pOrig) != MH_OK || MH_EnableHook(lpTarget) != MH_OK)
      return false;
    return true;
  }
  void Hooking::EnableHook(LPVOID lpTarget) { MH_EnableHook(lpTarget); }
  void Hooking::DisableHook(LPVOID lpTarget) { MH_DisableHook(lpTarget); }
  void Hooking::RemoveHook(LPVOID lpTarget) { MH_RemoveHook(lpTarget); }
  void Hooking::EnableAllHooks() { MH_EnableHook(MH_ALL_HOOKS); }
  void Hooking::DisableAllHooks() { MH_DisableHook(MH_ALL_HOOKS); }
  void Hooking::RemoveAllHooks() { MH_RemoveHook(MH_ALL_HOOKS); }
} // namespace DX11Base