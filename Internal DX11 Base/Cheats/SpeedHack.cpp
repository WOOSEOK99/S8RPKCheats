// =============================================================================
// SpeedHack.cpp  –  게임 배속 조절 모듈 (독립형)
// 2026-04-04
//
// [원리]
//   Windows의 QueryPerformanceCounter, GetTickCount, GetTickCount64를 가로채어
//   기준점(Baseline)과 현재 시간의 차이(Delta)에 배율을 곱합니다.
//
// [멀티스레딩 최적화]
//   - 단순 static 변수를 다루면 멀티스레드 환경(랜더링, 물리, AI 스레드)에서
//     경쟁 조건(Race Condition)으로 인해 시간 값이 뒤틀리면서 게임이 프리징됩니다.
//   - 이를 방지하기 위해 std::atomic 을 사용하여 Lock-free로 완벽하게 동기화합니다.
//   - 또한 3대 시간 함수(QPC, Tick, Tick64)를 모두 맞춰주어 엔진이 시간 이상을 감지하지 않게 합니다.
// =============================================================================

#include "SpeedHack.h"
#include "../MenuState.h"
#include "../pch.h"
#include "../Hooking/MinHook.h"
#include <windows.h>
#include <atomic>

namespace DX11Base {

// ---------------------------------------------------------------------------
// 내부 상태
// ---------------------------------------------------------------------------
namespace {
    typedef BOOL(WINAPI* PQueryPerformanceCounter)(LARGE_INTEGER*);
    typedef DWORD(WINAPI* PGetTickCount)();
    typedef ULONGLONG(WINAPI* PGetTickCount64)();

    static PQueryPerformanceCounter oQPC = nullptr;
    static PGetTickCount oGetTickCount = nullptr;
    static PGetTickCount64 oGetTickCount64 = nullptr;

    static bool s_installed = false;

    // Lock-free baselines for thread safety
    static std::atomic<LONGLONG> s_baselineRealQPC{0};
    static std::atomic<LONGLONG> s_baselineFakeQPC{0};
    
    static std::atomic<DWORD> s_baselineRealTick{0};
    static std::atomic<DWORD> s_baselineFakeTick{0};
    
    static std::atomic<ULONGLONG> s_baselineRealTick64{0};
    static std::atomic<ULONGLONG> s_baselineFakeTick64{0};

    static std::atomic<float> s_effectiveMultiplier{1.0f};
}

// ---------------------------------------------------------------------------
// 후킹 함수들
// ---------------------------------------------------------------------------
static BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER* lpCount) {
    if (!oQPC(lpCount)) return FALSE;
    float mul = s_effectiveMultiplier.load(std::memory_order_relaxed);
    if (mul != 1.0f || s_baselineFakeQPC.load(std::memory_order_relaxed) != 0) {
        LONGLONG real = lpCount->QuadPart;
        LONGLONG baseReal = s_baselineRealQPC.load(std::memory_order_relaxed);
        LONGLONG baseFake = s_baselineFakeQPC.load(std::memory_order_relaxed);
        lpCount->QuadPart = baseFake + (LONGLONG)((real - baseReal) * mul);
    }
    return TRUE;
}

static DWORD WINAPI hkGetTickCount() {
    DWORD real = oGetTickCount();
    float mul = s_effectiveMultiplier.load(std::memory_order_relaxed);
    if (mul != 1.0f || s_baselineFakeTick.load(std::memory_order_relaxed) != 0) {
        DWORD baseReal = s_baselineRealTick.load(std::memory_order_relaxed);
        DWORD baseFake = s_baselineFakeTick.load(std::memory_order_relaxed);
        return baseFake + (DWORD)((real - baseReal) * mul);
    }
    return real;
}

static ULONGLONG WINAPI hkGetTickCount64() {
    ULONGLONG real = oGetTickCount64();
    float mul = s_effectiveMultiplier.load(std::memory_order_relaxed);
    if (mul != 1.0f || s_baselineFakeTick64.load(std::memory_order_relaxed) != 0) {
        ULONGLONG baseReal = s_baselineRealTick64.load(std::memory_order_relaxed);
        ULONGLONG baseFake = s_baselineFakeTick64.load(std::memory_order_relaxed);
        return baseFake + (ULONGLONG)((real - baseReal) * mul);
    }
    return real;
}

// ---------------------------------------------------------------------------
// SpeedHack_Install: 프로그램 시작 시 단 한 번 훅 설치
// ---------------------------------------------------------------------------
void SpeedHack_Install() {
    if (s_installed) return;
    
    // Initialize baselines
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    s_baselineRealQPC.store(qpc.QuadPart);
    s_baselineFakeQPC.store(qpc.QuadPart);
    
    DWORD tick = GetTickCount();
    s_baselineRealTick.store(tick);
    s_baselineFakeTick.store(tick);
    
    ULONGLONG tick64 = GetTickCount64();
    s_baselineRealTick64.store(tick64);
    s_baselineFakeTick64.store(tick64);

    if (MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", &hkQueryPerformanceCounter, (LPVOID*)&oQPC) == MH_OK &&
        MH_CreateHookApi(L"kernel32.dll", "GetTickCount", &hkGetTickCount, (LPVOID*)&oGetTickCount) == MH_OK &&
        MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", &hkGetTickCount64, (LPVOID*)&oGetTickCount64) == MH_OK) 
    {
        MH_EnableHook(MH_ALL_HOOKS);
        s_installed = true;
    }
}

// ---------------------------------------------------------------------------
// SpeedHack_Update: 배속 수치가 변경될 때만 기준점(Baseline)을 재정렬합니다.
// 게임 쓰레드와의 충돌을 막기 위해 Atomic Store를 사용합니다.
// ---------------------------------------------------------------------------
void SpeedHack_Update() {
    // 사용자가 배속을 켰는데 아직 훅이 설치되지 않았다면 최초 1회 설치합니다.
    if (bSpeedHack && !s_installed) {
        SpeedHack_Install();
    }

    // 훅이 아예 설치되지 않은 상태면 아무런 작업도 하지 않고 OS 순정 상태를 유지합니다.
    if (!s_installed) return;

    float desiredMul = bSpeedHack ? g_speedMultiplier : 1.0f;
    float currentMul = s_effectiveMultiplier.load(std::memory_order_relaxed);
    
    if (desiredMul != currentMul) {
        // Multiplier is changing, update baselines to anchor the time
        LARGE_INTEGER realQPC;
        if (oQPC && oQPC(&realQPC)) {
            LONGLONG baseReal = s_baselineRealQPC.load(std::memory_order_relaxed);
            LONGLONG baseFake = s_baselineFakeQPC.load(std::memory_order_relaxed);
            LONGLONG newFake = baseFake + (LONGLONG)((realQPC.QuadPart - baseReal) * (double)currentMul);
            s_baselineRealQPC.store(realQPC.QuadPart, std::memory_order_relaxed);
            s_baselineFakeQPC.store(newFake, std::memory_order_relaxed);
        }
        
        if (oGetTickCount && oGetTickCount64) {
            DWORD realTick = oGetTickCount();
            DWORD baseRealTick = s_baselineRealTick.load(std::memory_order_relaxed);
            DWORD baseFakeTick = s_baselineFakeTick.load(std::memory_order_relaxed);
            DWORD newFakeTick = baseFakeTick + (DWORD)((realTick - baseRealTick) * (double)currentMul);
            s_baselineRealTick.store(realTick, std::memory_order_relaxed);
            s_baselineFakeTick.store(newFakeTick, std::memory_order_relaxed);
            
            ULONGLONG realTick64 = oGetTickCount64();
            ULONGLONG baseRealTick64 = s_baselineRealTick64.load(std::memory_order_relaxed);
            ULONGLONG baseFakeTick64 = s_baselineFakeTick64.load(std::memory_order_relaxed);
            ULONGLONG newFakeTick64 = baseFakeTick64 + (ULONGLONG)((realTick64 - baseRealTick64) * (double)currentMul);
            s_baselineRealTick64.store(realTick64, std::memory_order_relaxed);
            s_baselineFakeTick64.store(newFakeTick64, std::memory_order_relaxed);
        }
        
        s_effectiveMultiplier.store(desiredMul, std::memory_order_relaxed);
    }
}

} // namespace DX11Base
