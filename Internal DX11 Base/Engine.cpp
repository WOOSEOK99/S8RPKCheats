#include "Engine.h"
#include "Cheats.h"
#include "Cheats/System/SpeedHack.h"
#include "Fonts.h"
#include "Menu.h"
#include "MenuState.h"
#include "pch.h"
#include "resource.h"
#include "showlog.h"
#include "debug.h"
#include <map>

// ImGui Win32 Handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Forward declaration of RoninMonitor_Draw and LoadTextureFromResource
namespace DX11Base {
  extern void RoninMonitor_Draw();
}
extern bool LoadTextureFromResource(ID3D11Device *pDevice, int resource_id, ID3D11ShaderResourceView **out_srv,
                                    int *out_width, int *out_height);
extern std::map<int, void *> g_RangeTextures;

namespace DX11Base {
  Engine::Engine() {
    g_Hooking = std::make_unique<Hooking>();
    g_RenderManager = std::make_unique<RenderManager>();
  }
  Engine::~Engine() {
    g_Hooking.reset();
    g_RenderManager.reset();
  }

  RenderManager::RenderManager() {}
  RenderManager::~RenderManager() {
    if (bInitImGui) {
      if (m_RenderType == RenderType::DX11) {
        ImGui_ImplDX11_Shutdown();
      } else if (m_RenderType == RenderType::DX12) {
        ImGui_ImplDX12_Shutdown();
      }
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

    if (m_pd3dDevice12) {
      m_pd3dDevice12->Release();
      m_pd3dDevice12 = nullptr;
    }
    if (m_pd3dRtvDescHeap) {
      m_pd3dRtvDescHeap->Release();
      m_pd3dRtvDescHeap = nullptr;
    }
    if (m_pd3dSrvDescHeap) {
      m_pd3dSrvDescHeap->Release();
      m_pd3dSrvDescHeap = nullptr;
    }
    if (m_pd3dCommandList) {
      m_pd3dCommandList->Release();
      m_pd3dCommandList = nullptr;
    }
    for (int i = 0; i < 8; i++) {
      if (m_pd3dCommandAllocators[i]) {
        m_pd3dCommandAllocators[i]->Release();
        m_pd3dCommandAllocators[i] = nullptr;
      }
      if (m_pID3D12Resource[i]) {
        m_pID3D12Resource[i]->Release();
        m_pID3D12Resource[i] = nullptr;
      }
    }

    bInit = false;
  }

  LRESULT RenderManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool bAnyUIOpen = DX11Base::IsAnyUIOpen();
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

    if (g_RenderManager && g_RenderManager->m_OldWndProc)
      return CallWindowProc(g_RenderManager->m_OldWndProc, hWnd, msg, wParam, lParam);

    return DefWindowProc(hWnd, msg, wParam, lParam);
  }

  void WINAPI RenderManager::ExecuteCommandLists_hook(ID3D12CommandQueue *pQueue, UINT NumCommandLists,
                                                      ID3D12CommandList *const *ppCommandLists) {
    if (g_RenderManager && !g_RenderManager->m_pd3dCommandQueue) {
      if (pQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        g_RenderManager->m_pd3dCommandQueue = pQueue;
        AddLog(u8"[System] DX12 Command Queue 포착 성공!");
      }
    }
    return g_RenderManager->oExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
  }

  HRESULT APIENTRY RenderManager::SwapChain_Present_hook(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
    if (DX11Base::IsAnyUIOpen()) {
      g_RenderManager->Overlay(pSwapChain);
    }
    return g_RenderManager->IDXGISwapChain_Present_stub(pSwapChain, SyncInterval, Flags);
  }

  HRESULT APIENTRY RenderManager::SwapChain_ResizeBuffers_hook(IDXGISwapChain *p, UINT bufferCount, UINT Width,
                                                               UINT Height, DXGI_FORMAT fmt, UINT scFlags) {
    if (g_RenderManager->m_RenderType == RenderType::DX11 && g_RenderManager->m_RenderTargetView) {
      g_RenderManager->m_RenderTargetView->Release();
      g_RenderManager->m_RenderTargetView = nullptr;
    }
    // DX12 handles resizing differently, usually by releasing backbuffer resources

    HRESULT result = g_RenderManager->IDXGISwapChain_ResizeBuffers_stub(p, bufferCount, Width, Height, fmt, scFlags);

    if (g_RenderManager->m_RenderType == RenderType::DX11) {
      ID3D11Texture2D *backBuffer;
      p->GetBuffer(0, __uuidof(ID3D11Texture2D *), (LPVOID *)&backBuffer);
      if (backBuffer) {
        g_RenderManager->m_Device->CreateRenderTargetView(backBuffer, 0, &g_RenderManager->m_RenderTargetView);
        backBuffer->Release();
      }
    }

    if (g_RenderManager->bInitImGui) {
      ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
    }
    return result;
  }

  bool RenderManager::HookD3D() {
    if (GetD3DContext()) {
      AddLog(u8"[System] D3D VTable 가로채기 시작...");
      Hooking::CreateHook((void *)MethodsTable[IDXGI_PRESENT], &SwapChain_Present_hook,
                          (void **)&IDXGISwapChain_Present_stub);
      // We don't hook ResizeBuffers yet for stability, but we can if needed

      bInit = true;
      DX11Base::SpeedHack_Sleep_Install();
      AddLog(u8"[Success] Direct3D 후킹 성공!");
      return true;
    }
    return false;
  }

  void RenderManager::UnhookD3D() {
    if (g_Engine && g_Engine->pGameWindow && m_OldWndProc) {
      SetWindowLongPtr(g_Engine->pGameWindow, GWLP_WNDPROC, (LONG_PTR)m_OldWndProc);
      m_OldWndProc = nullptr;
    }
    MH_DisableHook(MH_ALL_HOOKS);
  }

  bool RenderManager::GetD3DContext() {
    if (!InitWindow())
      return false;

    // DX11 dummy to get VTable
    D3D_FEATURE_LEVEL FeatureLevel;
    const D3D_FEATURE_LEVEL FeatureLevels[] = {D3D_FEATURE_LEVEL_11_0};
    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = 1;
    SwapChainDesc.OutputWindow = WindowHwnd;
    SwapChainDesc.Windowed = 1;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain *SwapChain;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, FeatureLevels, 1, D3D11_SDK_VERSION,
                                      &SwapChainDesc, &SwapChain, &Device, &FeatureLevel, &Context) >= 0) {
      MethodsTable = (uint64_t *)::calloc(255, sizeof(uint64_t));
      memcpy(MethodsTable, *(uint64_t **)SwapChain, 18 * sizeof(uint64_t));
      SwapChain->Release();
      Device->Release();
      Context->Release();
    }

    DeleteWindow();
    return true;
  }

  bool RenderManager::InitWindow() {
    WindowClass.cbSize = sizeof(WNDCLASSEX);
    WindowClass.style = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = DefWindowProc;
    WindowClass.hInstance = GetModuleHandle(NULL);
    WindowClass.lpszClassName = L"MJ_INIT";
    RegisterClassEx(&WindowClass);
    WindowHwnd = CreateWindow(WindowClass.lpszClassName, L"DX Init", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL,
                              WindowClass.hInstance, NULL);
    return (WindowHwnd != NULL);
  }

  bool RenderManager::DeleteWindow() {
    DestroyWindow(WindowHwnd);
    UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
    return true;
  }

  bool RenderManager::InitImGui(IDXGISwapChain *swapChain) {
    m_pSwapChain = swapChain;

    // Auto-detect API
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), (void **)&m_Device))) {
      m_RenderType = RenderType::DX11;
      AddLog(u8"[System] DX11 환경 감지됨");
    } else if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void **)&m_pd3dDevice12))) {
      m_RenderType = RenderType::DX12;
      AddLog(u8"[System] DX12 환경 감지됨");

      // If DX12, we MUST capture the command queue first
      if (!m_pd3dCommandQueue) {
        // Hook ExecuteCommandLists to capture the queue
        void **vtable = *(void ***)m_pd3dDevice12; // Wait, we need the command queue's vtable.
        // Actually we hook the queue's vtable when it's first used or find a way to get it.
        // Usually, games use one main command queue.
        // Let's assume we capture it via a hook on ExecuteCommandLists.
        // Since we haven't hooked it yet (we only hook SwapChain::Present),
        // we'll wait for the hook to catch it.
        AddLog(u8"[Warn] DX12 Command Queue 대기 중...");
        // To catch the queue, we need to hook ID3D12CommandQueue::ExecuteCommandLists
        // But we don't have a queue instance yet. We can hook the vtable of a dummy queue.
        ID3D12CommandQueue *dummyQueue = nullptr;
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(
                m_pd3dDevice12->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void **)&dummyQueue))) {
          void **vtable = *(void ***)dummyQueue;
          Hooking::CreateHook((void *)vtable[10], &ExecuteCommandLists_hook, (void **)&oExecuteCommandLists);
          dummyQueue->Release();
        }
        return false;
      }
    }

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

    // --- Premium Gold Theme (Enhanced) ---
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10, 10);
    style.WindowRounding = 8.0f;
    style.FramePadding = ImVec2(5, 2); // y 줄여서 행 높이 감소
    style.FrameRounding = 4.0f;
    style.ItemSpacing = ImVec2(8, 4); // y 줄여서 행 간격 감소
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 15.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize = 8.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    auto *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 0.75f, 0.00f, 0.50f); // More visible Gold
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.38f, 0.38f, 0.39f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.80f, 0.00f, 1.00f); // Vibrant Gold
    colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 0.75f, 0.00f, 0.70f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.75f, 0.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(1.00f, 0.75f, 0.00f, 0.18f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.75f, 0.00f, 0.38f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.75f, 0.00f, 0.58f);
    colors[ImGuiCol_Header] = ImVec4(1.00f, 0.75f, 0.00f, 0.22f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 0.75f, 0.00f, 0.42f);
    colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 0.75f, 0.00f, 0.62f);
    colors[ImGuiCol_Separator] = ImVec4(1.00f, 0.75f, 0.00f, 0.32f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(1.00f, 0.75f, 0.00f, 0.52f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(1.00f, 0.75f, 0.00f, 0.72f);
    colors[ImGuiCol_Tab] = ImVec4(1.00f, 0.75f, 0.00f, 0.18f);
    colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 0.75f, 0.00f, 0.42f);
    colors[ImGuiCol_TabSelected] = ImVec4(1.00f, 0.75f, 0.00f, 0.58f);

    // --- Font Loading (Korean + Special Symbols mixed font fix) ---
    // ▶ 같은 기하도형 특수문자(U+25A0-U+25FF)가 Malgun에 없어서
    // fallback 폰트로 렌더링될 때 너비 계산이 달라지는 문제 수정.
    // 커스텀 글리프 범위로 한글 + 특수문자를 한 폰트에서 모두 처리.
    static const ImWchar s_koreanPlusSymbols[] = {0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
                                                  0x2000, 0x206F, // General Punctuation
                                                  0x2190, 0x21FF, // Arrows
                                                  0x2500, 0x257F, // Box Drawing
                                                  0x25A0, 0x25FF, // Geometric Shapes (▶ U+25B6 등)
                                                  0x2600, 0x26FF, // Miscellaneous Symbols
                                                  0x3000, 0x303F, // CJK Symbols and Punctuation
                                                  0xAC00, 0xD7A3, // Hangul Syllables (가-힣)
                                                  0x0000};

    ImFontConfig font_config;
    font_config.PixelSnapH = true;
    font_config.OversampleH = 2;
    font_config.OversampleV = 1;
    font_config.GlyphExtraSpacing.x = -1.0f; // 자간 더 좁힘

    float baseFontSize = 15.0f * scale; // DX11 시절과 비슷한 크기로 축소
    bool fontLoaded = false;
    char dllPath[MAX_PATH];
    if (GetModuleFileNameA(g_hModule, dllPath, MAX_PATH)) {
      std::filesystem::path p(dllPath);
      std::filesystem::path fontDir = p.parent_path();
      std::filesystem::path malgunPath = fontDir / "malgunbd.ttf";
      if (!std::filesystem::exists(malgunPath))
        malgunPath = fontDir / "malgun.ttf";

      if (std::filesystem::exists(malgunPath)) {
        io.Fonts->AddFontFromFileTTF(malgunPath.string().c_str(), baseFontSize, &font_config, s_koreanPlusSymbols);
        fontLoaded = true;
      }
    }

    if (!fontLoaded) {
      const char *sysFont = "C:\\Windows\\Fonts\\malgun.ttf";
      if (std::filesystem::exists(sysFont)) {
        io.Fonts->AddFontFromFileTTF(sysFont, baseFontSize, &font_config, s_koreanPlusSymbols);
        fontLoaded = true;
      }
    }

    if (!fontLoaded) {
      font_config.FontDataOwnedByAtlas = false;
      io.Fonts->AddFontFromMemoryTTF((void *)RudaBold, sizeof(RudaBold), baseFontSize, &font_config,
                                     io.Fonts->GetGlyphRangesKorean());
    }

    // --- 특수문자 AdvanceX 보정 (잠시 비활성화하여 입력 버그 원인 테스트) ---
    /*
    io.Fonts->Build();
    if (io.Fonts->Fonts.Size > 0) {
      ImFont *font = io.Fonts->Fonts[0];
      for (ImFontGlyph &g : font->Glyphs) {
        ImWchar cp = (ImWchar)g.Codepoint;
        if ((cp >= 0x2190 && cp <= 0x21FF) || // Arrows
            (cp >= 0x2500 && cp <= 0x257F) || // Box Drawing
            (cp >= 0x25A0 && cp <= 0x25FF) || // Geometric Shapes (▶◀▷ 등)
            (cp >= 0x2600 && cp <= 0x26FF))   // Misc Symbols
        {
          float drawnWidth = g.X1 - g.X0;
          float newAdvance = drawnWidth + 2.0f;
          g.AdvanceX = newAdvance;
          // 실제 렌더링에 사용되는 IndexAdvanceX 조회 테이블도 갱신
          if (cp < (ImWchar)font->IndexAdvanceX.Size)
            font->IndexAdvanceX[cp] = newAdvance;
        }
      }
    }
    */

    UINT bufferCount = Desc.BufferCount;
    if (bufferCount > 8)
      bufferCount = 8;

    if (m_RenderType == RenderType::DX11) {
      m_Device->GetImmediateContext(&m_DeviceContext);
      ID3D11Texture2D *BackBuffer;
      swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&BackBuffer);
      m_Device->CreateRenderTargetView(BackBuffer, NULL, &m_RenderTargetView);
      BackBuffer->Release();

      ImGui_ImplWin32_Init(g_Engine->pGameWindow);
      ImGui_ImplDX11_Init(m_Device, m_DeviceContext);
    } else {
      // DX12 Init
      D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
      srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      srvDesc.NumDescriptors = 1;
      srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (FAILED(m_pd3dDevice12->CreateDescriptorHeap(&srvDesc, __uuidof(ID3D12DescriptorHeap),
                                                      (void **)&m_pd3dSrvDescHeap)))
        return false;

      D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
      rtvDesc.NumDescriptors = bufferCount;
      rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      if (FAILED(m_pd3dDevice12->CreateDescriptorHeap(&rtvDesc, __uuidof(ID3D12DescriptorHeap),
                                                      (void **)&m_pd3dRtvDescHeap)))
        return false;

      SIZE_T rtvDescriptorSize = m_pd3dDevice12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

      for (UINT i = 0; i < bufferCount; i++) {
        m_mainRtvDescriptor[i] = rtvHandle;
        ID3D12Resource *pBackBuffer = nullptr;
        swapChain->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&pBackBuffer);
        m_pd3dDevice12->CreateRenderTargetView(pBackBuffer, NULL, rtvHandle);
        m_pID3D12Resource[i] = pBackBuffer;
        rtvHandle.ptr += rtvDescriptorSize;
      }

      ImGui_ImplWin32_Init(g_Engine->pGameWindow);

      ImGui_ImplDX12_Init(m_pd3dDevice12, bufferCount, Desc.BufferDesc.Format, m_pd3dSrvDescHeap,
                          m_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                          m_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

      for (UINT i = 0; i < bufferCount; i++) {
        m_pd3dDevice12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                               (void **)&m_pd3dCommandAllocators[i]);
      }
      m_pd3dDevice12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pd3dCommandAllocators[0], NULL,
                                        __uuidof(ID3D12GraphicsCommandList), (void **)&m_pd3dCommandList);
      m_pd3dCommandList->Close();
    }

    m_OldWndProc = (WNDPROC)SetWindowLongPtr(g_Engine->pGameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
    io.ImeWindowHandle = g_Engine->pGameWindow;
    bInitImGui = true;
    AddLog(u8"[Success] ImGui 초기화 완료 (API: %s)", m_RenderType == RenderType::DX11 ? "DX11" : "DX12");
    return true;
  }

  void RenderManager::Overlay(IDXGISwapChain *pSwapChain) {
    if (!bInitImGui) {
      if (!InitImGui(pSwapChain))
        return;
    }

    if (m_RenderType == RenderType::DX11) {
      ImGui_ImplDX11_NewFrame();
    } else {
      ImGui_ImplDX12_NewFrame();
    }
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    static bool s_prevAnyUIOpen = false;
    static bool s_prevWantText = false;
    bool currAnyUIOpen = DX11Base::IsAnyUIOpen();
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

    if (currAnyUIOpen) {
      Menu::Render();
    }
    RoninMonitor_Draw();

    ImGui::Render();

    if (m_RenderType == RenderType::DX11) {
      m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, NULL);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    } else {
      IDXGISwapChain3 *pSwapChain3 = (IDXGISwapChain3 *)pSwapChain;
      UINT backBufferIdx = pSwapChain3->GetCurrentBackBufferIndex();

      ID3D12CommandAllocator *allocator = m_pd3dCommandAllocators[backBufferIdx];
      allocator->Reset();

      m_pd3dCommandList->Reset(allocator, NULL);

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = m_pID3D12Resource[backBufferIdx];
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_pd3dCommandList->ResourceBarrier(1, &barrier);

      m_pd3dCommandList->OMSetRenderTargets(1, &m_mainRtvDescriptor[backBufferIdx], FALSE, NULL);
      m_pd3dCommandList->SetDescriptorHeaps(1, &m_pd3dSrvDescHeap);

      ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_pd3dCommandList);

      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      m_pd3dCommandList->ResourceBarrier(1, &barrier);

      m_pd3dCommandList->Close();
      m_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&m_pd3dCommandList);
    }
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
} // namespace DX11Base