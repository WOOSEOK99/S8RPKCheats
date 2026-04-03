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
#include <cstdio>


namespace DX11Base {

  bool bShowDebug = false;
  static bool bShowOffset = false;

  // 메모리 에디터 상태 변수
  bool bShowMemoryEditor = false;
  static uintptr_t hexEditorAddr = 0;
  static int hexEditorRows = 16;

  // C2712 컴파일 오류 방지를 위한 안전한 메모리 쓰기 도우미 함수
  static bool SafeWriteMemory(uintptr_t address, unsigned char val) {
    bool success = false;
    __try {
      *(unsigned char*)address = val;
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }
    return success;
  }

  static bool SafeReadMemory(uintptr_t address, unsigned char* out_val) {
    bool success = false;
    __try {
      *out_val = *(unsigned char*)address;
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }
    return success;
  }

  void renderMemoryEditorWindow(uintptr_t gameBase, uintptr_t p1) {
    if (!bShowMemoryEditor) return;

    ImGui::SetNextWindowSize(ImVec2(750, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"메모리 에디터", &bShowMemoryEditor)) {
      
      // 주소 입력 및 설정 섹션
      ImGui::Text(u8"주소:"); ImGui::SameLine();
      ImGui::SetNextItemWidth(140);
      if (ImGui::InputScalar("##Addr", ImGuiDataType_U64, &hexEditorAddr, NULL, NULL, "%016llX", ImGuiInputTextFlags_CharsHexadecimal)) {
        // 주소 변경 시 로직이 필요하다면 여기에 추가
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"붙여넣기")) {
        const char* clip = ImGui::GetClipboardText();
        if (clip) {
          unsigned long long val = 0;
          if (sscanf_s(clip, "%llx", &val) == 1) {
            hexEditorAddr = (uintptr_t)val;
          }
        }
      }

      ImGui::SameLine();
      if (ImGui::Button(u8"GameBase")) hexEditorAddr = gameBase;
      ImGui::SameLine();
      if (ImGui::Button(u8"p1 (Player)")) hexEditorAddr = p1;

      ImGui::SameLine();
      ImGui::SetNextItemWidth(80);
      ImGui::InputInt(u8"행 수", &hexEditorRows);
      if (hexEditorRows < 1) hexEditorRows = 1;
      if (hexEditorRows > 256) hexEditorRows = 256;

      ImGui::SameLine();
      if (ImGui::Button(u8"클립보드 복사")) {
        std::string clipboard;
        for (int row = 0; row < hexEditorRows; row++) {
          uintptr_t rowAddr = hexEditorAddr + (row * 16);
          char line[256];
          sprintf_s(line, sizeof(line), "%016llX: ", rowAddr);
          clipboard += line;

          char asciiStr[17];
          asciiStr[16] = '\0';

          for (int col = 0; col < 16; col++) {
            uintptr_t cellAddr = rowAddr + col;
            unsigned char val = 0;
            bool readSuccess = SafeReadMemory(cellAddr, &val);

            if (readSuccess) {
              sprintf_s(line, sizeof(line), "%02X ", val);
              asciiStr[col] = (val >= 32 && val <= 126) ? (char)val : '.';
            } else {
              sprintf_s(line, sizeof(line), "?? ");
              asciiStr[col] = '?';
            }
            clipboard += line;
          }
          clipboard += "| ";
          clipboard += asciiStr;
          clipboard += "\n";
        }
        ImGui::SetClipboardText(clipboard.c_str());
      }
      ImGui::SameLine();
      ImGui::Checkbox(u8"클릭 차단", &bBlockClickInMemoryEditor);

      // 변화 감지 기능 추가
      static unsigned char s_refBuffer[4096];
      static uintptr_t s_refAddr = 0;
      static int s_refRows = 0;
      static bool s_isRefCaptured = false;

      ImGui::SameLine();
      if (ImGui::Button(u8"현재 상태 기준 저장")) {
          s_refAddr = hexEditorAddr;
          s_refRows = hexEditorRows;
          for (int i = 0; i < s_refRows * 16; i++) {
              unsigned char v = 0;
              if (SafeReadMemory(s_refAddr + i, &v)) s_refBuffer[i] = v;
              else s_refBuffer[i] = 0;
          }
          s_isRefCaptured = true;
          AddLog(u8"[메모리 에디터] 현재 상태를 변화 감지 기준으로 저장했습니다.");
      }
      if (s_isRefCaptured) {
          ImGui::SameLine();
          if (ImGui::Button(u8"기준 초기화")) s_isRefCaptured = false;
      }

      ImGui::Separator();

      // 메모리 그리드
      static const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
      if (ImGui::BeginTable("##MemoryGrid", 18, flags, ImVec2(0, ImGui::GetContentRegionAvail().y - 40))) {
        ImGui::TableSetupColumn("Offset(h)", ImGuiTableColumnFlags_WidthFixed, 130);
        for (int i = 0; i < 16; i++) {
          char buf[4]; sprintf_s(buf, sizeof(buf), "%02X", i);
          ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed, 25);
        }
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int row = 0; row < hexEditorRows; row++) {
          uintptr_t rowAddr = hexEditorAddr + (row * 16);
          ImGui::TableNextRow();
          
          // Offset Column
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%016llX", rowAddr);

          // Hex Columns
          char ascii[17];
          ascii[16] = '\0';

          for (int col = 0; col < 16; col++) {
            ImGui::TableSetColumnIndex(col + 1);
            uintptr_t cellAddr = rowAddr + col;
            unsigned char val = 0;
            bool readSuccess = SafeReadMemory(cellAddr, &val);

            if (readSuccess) {
              ImGui::PushID(row * 16 + col);
              ImGui::SetNextItemWidth(25);
              ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
              ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
              
              // 변화 감지 시 빨간색 강조
              bool isChanged = false;
              if (s_isRefCaptured && s_refAddr == hexEditorAddr && (row * 16 + col) < (s_refRows * 16)) {
                  if (val != s_refBuffer[row * 16 + col]) {
                      isChanged = true;
                      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f)); // 빨간색
                  }
              }

              if (ImGui::InputScalar("##v", ImGuiDataType_U8, &val, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal)) {
                  SafeWriteMemory(cellAddr, val);
                  if (s_isRefCaptured && s_refAddr == hexEditorAddr) s_refBuffer[row * 16 + col] = val; // 수동 수정 시 기준값도 업데이트
              }
              
              if (isChanged) ImGui::PopStyleColor();

              ImGui::PopStyleColor();
              ImGui::PopStyleVar();
              ImGui::PopID();
              
              ascii[col] = (val >= 32 && val <= 126) ? (char)val : '.';
            } else {
              ImGui::TextDisabled("??");
              ascii[col] = '?';
            }
          }

          // ASCII Column
          ImGui::TableSetColumnIndex(17);
          ImGui::TextUnformatted(ascii);
        }
        ImGui::EndTable();
      }

      ImGui::Separator();
      ImGui::End();
    }
  }

  void debuging(uintptr_t gameBase, uintptr_t p1) {
#ifdef ENABLE_DEBUG_LOG
    ImGuiIO &io = ImGui::GetIO();
    float scale = io.FontGlobalScale;

    ImGui::Checkbox(u8"디버그 정보 보기", &bShowDebug);
    ImGui::SameLine();
    if (ImGui::Button(u8"메모리 에디터 열기")) {
      bShowMemoryEditor = true;
    }

    renderMemoryEditorWindow(gameBase, p1);

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