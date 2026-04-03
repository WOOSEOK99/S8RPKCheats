#pragma once
#include "pch.h"

namespace DX11Base {
    namespace MenuSections {
        // 공통 UI 헬퍼
        void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase, float scale);

        // 섹션별 그리기 함수
        void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale);
        void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale);
        void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale);
        void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale);
    }
}
