#pragma once
#include "helper.h"

namespace DX11Base 
{
	class Engine {
	public:

		HWND	pGameWindow{ 0 };

		bool bShowMenu{ false };

		bool TestBool;
		int TestInt = 5;

		Engine();
		~Engine();
	};
	inline std::unique_ptr<Engine> g_Engine;

	enum class RenderType {
		None,
		DX11,
		DX12
	};

	class RenderManager
	{
	public:
		enum DXGI : int
		{
			IDXGI_PRESENT = 8,
			IDXGI_RESIZE_BUFFERS = 13,
		};

	public:
		bool								bInit{ false };
		bool								bInitImGui{ false };
		RenderType                          m_RenderType{ RenderType::None };
		WNDPROC								m_OldWndProc{};
		ImGuiContext* pImGui;
		ImGuiViewport* pViewport;

	public:
		bool								GetD3DContext();
		bool								HookD3D();
		void								HookWndProc(HWND hWnd);
		void								UnhookD3D();
		bool								InitWindow();
		bool								DeleteWindow();
		bool								InitImGui(IDXGISwapChain* swapChain);
		void								Overlay(IDXGISwapChain* pSwapChain);
		
		ID3D11Device*						GetDevice11() { return m_Device; }
		ID3D11DeviceContext*				GetDeviceContext11() { return m_DeviceContext; }
		
		ID3D12Device*                       GetDevice12() { return m_pd3dDevice12; }
		ID3D12CommandQueue*                 GetCommandQueue12() { return m_pd3dCommandQueue; }

	public:
		explicit RenderManager();
		~RenderManager() noexcept;

	private:
		typedef HRESULT(WINAPI* IDXGISwapChainPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
		typedef HRESULT(WINAPI* IDXGISwapChainResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
		IDXGISwapChainPresent				IDXGISwapChain_Present_stub = 0;
		IDXGISwapChainResizeBuffers			IDXGISwapChain_ResizeBuffers_stub = 0;

		typedef void(WINAPI* PExecuteCommandLists)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
		PExecuteCommandLists                oExecuteCommandLists = 0;

		WNDCLASSEX							WindowClass;
		HWND								WindowHwnd;
		
		// DX11
		ID3D11Device* m_Device{};
		ID3D11DeviceContext* m_DeviceContext{};
		ID3D11RenderTargetView* m_RenderTargetView{};
		
		// DX12
		ID3D12Device*                       m_pd3dDevice12 = nullptr;
		ID3D12CommandQueue*                 m_pd3dCommandQueue = nullptr;
		ID3D12DescriptorHeap*               m_pd3dRtvDescHeap = nullptr;
		ID3D12DescriptorHeap*               m_pd3dSrvDescHeap = nullptr;
		ID3D12GraphicsCommandList*          m_pd3dCommandList = nullptr;
		ID3D12CommandAllocator*             m_pd3dCommandAllocators[8] = {}; // For multi-frame-in-flight if needed
		ID3D12Resource*                     m_pID3D12Resource[8] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE         m_mainRtvDescriptor[8] = {};

		IDXGISwapChain* m_pSwapChain{};
		float m_DpiScale{ 1.0f };
		uint64_t*							MethodsTable = NULL;


	private:
		static LRESULT APIENTRY				WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		static HRESULT APIENTRY				SwapChain_Present_hook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
		static HRESULT WINAPI				SwapChain_ResizeBuffers_hook(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
		static void WINAPI                  ExecuteCommandLists_hook(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
	};
	inline std::unique_ptr<RenderManager> g_RenderManager;

	class Hooking
	{
	public:
		void								Initialize();
		void								Shutdown();
		static bool							CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* pOrig);
		static void							EnableHook(LPVOID pTarget);
		static void							EnableAllHooks();
		static void							DisableHook(LPVOID pTarget);
		static void							RemoveHook(LPVOID pTarget);
		static void							DisableAllHooks();
		static void							RemoveAllHooks();


	public:
		explicit Hooking();
		~Hooking() noexcept;
	};
	inline std::unique_ptr<Hooking> g_Hooking;
}