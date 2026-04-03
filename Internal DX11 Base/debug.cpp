#include "debug.h"
#include "Cheats.h"
#include "Cheats\InstantLoveCave.h"
#include "Cheats\OfficerDetail.h"
#include "Cheats\SelectOfficercapture.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuState.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"


namespace DX11Base {

  bool bShowDebug = false;
  static bool bShowOffset = false;

  void debuging(uintptr_t gameBase, uintptr_t p1) {
#ifdef ENABLE_DEBUG_LOG
    ImGuiIO &io = ImGui::GetIO();
    float scale = io.FontGlobalScale;

    ImGui::Separator();
    ImGui::Checkbox(u8"디버그 정보 보기", &bShowDebug);

    if (bShowDebug) {

      if (ImGui::Button(u8"기본 포인터 검색 (초기화)", ImVec2(-1, 30))) {
        DX11Base::InitCheats();
      }

      ImGui::Checkbox(u8"Offset 정보 보기", &bShowOffset);

      if (bShowOffset) {
        if (gameBase) {
          ImGui::Text("GameBase:               0x%llX", gameBase);
          ImGui::Text("Player(p1):             0x%llX", p1);
        }
      }

#if false
      if (ImGui::Button(u8"주인공 + 0x3D0 장수 정보 구조 검사", ImVec2(-1, 30))) {
        if (p1 > 0x10000) {
          uintptr_t target = p1 + 0x3D0;
          // 예외 처리를 추가하여 메모리 접근 크래시 방지
          __try {
            unsigned int tGold = *(unsigned int*)(target + 0xE8);
            unsigned short tMerit = *(unsigned short*)(target + 0x100);
            unsigned short tRepM = *(unsigned short*)(target + 0x106);
            unsigned short tRepL = *(unsigned short*)(target + 0x104);
            unsigned short tRepI = *(unsigned short*)(target + 0x108);
            unsigned char tSP = *(unsigned char*)(target + 0xED);
            
            AddLog(u8"[Debug] p1+0x3D0 (0x%llX) 접근 성공!", target);
            AddLog(u8" ├─ 자금(0xE8): %u, 공적(0x100): %u", tGold, tMerit);
            AddLog(u8" ├─ 무명(0x106): %u, 문명(0x104): %u", tRepM, tRepL);
            AddLog(u8" └─ 악명(0x108): %u, 전략P(0xED): %u", tRepI, tSP);
            AddLog(u8" → 만약 수치들이 다른 장수와 비슷하다면 장수 간격 오프셋(0x3D0)이 맞습니다.");
          }
          __except (EXCEPTION_EXECUTE_HANDLER) {
            AddLog(u8"[Debug] Error: p1+0x3D0 (0x%llX) 메모리 읽기 실패 (크래시 방지됨)", target);
          }
        } else {
          AddLog(u8"[Debug] Error: 올바르지 않은 주인공 주소입니다.");
        }
      }
#endif
      showLoveLogs();
    }

  #endif

  }


} // namespace DX11Base