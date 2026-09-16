#pragma once
#include "pch.h"

namespace DX11Base {
  void DrawAIWarImproveSection(float scale);

  namespace MenuSections {
    // 공통 UI 헬퍼
    void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase,
                     float scale);

    // 섹션별 그리기 함수
    void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale);
    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale);
    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale);

#ifdef DX11BASE_MENU_WRAP_WAR_SECTION
    // Menu.cpp에서만 기존 전쟁 섹션 바로 뒤에 AI 전투 개선 UI를 추가합니다.
    inline void DrawWarSectionWithAIImprove(uintptr_t p1, uintptr_t gameBase, float scale) {
      DrawWarSection(p1, gameBase, scale);
      ::DX11Base::DrawAIWarImproveSection(scale);
    }
#define DrawWarSection DrawWarSectionWithAIImprove
#endif

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale);
  } // namespace MenuSections
} // namespace DX11Base

/*
ImVec4 White   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // 흰색
ImVec4 Black   = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // 검정
ImVec4 Red     = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // 빨강
ImVec4 Green   = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // 초록
ImVec4 Blue    = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // 파랑
ImVec4 Yellow  = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // 노랑
ImVec4 Cyan    = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); // 청록
ImVec4 Magenta = ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // 자홍
ImVec4 Orange  = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // 주황
ImVec4 Purple  = ImVec4(0.5f, 0.0f, 0.5f, 1.0f); // 보라
ImVec4 Gray    = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // 회색
ImVec4 Brown   = ImVec4(0.5f, 0.25f, 0.0f, 1.0f); // 갈색
ImVec4 Pink    = ImVec4(1.0f, 0.75f, 0.8f, 1.0f); // 분홍
ImVec4 Lime    = ImVec4(0.75f, 1.0f, 0.0f, 1.0f); // 연두
ImVec4 Teal    = ImVec4(0.0f, 0.5f, 0.5f, 1.0f); // 청록
ImVec4 Navy    = ImVec4(0.0f, 0.0f, 0.5f, 1.0f); // 남색
ImVec4 Maroon  = ImVec4(0.5f, 0.0f, 0.0f, 1.0f); // 진홍
ImVec4 Olive   = ImVec4(0.5f, 0.5f, 0.0f, 1.0f); // 올리브
ImVec4 Silver  = ImVec4(0.75f, 0.75f, 0.75f, 1.0f); // 은색
ImVec4 Gold    = ImVec4(1.0f, 0.84f, 0.0f, 1.0f); // 금색
ImVec4 Bronze  = ImVec4(0.8f, 0.5f, 0.2f, 1.0f); // 청동
ImVec4 Indigo  = ImVec4(0.29f, 0.0f, 0.51f, 1.0f); // 인디고
ImVec4 Violet  = ImVec4(0.54f, 0.17f, 0.88f, 1.0f); // 바이올렛
ImVec4 Azure   = ImVec4(0.0f, 0.5f, 1.0f, 1.0f); // 아줄
ImVec4 Coral   = ImVec4(1.0f, 0.5f, 0.31f, 1.0f); // 코랄
ImVec4 Khaki   = ImVec4(0.94f, 0.9f, 0.55f, 1.0f); // 카키
ImVec4 Lavender= ImVec4(0.9f, 0.7f, 1.0f, 1.0f); // 라벤더
ImVec4 Salmon  = ImVec4(0.98f, 0.5f, 0.45f, 1.0f); // 연어
ImVec4 Tan     = ImVec4(0.82f, 0.7f, 0.55f, 1.0f); // 황갈색
ImVec4 Turquoise= ImVec4(0.25f, 0.88f, 0.82f, 1.0f); // 터콰이즈
ImVec4 Violet2 = ImVec4(0.54f, 0.17f, 0.88f, 1.0f); // 바이올렛2
ImVec4 Wheat   = ImVec4(0.96f, 0.87f, 0.7f, 1.0f); // 밀색

ImVec4 Cyan    = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); // 하늘색
ImVec4 Magenta = ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // 자홍

ImVec4 Orange  = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // 주황
ImVec4 Purple  = ImVec4(0.5f, 0.0f, 0.5f, 1.0f); // 보라
ImVec4 Pink    = ImVec4(1.0f, 0.4f, 0.7f, 1.0f); // 핑크
ImVec4 Lime    = ImVec4(0.5f, 1.0f, 0.0f, 1.0f); // 라임
ImVec4 SkyBlue = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); // 연하늘

ImVec4 DarkRed   = ImVec4(0.5f, 0.0f, 0.0f, 1.0f);
ImVec4 DarkGreen = ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
ImVec4 DarkBlue  = ImVec4(0.0f, 0.0f, 0.5f, 1.0f);
ImVec4 Gray      = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
ImVec4 DarkGray  = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

*/