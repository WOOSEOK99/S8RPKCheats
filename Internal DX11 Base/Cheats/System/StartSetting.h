#pragma once

namespace DX11Base {
    extern bool g_startSettingEnabled;
    void SetStartSetting(bool enable);
    void SetUndiscoveredToRonin(bool enable);

    // 미발견→재야 보정은 로딩 전환을 추측하지 않고 명시적인 로드 이벤트로 1회 요청합니다.
    // gameBase 최초 획득, P1 변경, 사용자가 기능을 직접 켠 경우에 요청하고
    // 실제 무장 데이터가 준비되면 Process에서 한 번만 처리합니다.
    void RequestUndiscoveredToRoninScan(const char* reason);
    void ProcessPendingUndiscoveredToRoninScan(uintptr_t p1);
}
