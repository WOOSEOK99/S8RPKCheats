#pragma once
#include <cstdint>

namespace DX11Base {
    // ───────────────────────────────────────────────
    //  전투맵 랜덤 셔플 모듈
    // ───────────────────────────────────────────────

    // 평정(Council) 상태를 매 프레임 전달받아 자동 제어
    void UpdateBattleMapAuto(bool isCouncil);

    // 수동 제어 및 설정 적용/해제 시 원상복구
    void SetBattleMapShuffle(bool enable);
}
