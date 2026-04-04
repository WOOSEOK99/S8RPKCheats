// =============================================================================
// SpeedHack.cpp  –  게임 배속 조절 모듈 (독립형)
// 2026-04-04
//
// [원리]
//   Windows의 QueryPerformanceCounter()를 MinHook으로 가로채서,
//   반환되는 타이머 카운트에 배율을 곱합니다.
//   게임 엔진은 이 값으로 DeltaTime을 계산하므로,
//   카운트가 빠르게 증가하면 게임 내 시간도 빠르게 흐릅니다.
//
// [주의]
//   - 배율 1.0 = 정상속도
//   - 배율 > 1.0 = 배속 (2.0 = 2배속)
//   - 배율 < 1.0 = 슬로우 (0.5 = 0.5배속)
//   - 배율이 너무 높으면 물리/AI 연산이 불안정해질 수 있습니다.
// =============================================================================

#include "SpeedHack.h"
#include "../MenuState.h"
#include "../pch.h"

// MinHook (이미 프로젝트에 포함되어 있음)
#include "../Hooking/MinHook.h"

#include <windows.h>

namespace DX11Base {

// ---------------------------------------------------------------------------
// 내부 상태
// ---------------------------------------------------------------------------
namespace {
    typedef BOOL(WINAPI* PQueryPerformanceCounter)(LARGE_INTEGER*);
    static PQueryPerformanceCounter oQPC = nullptr;
    static bool    s_installed    = false;
    static bool    s_wasEnabled   = false;   // 이전 프레임 활성 여부 (토글 감지)
    static LONGLONG s_lastReal    = 0;       // 직전 실제 QPC 값
    static LONGLONG s_accumulated = 0;       // 조작된 누적 오프셋
    static LONGLONG s_freq        = 0;       // QPC 주파수 (한번만 읽음)
}

// ---------------------------------------------------------------------------
// 후킹 함수: 실제 QPC 반환값에 배율 적용
// ---------------------------------------------------------------------------
static BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER* lpCount)
{
    BOOL result = oQPC(lpCount);
    if (!result) return result;

    if (!bSpeedHack || g_speedMultiplier <= 0.0f) {
        // 비활성 상태: 누적 오프셋만 동기화 (활성화 시 점프 방지)
        s_lastReal    = lpCount->QuadPart;
        s_accumulated = 0;
        return result;
    }

    LONGLONG real = lpCount->QuadPart;
    LONGLONG delta = real - s_lastReal;
    s_lastReal = real;

    if (delta < 0) delta = 0; // 시간 역행 방지

    // 배율 적용: delta에 multiplier를 곱하여 시간 흐름을 늘림
    LONGLONG scaled = (LONGLONG)((double)delta * (double)g_speedMultiplier);
    s_accumulated += (scaled - delta); // 원래 delta와의 차이만큼 오프셋 누적

    lpCount->QuadPart = real + s_accumulated;
    return result;
}

// ---------------------------------------------------------------------------
// SpeedHack_Install: 프로그램 시작 시 단 한 번 훅 설치
// (Source.cpp::MainThread_Initialize 마지막에 호출)
// ---------------------------------------------------------------------------
void SpeedHack_Install()
{
    if (s_installed) return;

    // QPC 주파수 저장
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    s_freq = freq.QuadPart;

    // MinHook으로 QueryPerformanceCounter 후킹
    if (MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter",
                         &hkQueryPerformanceCounter,
                         reinterpret_cast<LPVOID*>(&oQPC)) == MH_OK)
    {
        MH_EnableHook(MH_ALL_HOOKS);
        s_installed = true;
    }
}

// ---------------------------------------------------------------------------
// SpeedHack_Update: Menu::Loops() 에서 매 루프 호출
// 배속이 꺼질 때 누적 오프셋을 초기화하여 시간이 역행하지 않게 함
// ---------------------------------------------------------------------------
void SpeedHack_Update()
{
    if (!bSpeedHack && s_wasEnabled) {
        // 방금 꺼졌을 때: 오프셋 리셋
        s_accumulated = 0;
        s_lastReal    = 0;
    }
    s_wasEnabled = bSpeedHack;
}

} // namespace DX11Base
