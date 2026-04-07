#include "SelectOfficercapture.h"
#include "Cheats.h"
#include "CityData.h"
#include "InstantLoveCave.h"
#include "MemoryUtils.h"
#include "MenuState.h"
#include "OfficerData.h"
#include "OfficerDetail.h"
#include "RoninMonitor.h" // 알림 동기화용 추가
#include "pch.h"
#include "showcal.h"
#include "showlog.h"
#include "../debug.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <psapi.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

namespace DX11Base {
  extern HMODULE g_hModule;

  void AddLog(const char *fmt, ...);
  extern uintptr_t g_HeroAddr;
  extern bool bForceCenterSelectedOfficer;
  extern bool bForceCenterOfficerList;
  extern bool bShowSelectedOfficerWin;
  extern bool bShowOfficerListWin;

  // --- [선택 무장용 상태 변수] ---
  static int v_OfficerID = 0, v_CG = 0, v_ModelNo = 0, v_ModelColor = 0, v_Birth = 0, v_Appear = 0, v_Death = 0,
             v_Loyalty = 0, v_StrPoint = 0, v_ForceID = 0, v_ForceColor = 0, v_ActionPoints = 0, v_VTableByte = 0,
             v_City = 0, v_AffCity = 0, v_StatusFlags = 0, v_DeathFlag = 0, v_Location = 0;

  bool g_officerCaptureRunning = false;
  static uintptr_t g_officerHookAddr = 0;
  static uint8_t g_officerOriginal[8] = {};
  static uintptr_t g_officerCaveAddr = 0;
  static bool g_officerApplied = false;

  // --- [목록 필터 및 전역 상태 공유용] ---
  static uintptr_t s_stableArrayBase = 0;
  static uintptr_t s_lastCapturedByUI = 0;
  static int s_currentFilter = -1; // -1: 전부, 0x18: 일반, 0x28: 태수, 0x58: 재야, 0x68: 미발견, 0x88: 사망, 0x98: NCP
  static bool s_triggerReselection = false;            // [UX] 무장 상태 변경 시 자동으로 다음 무장 선택 여부
  static std::unordered_set<int> s_selectedOfficerIDs; // 다중 선택용 보관함
  static std::vector<int> s_filteredIndices;           // 현재 필터링된 무장 인덱스들

  // --- [팝업 및 덤프용 상태] ---
  static bool g_showDumpPopup = false;
  static std::string g_dumpText = "";

  // ───────────────────────────────────────────────
  //  선택 무장 베이스 주소 캡처
  //  원본: movzx r9d, byte ptr [r15+0xAB]  (8바이트)
  //  r15 = 선택한 무장 구조체 베이스
  // ───────────────────────────────────────────────

  static bool InstallOfficerCave() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t hookAddr = 0;
    uint8_t targetOffset = 0;
    uint8_t opType = 0;

    struct HookCandidate {
      const char *pattern;
      uint8_t offset;
      uint8_t type;
    } candidates[] = {
        {"45 0F B6 8F A5 00 00 00", 0xA5, 1},
        {"45 0F B6 8F AB 00 00 00", 0xAB, 1},
        {"45 0F B6 8F AE 00 00 00", 0xAE, 1},
    };

    for (const auto &c : candidates) {
      uintptr_t addr = DX11Base::FindPattern(exeBase, exeBase + 0x3000000, c.pattern);
      if (addr) {
        // 이미 후킹되어 있는지 체크 (E9 = JMP)
        if (*(unsigned char *)addr == 0xE9) {
          AddLog(u8"[CONFLICT] 지점 0x%X는 이미 타 프로그램이 사용 중입니다. 다음 후보 탐색...", c.offset);
          continue;
        }
        hookAddr = addr;
        targetOffset = c.offset;
        opType = c.type;
        break;
      }
    }

    if (!hookAddr)
      return false;

    g_officerHookAddr = hookAddr;
    memcpy(g_officerOriginal, (void *)hookAddr, 8);

    g_officerCaveAddr = AllocNear(hookAddr, 1024);
    if (!g_officerCaveAddr)
      return false;

    // 절대 주소를 사용하는 안전한 쉘코드 + 자동 해제 플래그 기입
    unsigned char shellcode[] = {
        0x50,                                                       // push rax
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, &g_capturedOfficerBase (Offset 3)
        0x4C, 0x89, 0x38,                                           // mov [rax], r15
        0x58,                                                       // pop rax
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 가로챈 명령어 복원 (Offset 15)
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,                         // jmp [rip+0] (Offset 23)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00              // Target Addr (Offset 29)
    };

    // 1. 저장할 변수의 절대 주소 기입
    *(uintptr_t *)(shellcode + 3) = (uintptr_t)&g_capturedOfficerBase;

    // 2. 가로챈 명령어를 쉘코드에 복원
    if (opType == 1) {
      // movzx r9d, byte ptr [r15 + offset]
      unsigned char instr[] = {0x45, 0x0F, 0xB6, 0x8F, 0x00, 0x00, 0x00, 0x00};
      *(uint32_t *)(instr + 4) = (uint32_t)targetOffset;
      memcpy(shellcode + 15, instr, 8);
    } else if (opType == 2) {
      // movzx r9d, word ptr [r15 + 0x2E]
      unsigned char instr[] = {0x45, 0x0F, 0xB7, 0x8F, 0x2E, 0x00, 0x00, 0x00};
      memcpy(shellcode + 15, instr, 8);
    }

    // 3. 복귀할 주소 기입
    uintptr_t jumpBackAddr = hookAddr + 8;
    *(uintptr_t *)(shellcode + 29) = jumpBackAddr;

    memcpy((void *)g_officerCaveAddr, shellcode, sizeof(shellcode));

    // 3. 훅 설치 (E9 점프)
    return ApplyJmp(hookAddr, g_officerCaveAddr, 8);
  }

  void SetOfficerCapture(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (enable) {
      if (g_officerApplied)
        return;
      if (g_officerCaptureRunning)
        return;

      g_officerCaptureRunning = true;

      HANDLE hThread = CreateThread(
          nullptr, 0,
          [](LPVOID) -> DWORD {
            if (!g_officerApplied) {
              if (InstallOfficerCave())
                g_officerApplied = true;
            }

            AddLog("[DEBUG] officerCave applied: %d", g_officerApplied);
            g_officerCaptureRunning = false;
            return 0;
          },
          nullptr, 0, nullptr);

      if (hThread)
        CloseHandle(hThread);

    } else {
      g_capturedOfficerBase = 0;

      if (g_officerApplied) {
        RestoreBytes(g_officerHookAddr, g_officerOriginal, 8);
        VirtualFree((LPVOID)g_officerCaveAddr, 0, MEM_RELEASE);
        g_officerCaveAddr = 0;
        g_officerApplied = false;
        g_officerHookAddr = 0;
      }
    }
  }

  // --- [ UI Helper Functions ] ---

  static void PatchMasterData(unsigned short targetID, std::function<void(uintptr_t)> patchFunc) {
    if (s_stableArrayBase < 0x10000)
      return;
    for (int i = 0; i < 5102; i++) {
      uintptr_t targetBase = s_stableArrayBase + (i * 0x3D0);
      RosterStats s = SafeReadRosterStats(targetBase);
      if (s.valid && s.id_08 == targetID) {
        patchFunc(targetBase);
        break; // [최적화] 대상을 찾았으면 남은 5000여 번의 루프를 종료합니다.
      }
    }
  }

  void DrawOfficerHeader(uintptr_t pBase, float scale) {
    uintptr_t forceAddr = *(uintptr_t *)(pBase + 0x18);
    unsigned char vtableByte = *(unsigned char *)(pBase + 0x10);

    if (ImGui::BeginTable("DetailInfoTable", 2, ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 130.0f * scale);
      ImGui::TableSetupColumn(u8"내용", ImGuiTableColumnFlags_WidthFixed, 350.0f * scale);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::Selectable(u8" [ 무장 정보 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);

      if (bShowDebug) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(u8"소속 세력 주소");
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%p", (void *)forceAddr);
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"복사##ForceCopy")) {
          char buf[32];
          sprintf_s(buf, sizeof(buf), "%016llX", (unsigned long long)forceAddr);
          ImGui::SetClipboardText(buf);
        }
      }

      if (forceAddr > 0x10000) {
        RenderStatRow(forceAddr, u8"세력 색상", 0x09, 1, &v_ForceColor, scale);
      }

      if (bShowDebug) {
        uintptr_t corpsAddr = *(uintptr_t *)(pBase + 0x20);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(u8"소속 군단 주소");
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%p", (void *)corpsAddr);
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"복사##CorpsCopy")) {
          char buf[32];
          sprintf_s(buf, sizeof(buf), "%016llX", (unsigned long long)corpsAddr);
          ImGui::SetClipboardText(buf);
        }
      }
      unsigned short currentID = *(unsigned short *)(pBase + 0x08);
      std::string nameValue = u8"???";
      if (g_officerNames.count(currentID)) {
        nameValue = g_officerNames[currentID];
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(u8"이름");
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", nameValue.c_str());

      // [신규] 거주 도시 표시
      {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t cityArrayBase = 0;
        if (exeBase) {
          uintptr_t p1 = *(uintptr_t *)(exeBase + 0x34C8630);
          if (p1 && IsValidPtr(p1, 8)) {
            uintptr_t p2 = *(uintptr_t *)(p1);
            if (p2 && IsValidPtr(p2, 8)) cityArrayBase = *(uintptr_t *)(p2);
          }
        }
        std::string cityName = u8"정보 없음";
        if (cityArrayBase > 0x10000) {
          uintptr_t cityPtr = *(uintptr_t *)(pBase + 0x20);
          if (cityPtr >= cityArrayBase) {
            int idx = (int)((cityPtr - cityArrayBase) / 0x2A0);
            if (idx >= 0 && idx < g_CityCount) {
              cityName = g_CityList[idx].cityname;
            }
          }
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(u8"거주 도시");
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%s", cityName.c_str()); // 노란색 계열
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(u8"무장 ID");
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(1, 1, 0, 1), "%d", (int)currentID);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(u8"무장 상태");
      ImGui::TableSetColumnIndex(1);

      const char *stateStr = u8"기타";
      ImVec4 stateColor = ImVec4(1, 1, 1, 1);

      switch (vtableByte) {
      case 0x18:
        stateStr = u8"군사";
        stateColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
        break;
      case 0x28:
        stateStr = u8"일반";
        stateColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        break;
      case 0x58:
        stateStr = u8"재야";
        stateColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        break;
      case 0x68:
      case 0x78:
        stateStr = u8"미발견";
        stateColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      case 0x88:
        stateStr = u8"사망";
        stateColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        break;
      case 0xD8:
        stateStr = u8"도독";
        stateColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
        break;
      case 0xE8:
        stateStr = u8"태수";
        stateColor = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        break;
      case 0xC8:
        stateStr = u8"군주";
        stateColor = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
        break;
      case 0x98:
        stateStr = u8"NPC";
        stateColor = ImVec4(0.7f, 0.7f, 1.0f, 1.0f); // 연보라색 계열
        break;
      default:
        static char fallbackStr[32];
        snprintf(fallbackStr, sizeof(fallbackStr), u8"기타 (0x%02X)", vtableByte);
        stateStr = fallbackStr;
        stateColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      }

      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(stateColor, "%s", stateStr);

      if (vtableByte == 0x88) { // 사망
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(u8"부활", ImVec2(0, 24 * scale))) {
          uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
          uintptr_t cityArrayBase = 0;
          if (exeBase) {
            uintptr_t p1 = *(uintptr_t *)(exeBase + 0x34C8630);
            if (p1 && IsValidPtr(p1, 8)) {
              uintptr_t p2 = *(uintptr_t *)(p1);
              if (p2 && IsValidPtr(p2, 8)) cityArrayBase = *(uintptr_t *)(p2);
            }
          }

          uint8_t heroCityIdx = 0;
          uintptr_t targetCityPtr = 0;
          uintptr_t tempHeroBase = 0;
          uintptr_t tempGameBase = DX11Base::GetGameBase();
          if (tempGameBase) {
            tempHeroBase = *(uintptr_t *)(tempGameBase + 0xE0);
            if (tempHeroBase && IsValidPtr(tempHeroBase, 0x100)) {
              uintptr_t heroCityPtr = *(uintptr_t *)(tempHeroBase + 0x20);
              if (cityArrayBase > 0x10000 && heroCityPtr >= cityArrayBase) {
                heroCityIdx = (uint8_t)((heroCityPtr - cityArrayBase) / 0x2A0);
                targetCityPtr = heroCityPtr;
              }
            }
          }

          // [진단 완료] cityArrayBase와 0x2A0 크기가 정확함을 확인했습니다. (49번=운남)


        auto PatchStatus = [&](uintptr_t base) {
            ModifyStat(base, 0x10, 0x58, 1);    // 상태: 재야(0x58)
            ModifyStat(base, 0x36, 255, 2);     // 몰년 연장
            ModifyStat(base, 0xEE, 200, 1);     // 행동력
            ModifyStat(base, 0x374, 0, 4);      // 사망 플래그 제거


            if (targetCityPtr) {
              DWORD oldP;
              if (VirtualProtect((LPVOID)(base + 0x20), 8, PAGE_READWRITE, &oldP)) {
                *(uintptr_t *)(base + 0x20) = targetCityPtr;
                VirtualProtect((LPVOID)(base + 0x20), 8, oldP, &oldP);
              }
            }
          };

          unsigned short officerID = *(unsigned short *)(pBase + 0x08);
          PatchStatus(pBase);
          PatchMasterData(officerID, PatchStatus);
          s_triggerReselection = true;
          AddLog(u8"[LIFE] %s 무장을 주인공 도시(Index:%d)로 부활시켰습니다.", g_officerNames[officerID].c_str(), heroCityIdx);
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"주인공과 같은도시로 사망한 무장을 부활시킵니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 저장 후 불러오기를 해야 게임에 반영됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f * scale);
        const char *current_city_name = g_CityList[s_selectedCityIdx].cityname;
        if (ImGui::BeginCombo(u8"##CitySelector", current_city_name)) {
          for (int n = 0; n < g_CityCount; n++) {
            bool is_selected = (s_selectedCityIdx == n);
            if (ImGui::Selectable(g_CityList[n].cityname, is_selected))
              s_selectedCityIdx = n;
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button(u8"선택도시로 부활", ImVec2(0, 24 * scale))) {
          uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
          uintptr_t p1 = 0, p2 = 0, cityArrayBase = 0;
          unsigned short pBaseID = *(unsigned short *)(pBase + 0x08);
          if (exeBase) {
            p1 = *(uintptr_t *)(exeBase + 0x34C8630);
            if (p1 && p1 > 0x10000) {
              p2 = *(uintptr_t *)(p1 + 0x0);
              if (p2 && p2 > 0x10000) {
                cityArrayBase = *(uintptr_t *)(p2 + 0x0);
              }
            }
          }
          if (cityArrayBase && cityArrayBase > 0x10000) {
            uintptr_t targetAddr = cityArrayBase + (s_selectedCityIdx * 0x2A0);
            DWORD oldP;
            if (VirtualProtect((LPVOID)(pBase + 0x20), 8, PAGE_READWRITE, &oldP)) {
              *(uintptr_t *)(pBase + 0x20) = targetAddr;
              VirtualProtect((LPVOID)(pBase + 0x20), 8, oldP, &oldP);
            }

            // [패치] 현재 가로챈 객체(UI용)와 마스터 배열 내의 원본을 동시에 수정
            auto PatchStatus = [&](uintptr_t base) {
              ModifyStat(base, 0x10, 0x58, 1); // 상태: 재야(0x58)
              ModifyStat(base, 0x36, 255, 2);  // 몰년: 수명 연장
              ModifyStat(base, 0xEE, 200, 1);  // 행동력
              ModifyStat(base, 0x374, 0, 4);   // 사망 플래그 제거
              DWORD oldP2;
              if (VirtualProtect((LPVOID)(base + 0x20), 8, PAGE_READWRITE, &oldP2)) {
                *(uintptr_t *)(base + 0x20) = targetAddr;
                VirtualProtect((LPVOID)(base + 0x20), 8, oldP2, &oldP2);
              }
            };

            PatchStatus(pBase);
            PatchMasterData(pBaseID, PatchStatus);
            s_triggerReselection = true;

            AddLog(u8"[부활] %s 무장을 [%s] 도시로 부활시켰습니다!", DX11Base::g_officerNames[pBaseID].c_str(),
                   g_CityList[s_selectedCityIdx].cityname);
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"선택한 도시로 사망한 무장을 부활시킵니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 저장 후 불러오기를 해야 게임에 반영됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::PopStyleColor(3);       // Pop 2nd button style (3 colors)
        ImGui::PopStyleColor(1);       // Pop 1st button style (1 color)
      } else if (vtableByte == 0x58) { // 재야 무장: 도시 이동 기능만 제공
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f * scale);
        const char *current_city_name = g_CityList[s_selectedCityIdx].cityname;
        if (ImGui::BeginCombo(u8"##CitySelectorRonin", current_city_name)) {
          for (int n = 0; n < g_CityCount; n++) {
            bool is_selected = (s_selectedCityIdx == n);
            if (ImGui::Selectable(g_CityList[n].cityname, is_selected))
              s_selectedCityIdx = n;
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button(u8"선택도시로 이동", ImVec2(0, 24 * scale))) {
          uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
          uintptr_t p1 = 0, p2 = 0, cityArrayBase = 0;
          if (exeBase) {
            p1 = *(uintptr_t *)(exeBase + 0x34C8630);
            if (p1 && p1 > 0x10000) {
              p2 = *(uintptr_t *)(p1 + 0x0);
              if (p2 && p2 > 0x10000) {
                cityArrayBase = *(uintptr_t *)(p2 + 0x0);
              }
            }
          }
          if (cityArrayBase && cityArrayBase > 0x10000) {
            uintptr_t targetAddr = cityArrayBase + (s_selectedCityIdx * 0x2A0);
            *(uintptr_t *)(pBase + 0x20) = targetAddr;
            AddLog(u8"[이동] %s 무장을 [%s] 도시로 이동시켰습니다!",
                   DX11Base::g_officerNames[*(unsigned short *)(pBase + 0x08)].c_str(),
                   g_CityList[s_selectedCityIdx].cityname);
          }
        }
        ImGui::PopStyleColor(3);
      } else if (vtableByte == 0x68 || vtableByte == 0x78) { // 미발견 무장: 재야로 변경 버튼 제공
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(u8"재야", ImVec2(0, 24 * scale))) {
          unsigned short pBaseID = *(unsigned short *)(pBase + 0x08);
          auto patchRonin = [&](uintptr_t base) { ModifyStat(base, 0x10, 0x58, 1); };
          patchRonin(pBase);
          PatchMasterData(pBaseID, patchRonin);
          s_triggerReselection = true;

          AddLog(u8"[LIFE] %s 미발견 무장을 재야(0x58) 상태로 변경했습니다.", g_officerNames[pBaseID].c_str());
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"미발견 무장을 즉시 재야 상태로 변경합니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 저장 후 불러오기를 해야 게임에 반영됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::PopStyleColor(1);
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.6f, 0.1f, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
      ImGui::Selectable(u8" [ 외형 & 특징 ]", true,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);

      RenderStatRow(pBase, u8"얼굴 번호", 0x2E, 2, &v_OfficerID, scale);
      RenderStatRow(pBase, u8"모델 번호", 0xA5, 1, &v_ModelNo, scale);
      RenderStatRow(pBase, u8"모델 색상", 0xA6, 1, &v_ModelColor, scale);
      RenderStatRow(pBase, u8"등장년도", 0x32, 2, &v_Appear, scale);
      RenderStatRow(pBase, u8"생년", 0x34, 2, &v_Birth, scale);
      RenderStatRow(pBase, u8"몰년(수명)", 0x36, 2, &v_Death, scale);

      ImGui::EndTable();
    }
  }

  void DrawOfficerTalents(uintptr_t pBase, float scale) {
    if (ImGui::BeginTable("TraitHeaderTable", 1)) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
      ImGui::Selectable(u8" [ 기재 정보 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);
      ImGui::EndTable();
    }

    uintptr_t officerRealBase = DX11Base::GetSelectedOfficerBase();
    static int editId[3] = {0, 0, 0};

    for (int i = 0; i < 3; i++) {
      TalentInfo info;
      ImGui::PushID(i);
      ImGui::BeginGroup();
      if (GetOfficerTalentDetailed(pBase, i, info)) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), u8"[*] 기재 %d : %s (ID %d)", i + 1, GetTalentName(info.id),
                           info.id);
        ImGui::Indent(15.0f * scale);
        for (int j = 0; j < 6; j++) {
          if (info.effects[j].effectId == 0)
            break;
          std::string desc = GetFormattedEffectDescription(info.effects[j]);
          if (!desc.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), u8"  - %s (ID:%d)", desc.c_str(),
                               info.effects[j].effectId);
          } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), u8"  - 효과 %d: %d (값1: %d, 값2: %d)", j + 1,
                               info.effects[j].effectId, info.effects[j].val1, info.effects[j].val2);
          }
        }
        ImGui::Unindent(15.0f * scale);
      } else {
        ImGui::TextDisabled(u8"[ ] 기재 %d : 비어있음", i + 1);
      }

      if (officerRealBase) {
        ImGui::SetNextItemWidth(100 * scale);
        ImGui::InputInt(u8"##id", &editId[i]);
        ImGui::SameLine();
        if (ImGui::Button(u8"기속 적용", ImVec2(80 * scale, 0))) {
          SetTraitID(officerRealBase, i, (uint16_t)editId[i]);
        }
      }
      ImGui::EndGroup();
      if (i < 2)
        ImGui::Separator();
      ImGui::PopID();
    }
  }

  void DrawSelectedOfficerWindow(ImVec2 mPos, ImVec2 mSize, float scale, bool asChild) {
    LoadOfficerNames();
    LoadEffectDefinitions();
    static bool s_wasShowWin = false;
    if (!asChild) {
      if (!bShowSelectedOfficerWin) {
        if (s_wasShowWin) {
          if (g_officerApplied) {
            RestoreBytes(g_officerHookAddr, g_officerOriginal, 8);
            g_officerApplied = false;
          }
          bAllowGameClick = false;
          AddLog(u8"[LIVE] 실시간 추적을 종료하고 훅을 해제했습니다.");
          s_wasShowWin = false;
        }
        return;
      }
      s_wasShowWin = true;
    }

    if (!asChild && bShowSelectedOfficerWin && !g_officerApplied && !g_officerCaptureRunning) {
      SetOfficerCapture(true);
    }

    if (asChild) {
      ImGui::BeginChild("SelectedOfficerChild", ImVec2(0, 0), true);
    } else {
      if (bForceCenterSelectedOfficer) {
        ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        bForceCenterSelectedOfficer = false;
      } else {
        ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      }
      if (!ImGui::Begin(u8"선택 무장 상세 편집###SelectedOfficerWin", &bShowSelectedOfficerWin)) {
        ImGui::End();
        return;
      }
    }

    if (g_capturedOfficerBase == 0) {
      ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), u8"캡처된 데이터가 없습니다.");
      ImGui::BulletText(u8"게임에서 상세 정보를 열거나, 리스트에서 선택하세요.");
      if (asChild)
        ImGui::EndChild();
      else
        ImGui::End();
      return;
    }

    uintptr_t pBase = g_capturedOfficerBase;
    if (!asChild) {
      if (!g_officerApplied) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), u8"실시간 추적 준비 중...");
      } else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"● 실시간 추적 활성 상태 (정보 -> 무장 -> 클릭)");
      }
      ImGui::Spacing();
      ImGui::Checkbox(u8"게임 화면 클릭 허용 (무장 선택 시 필요)", &bAllowGameClick);
      ImGui::Separator();
    }

    if (bShowDebug)
{
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1), u8"[ 선택 무장 실시간 정보 ]");
    ImGui::Text(u8"연결된 주소: %p", (void *)pBase);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"복사##AddrCopy")) {
      char buf[32];
      sprintf_s(buf, sizeof(buf), "%016llX", (unsigned long long)pBase);
      ImGui::SetClipboardText(buf);
    }
    ImGui::Separator();
  }

    static int currentTabIdx = 0;
    static uintptr_t lastCapturedBase = 0;
    if (pBase != lastCapturedBase) {
      ImGui::SetWindowSize(ImVec2(0, 0));
      lastCapturedBase = pBase;
    }

    // [이동] 미발견 목록 보기일 때만 '전부 재야' 및 '선택 재야' 버튼 표시
    if (asChild && s_currentFilter == 0x68) {
      float totalWidth = ImGui::GetContentRegionAvail().x;
      float buttonWidth = (totalWidth - 8.0f * scale) / 2.0f;
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));

      // [최적화] 미발견 전부 재야: 일괄 메모리 보호 + 단일 루프 처리
      if (ImGui::Button(u8"미발견 전부 재야", ImVec2(buttonWidth, 30.0f * scale))) {
        int count = 0;
        if (s_stableArrayBase > 0x10000) {
          DWORD old;
          // 5102명 전체 영역(약 2MB)을 한 번만 권한 변경하여 수천 번의 시스템 호출 방지
          if (VirtualProtect((LPVOID)s_stableArrayBase, 5102 * 0x3D0, PAGE_READWRITE, &old)) {
            for (int idx : s_filteredIndices) {
              uintptr_t targetBase = s_stableArrayBase + (idx * 0x3D0);
              unsigned short currentID = *(unsigned short *)(targetBase + 0x08);
              unsigned char vByte = *(unsigned char *)(targetBase + 0x10);
              if (vByte == 0x68 || vByte == 0x78) {
                ModifyStatFast(targetBase + 0x10, 0x58, 1);
                count++;
              }
            }
            VirtualProtect((LPVOID)s_stableArrayBase, 5102 * 0x3D0, old, &old);
          }
        }
        if (count > 0)
          AddLog(u8"[LIFE] 현재 필터링된 %d명의 미발견 무장을 재야(0x58) 상태로 변경했습니다.", count);
      }
      ImGui::SameLine(0, 8.0f * scale);

      // [최적화] 선택 무장 재야: O(N*M) -> O(N) 최적화 및 일괄 메모리 보호
      if (ImGui::Button(u8"선택 무장 재야", ImVec2(buttonWidth, 30.0f * scale))) {
        if (s_selectedOfficerIDs.empty())
          AddLog(u8"[WARN] 선택된 무장이 없습니다.");
        else {
          int count = 0;
          if (s_stableArrayBase > 0x10000) {
            DWORD old;
            if (VirtualProtect((LPVOID)s_stableArrayBase, 5102 * 0x3D0, PAGE_READWRITE, &old)) {
              // 5102명 전체 리스트를 딱 한 번만 뒤지면서 선택된 ID인지 체크
              for (int i = 0; i < 5102; i++) {
                uintptr_t targetBase = s_stableArrayBase + (i * 0x3D0);
                unsigned short currentID = *(unsigned short *)(targetBase + 0x08);
                if (s_selectedOfficerIDs.count(currentID)) {
                  ModifyStatFast(targetBase + 0x10, 0x58, 1);
                  count++;
                }
              }
              VirtualProtect((LPVOID)s_stableArrayBase, 5102 * 0x3D0, old, &old);
            }
          }
          s_triggerReselection = true;
          AddLog(u8"[LIFE] 선택한 %d명의 미발견 무장을 재야(0x58) 상태로 변경했습니다.", count);
          s_selectedOfficerIDs.clear();
        }
      }
      ImGui::PopStyleColor();
      ImGui::Spacing();
    }

    if (ImGui::BeginTabBar("SelectedOfficerTabs")) {
      if (ImGui::BeginTabItem(u8"상세 정보")) {
        if (currentTabIdx != 0) {
          ImGui::SetWindowSize(ImVec2(0, 0));
          currentTabIdx = 0;
        }
        DrawOfficerHeader(pBase, scale);
        // RenderBasicTab(pBase, scale, true);
        DrawOfficerTalents(pBase, scale);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(u8"능력/상태")) {
        RenderBasicTab(pBase, scale, true);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(u8"기능")) {
        if (currentTabIdx != 1) {
          ImGui::SetWindowSize(ImVec2(0, 0));
          currentTabIdx = 1;
        }
        RenderResearchTab(pBase, scale);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(u8"소양(EXP)")) {
        if (currentTabIdx != 2) {
          ImGui::SetWindowSize(ImVec2(0, 0));
          currentTabIdx = 2;
        }
        RenderExpTab(pBase, scale);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (asChild)
      ImGui::EndChild();
    else
      ImGui::End();
  }

  static void DrawOfficerDumpPopup(float scale) {
    if (g_showDumpPopup) {
      ImGui::OpenPopup(u8"무장 데이터 덤프");
      g_showDumpPopup = false;
    }
    if (ImGui::BeginPopupModal(u8"무장 데이터 덤프", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(u8"아래 내용을 복사하여 분석에 사용하세요.");
      ImGui::Separator();
      static std::vector<char> dumpBuf;
      dumpBuf.assign(g_dumpText.begin(), g_dumpText.end());
      dumpBuf.push_back('\0');
      ImGui::InputTextMultiline("##dumpoutput", dumpBuf.data(), dumpBuf.size(), ImVec2(600 * scale, 400 * scale),
                                ImGuiInputTextFlags_ReadOnly);
      if (ImGui::Button(u8"클립보드 복사"))
        ImGui::SetClipboardText(g_dumpText.c_str());
      ImGui::SameLine();
      if (ImGui::Button(u8"닫기"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  void DumpOfficerData(uintptr_t base) {
    if (base < 0x10000)
      return;
    std::ostringstream oss;
    oss << "--- Officer Data Dump (Base: " << (void *)base << ") ---\n";
    for (int i = 0; i < 0x3D0;) {
      const char *label = GetOffsetLabel(i);
      if (label) {
        oss << "[0x" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << i << "] " << label << ": ";
        if (i == 0x08 || i == 0x2E || i == 0x32 || i == 0x34 || i == 0x36 || i == 0x100 || i == 0x104 || i == 0x106 ||
            i == 0x108) {
          oss << std::dec << *(unsigned short *)(base + i);
          i += 2;
        } else if (i == 0x18 || i == 0x88) {
          oss << std::hex << (void *)(*(uintptr_t *)(base + i));
          i += 8;
        } else if (i == 0xE8) {
          oss << std::dec << *(unsigned int *)(base + i);
          i += 4;
        } else {
          oss << std::dec << (int)*(unsigned char *)(base + i);
          i += 1;
        }
        oss << "\n";
      } else {
        oss << "[0x" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << i
            << "] 분석안됨: " << std::setw(2) << (int)*(unsigned char *)(base + i) << "\n";
        i++;
      }
    }
    g_dumpText = oss.str();
    g_showDumpPopup = true;
  }

  void DrawOfficerListWindow(uintptr_t p1, float scale) {
    if (!bShowOfficerListWin)
      return;

    LoadOfficerNames();
    LoadEffectDefinitions();

    if (bForceCenterOfficerList) {
      ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      bForceCenterOfficerList = false;
    } else {
      ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    // 최소 세로 길이를 700으로 상향하여 상세 정보가 스크롤 없이 시원하게 보이게 합니다.
    ImGui::SetNextWindowSizeConstraints(ImVec2(820 * scale, 700 * scale), ImVec2(1400 * scale, 1000 * scale));

    if (ImGui::Begin(u8"모든 무장 편집 리스트 (5102명)###OfficerListWin", &bShowOfficerListWin,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      if (!p1) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), u8"무장 배열 데이터를 찾을 수 없습니다.");
        ImGui::BulletText(u8"인게임(전략 화면 등)으로 진입해야 활성화됩니다.");
        ImGui::Spacing();
        if (ImGui::Button(u8"닫기")) {
          bShowOfficerListWin = false;
        }
        ImGui::End();
        return;
      }

      static char s_searchBuf[64] = "";
      static int s_scrollToIndex = -1;
      bool doSearch = false;

      // 장수 선택이 바뀌면 창 크기를 내용에 맞게 재조정 (Auto-Resize 유도)
      static uintptr_t lastListBase = 0;
      if (g_capturedOfficerBase != lastListBase) {
        ImGui::SetWindowSize(u8"모든 무장 편집 리스트 (5102명)###OfficerListWin", ImVec2(0, 0));
        lastListBase = g_capturedOfficerBase;
      }

      // UI에서 넘겨준 p1은 배열에 속하지 않은 임시 주소(temp copy)일 수 있습니다. (예: GetGameBase() + 0xE0)
      // 따라서 p1에서 주인공의 ID만 추출하고, 배열의 원본 주소를 찾을 때는 훅(Hook)으로 잡은 g_capturedOfficerBase를
      // 기준점으로 사용합니다.
      unsigned short heroID_real = *(unsigned short *)(p1 + 0x08);

      uintptr_t referenceBase = (g_capturedOfficerBase && g_capturedOfficerBase > 0x10000) ? g_capturedOfficerBase : p1;

      // g_capturedOfficerBase가 외부 훅에 의해 변경되었을 때만 arrayBase를 갱신합니다.
      // (목록 내부 클릭으로 인한 갱신이면 기준점이 뒤틀리는 drift 현상을 방지합니다)
      if (s_stableArrayBase == 0 || (g_capturedOfficerBase != 0 && g_capturedOfficerBase != s_lastCapturedByUI)) {
        unsigned short refID = *(unsigned short *)(referenceBase + 0x08);
        s_stableArrayBase = referenceBase - ((refID - 1) * 0x3D0);
        s_lastCapturedByUI = g_capturedOfficerBase;
      }
      uintptr_t arrayBase = s_stableArrayBase;

      ImGui::SetNextItemWidth(100.0f * scale);
      if (ImGui::InputTextWithHint(u8"##search", u8"이름 or ID", s_searchBuf, sizeof(s_searchBuf),
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
        doSearch = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"찾기")) {
        doSearch = true;
      }

      // [토글] 전체 선택 / 선택 해제 버튼
      ImGui::SameLine();
      bool hasSelections = !s_selectedOfficerIDs.empty();
      if (hasSelections) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button(u8"선택 해제"))
          s_selectedOfficerIDs.clear();
        ImGui::PopStyleColor();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.2f, 1.0f));
        if (ImGui::Button(u8"전체 선택")) {
          for (int idx : s_filteredIndices)
            s_selectedOfficerIDs.insert(idx + 1);
        }
        ImGui::PopStyleColor();
      }
      ImGui::SameLine();
      if (hasSelections)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"(%zu명)", s_selectedOfficerIDs.size());
      else
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"(0명)");

      if (bShowDebug)
{
        ImGui::SameLine();
      ImGui::Checkbox(u8"클릭 차단", &bBlockClickInOfficerList);
      ImGui::SameLine();
      if (ImGui::Button(u8"덤프")) {
        if (g_capturedOfficerBase != 0) {
          DumpOfficerData(g_capturedOfficerBase);
        }
      }
    }

      // 필터 버튼 오른쪽 정렬
      {
        const char *filterLabels[] = {u8"군사", u8"일반",   u8"태수", u8"도독", u8"군주",
                                      u8"재야", u8"미발견", u8"사망", u8"NPC",  u8"전부"};
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float fp = ImGui::GetStyle().FramePadding.x;
        float totalW = 0.0f;
        for (auto *lbl : filterLabels) {
          totalW += ImGui::CalcTextSize(lbl).x + fp * 2.0f;
        }
        totalW += spacing * (IM_ARRAYSIZE(filterLabels) - 1);
        float posX = ImGui::GetContentRegionMax().x - totalW;
        if (posX > ImGui::GetCursorPosX())
          ImGui::SameLine(posX);
        else
          ImGui::SameLine();
      }

      // 필터 버튼들
      auto DrawFilterButton = [scale](const char *label, int filterVal) {
        if (s_currentFilter == filterVal) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
          if (ImGui::Button(label))
            s_currentFilter = filterVal;
          ImGui::PopStyleColor();
        } else {
          if (ImGui::Button(label))
            s_currentFilter = filterVal;
        }
      };

      DrawFilterButton(u8"군사", 0x18);
      ImGui::SameLine();
      DrawFilterButton(u8"일반", 0x28);
      ImGui::SameLine();
      DrawFilterButton(u8"태수", 0xE8);
      ImGui::SameLine();
      DrawFilterButton(u8"도독", 0xD8);
      ImGui::SameLine();
      DrawFilterButton(u8"군주", 0xC8);
      ImGui::SameLine();
      DrawFilterButton(u8"재야", 0x58);
      ImGui::SameLine();
      DrawFilterButton(u8"미발견", 0x68);
      ImGui::SameLine();
      DrawFilterButton(u8"사망", 0x88);
      ImGui::SameLine();
      DrawFilterButton(u8"NPC", 0x98);
      ImGui::SameLine();
      DrawFilterButton(u8"전부", -1);

      // 필터 리스트 갱신 로직
      s_filteredIndices.clear();
      s_filteredIndices.reserve(5102);

      bool seenIDs[65536];
      memset(seenIDs, 0, sizeof(seenIDs));

      for (int i = 0; i < 5102; i++) {
        uintptr_t targetBase = arrayBase + (i * 0x3D0);

        // 메모리 접근 예외를 방지하기 위해 SafeReadRosterStats를 사용합니다.
        RosterStats s = SafeReadRosterStats(targetBase);
        if (!s.valid)
          continue;

        // 중복 데이터 방지 (배열 뒤쪽에 복사되는 임시 클론 찌꺼기를 필터링합니다)
        if (seenIDs[s.id_08])
          continue;

        // 이름이 없는 더미("???") 데이터나 빈 문자열 이름은 렌더링 목록에서 완전히 제외합니다.
        if (g_officerNames.count(s.id_08) == 0)
          continue;
        if (g_officerNames[s.id_08].empty() || g_officerNames[s.id_08] == u8"???")
          continue;

        seenIDs[s.id_08] = true;

        if (s_currentFilter == -1) {
          s_filteredIndices.push_back(i);
        } else {
          uint8_t status = *(uint8_t *)(targetBase + 0x10);
          if (status == s_currentFilter || (s_currentFilter == 0x68 && status == 0x78)) {
            s_filteredIndices.push_back(i);
          }
        }
      }

      // [추가] 필터 변경 시 자동으로 첫 번째 장수 선택 (목록이 비어있으면 해제)
      static int s_lastFilterForSelection = -2;
      if (s_lastFilterForSelection != s_currentFilter || s_triggerReselection) {
        if (!s_filteredIndices.empty()) {
          uintptr_t firstBase = arrayBase + (s_filteredIndices[0] * 0x3D0);
          g_capturedOfficerBase = firstBase;
          s_lastCapturedByUI = firstBase;
        } else if (s_triggerReselection) {
          g_capturedOfficerBase = 0;
        }
        s_lastFilterForSelection = s_currentFilter;
        s_triggerReselection = false;
      }

      if (doSearch && s_searchBuf[0] != '\0') {
        std::string q = s_searchBuf;
        for (size_t idx = 0; idx < s_filteredIndices.size(); idx++) {
          int i = s_filteredIndices[idx];
          int targetId = i + 1;
          if (std::to_string(targetId) == q) {
            s_scrollToIndex = static_cast<int>(idx);
            break;
          }
          if (g_officerNames.count(targetId) && g_officerNames[targetId].find(q) != std::string::npos) {
            s_scrollToIndex = static_cast<int>(idx);
            break;
          }
        }
      }

      ImGui::Separator();

      // Left Split Pane 너비를 380에서 280으로 축소하여 콤팩트하게 만듭니다.
      ImGui::BeginChild("OfficerListPane", ImVec2(280 * scale, 0), true);

      // 행 높이를 별도로 하드코딩하지 않아 클리퍼(Clipper) 스크롤 끝이 잘리지 않게 합니다.
      const float APPROX_HEIGHT = ImGui::GetTextLineHeightWithSpacing() + 3.0f;

      if (ImGui::BeginTable("OfficerListTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
        // 검색 성공 직후: 대략적인 위치로 먼저 이동 (클리퍼가 해당 아이템을 감지할 수 있게 함)
        if (s_scrollToIndex >= 0 && s_scrollToIndex < s_filteredIndices.size()) {
          ImGui::SetScrollY(s_scrollToIndex * APPROX_HEIGHT);
          g_capturedOfficerBase = arrayBase + (s_filteredIndices[s_scrollToIndex] * 0x3D0);
          s_lastCapturedByUI = g_capturedOfficerBase;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(u8"V", ImGuiTableColumnFlags_WidthFixed, 22.0f * scale);
        ImGui::TableSetupColumn(u8"무장", ImGuiTableColumnFlags_WidthFixed, 140.0f * scale);
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)s_filteredIndices.size()); // 첫 행을 기반으로 높이 자동 측정
        while (clipper.Step()) {
          for (int row_idx = clipper.DisplayStart; row_idx < clipper.DisplayEnd; row_idx++) {
            int original_idx = s_filteredIndices[row_idx];
            uintptr_t targetBase = arrayBase + (original_idx * 0x3D0);
            int currentID = original_idx + 1; // 1번 장수부터 시작하므로 index에 +1

            ImGui::TableNextRow();            // 강제 제한 풀기
            ImGui::AlignTextToFramePadding(); // 수직 중앙 정렬

            if (targetBase > 0x10000) {
              RosterStats s = SafeReadRosterStats(targetBase);
              if (s.valid) {
                std::string displayName = u8"???";
                if (g_officerNames.count(s.id_08)) {
                  displayName = g_officerNames[s.id_08];
                }

                // 현재 선택된 무장인지 확인 (하이라이트용)
                bool isSelected = (g_capturedOfficerBase == targetBase);
                ImGui::PushID(original_idx);

                // 첫 번째 열: 체크박스 (다중 선택용)
                ImGui::TableNextColumn();
                bool isMultiSelected = s_selectedOfficerIDs.count(currentID) > 0;
                if (ImGui::Checkbox("##sel", &isMultiSelected)) {
                  if (isMultiSelected)
                    s_selectedOfficerIDs.insert(currentID);
                  else
                    s_selectedOfficerIDs.erase(currentID);
                }

                // 두 번째 열: 이름
                ImGui::TableNextColumn();
                char label[128];
                if (currentID == heroID_real)
                  snprintf(label, sizeof(label), u8"★ %s (주인공)", displayName.c_str());
                else
                  snprintf(label, sizeof(label), "%s", displayName.c_str());

                if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                }

                // [추가] 키보드 방향키 이동 시에도 상세 정보 업데이트
                if (ImGui::IsItemFocused() && g_capturedOfficerBase != targetBase) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                }

                // [수정] 검색 대상 항목인 경우 포커스 및 화면 중앙 정렬
                if (s_scrollToIndex == row_idx) {
                  ImGui::SetKeyboardFocusHere(-1);
                  ImGui::SetScrollHereY(0.5f);
                  s_scrollToIndex = -1;
                }

                ImGui::TableNextColumn();
                // 두 번째 열: ID (여기도 클릭 가능하게 처리)
                if (ImGui::Selectable(std::to_string(s.id_08).c_str(), isSelected, ImGuiSelectableFlags_None)) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                }

                // [추가] 키보드 방향키 이동 시에도 상세 정보 업데이트 및 스크롤 추적
                if (ImGui::IsItemFocused() && g_capturedOfficerBase != targetBase) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                  ImGui::SetScrollHereY(0.5f); // 키보드 네비게이션 시 화면 중앙으로 정렬
                }

                ImGui::PopID();
              } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error");
                ImGui::TableNextColumn();
              }
            } else {
              ImGui::TextColored(ImVec4(1, 0, 0, 1), "Invalid");
              ImGui::TableNextColumn();
            }
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndChild(); // 왼쪽 리스트 패널 종료

      ImGui::SameLine();

      // 오른쪽 상세 편집 패널: 내용이 잘리지 않도록 너비를 520으로 고정합니다.
      ImGui::BeginChild("OfficerDetailPane", ImVec2(520 * scale, 0), true);
      if (g_capturedOfficerBase != 0) {
        DrawSelectedOfficerWindow(ImVec2(0, 0), ImVec2(0, 0), scale, true);
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"왼쪽 리스트에서 상세 편집할 무장을 클릭 하세요.");
      }
      ImGui::EndChild(); // 오른쪽 상세 편집 패널 종료

      DrawOfficerDumpPopup(scale);
    }
    ImGui::End();
  }

} // namespace DX11Base