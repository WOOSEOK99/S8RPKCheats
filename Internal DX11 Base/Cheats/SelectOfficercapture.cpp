#include "SelectOfficercapture.h"
#include "../debug.h"
#include "Cheats.h"
#include "CityData.h"
#include "InstantLoveCave.h"
#include "MemoryUtils.h"
#include "MenuState.h"
#include "OfficerData.h"
#include "OfficerDetail.h"
#include "OfficerRosterResolve.h"
#include "RoninMonitor.h" // 알림 동기화용 추가
#include "pch.h"
#include "showcal.h"
#include "showlog.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <psapi.h>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <windows.h>

namespace DX11Base {
  extern HMODULE g_hModule;

  void AddLog(const char *fmt, ...);
  extern bool bForceCenterSelectedOfficer;
  extern bool bForceCenterOfficerList;
  extern bool bShowSelectedOfficerWin;
  extern bool bShowOfficerListWin;

  // --- [선택 무장용 상태 변수] ---
  static int v_OfficerID = 0, v_CG = 0, v_ModelNo = 0, v_ModelColor = 0, v_Birth = 0, v_Appear = 0, v_Death = 0,
             v_Loyalty = 0, v_StrPoint = 0, v_ForceID = 0, v_ForceColor = 0, v_ActionPoints = 0, v_VTableByte = 0,
             v_City = 0, v_AffCity = 0, v_StatusFlags = 0, v_DeathFlag = 0, v_Location = 0, v_Gender = 0;

  // --- [목록 필터 및 전역 상태 공유용] ---
  // --- [목록 필터 및 전역 상태 공유용] ---
  struct CachedOfficer {
    int originalIndex;       // 마스터 배열에서의 인덱스
    int officerID;           // 무장 고유 ID
    uint8_t statusByte;      // 상태 바이트 (0x10)
    std::string displayName; // 미리 가공된 이름 (NFC/UTF-8 완료)
  };

  static uintptr_t s_stableArrayBase = 0;
  static uintptr_t s_lastCapturedByUI = 0;
  static int s_currentFilter = -1; // -1: 전부, 0x18 군사, 0x28 일반, 0x38 두령, 0x48 동지, 0x58 재야, 0x68 미발견(0x78 동류), 0x88 사망, 0x98 NPC, 0xD8 도독, 0xE8 태수, 0xC8 군주
  static bool s_triggerReselection = false;            // [UX] 무장 상태 변경 시 자동으로 다음 무장 선택 여부
  static bool s_requestOfficerListRefresh = false;     // [최적화] 목록 캐시 재구축 요청 플래그
  static bool s_forceFilterRebuild = false;            // [UX] 캐시 변동 후 필터 리스트 즉각적인 재구축 요청 플래그
  static uintptr_t s_nextTargetFallback = 0;           // [UX] 일괄 변경 시 다음으로 선택할 무장 주소 보관
  static std::unordered_set<int> s_selectedOfficerIDs; // 다중 선택용 보관함
  static std::vector<CachedOfficer> s_allOfficerCache; // [최적화] 전체 무장 캐시 (새로고침 시 1회 구축)
  static std::vector<CachedOfficer> s_filteredIndices; // [최적화] 필터링 및 이름/ID 캐싱된 목록
  static int s_officerNameEditId = -1;                 // JSON 이름 편집 중인 무장 ID
  static char s_officerNameEditBuf[384] = {};
  static bool s_focusDetailNameInput = false; // 상세 패널 이름 클릭 직후 InputText 포커스

  // ID는 고유하므로 단건 상태 변경 시 전체 재스캔 없이 캐시 항목만 즉시 갱신
  static bool UpdateOfficerStatusInAllCache(int officerID, uint8_t newStatus) {
    for (auto &info : s_allOfficerCache) {
      if (info.officerID == officerID) {
        info.statusByte = newStatus;
        return true;
      }
    }
    return false;
  }

  // 선택 무장 상세(0x3D0) 스냅샷 — pBase가 같으면 재복사 안 함; 목록 재오픈·외부 수정 시 무효화 필요
  static alignas(8) uint8_t s_capOfficerSnap[0x3D0];
  static uintptr_t s_capOfficerSnapGame = 0;

  // --- [팝업 및 덤프용 상태] ---
  static bool g_showDumpPopup = false;
  static std::string g_dumpText = "";
  static bool UnsafeRead8(uintptr_t addr, uint8_t *out);
  static bool UnsafeRead16(uintptr_t addr, unsigned short *out);
  static bool UnsafeRead32(uintptr_t addr, uint32_t *out);
  static bool UnsafeReadPtr(uintptr_t addr, uintptr_t *out);
  static bool UnsafeReadMem(uintptr_t addr, void *buf, size_t size);

  // NOTE:
  // SetOfficerCapture/InstallOfficerCave 코드는 성능/안정성 점검을 위해
  // active path에서 분리했습니다. 복구용 원본은
  // `Cheats/legacy/OfficerCaptureCave_legacy.cpp`에 보관합니다.

  // --- [ UI Helper Functions ] ---

  // 무장 마스터 배열(5102 슬롯, stride 0x3D0) 베이스: CE 포인터 체인 우선, 실패 시 기존 역산
  static void RefreshStableOfficerArrayBase(uintptr_t p1Fallback) {
    // [최적화] 이미 유효한 베이스가 있으면 굳이 매 프레임 재탐색하지 않음
    if (s_stableArrayBase > 0x10000 && IsValidPtr(s_stableArrayBase, 8)) {
      return;
    }

    uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t chain = 0;
    if (exe && TryResolveOfficerRosterArrayBase(exe, &chain) && chain > 0x10000) {
      s_stableArrayBase = chain;
      return;
    }
    uintptr_t ref = (g_capturedOfficerBase > 0x10000) ? g_capturedOfficerBase : p1Fallback;
    if (ref <= 0x10000 && g_savedHeroAddr > 0x10000)
      ref = g_savedHeroAddr;
    if (ref > 0x10000 && IsValidPtr(ref + 0x08, sizeof(unsigned short))) {
      unsigned short refID = *(unsigned short *)(ref + 0x08);
      if (refID >= 1 && refID <= 5102)
        s_stableArrayBase = ref - ((refID - 1) * 0x3D0);
    }
  }

  static void PatchMasterData(unsigned short targetID, std::function<void(uintptr_t)> patchFunc) {
    // 목록 창에서 이미 안정 베이스를 관리하므로, 상세 child 렌더에서는
    // 매 프레임 재해석을 하지 않아 프레임 드랍을 줄입니다.
    if (s_stableArrayBase < 0x10000) {
      AddLog(u8"[DEBUG] [PatchMasterData] s_stableArrayBase 유효하지 않음: %p", (void *)s_stableArrayBase);
      return;
    }
    AddLog(u8"[DEBUG] [PatchMasterData] 탐색 시작: ID %d (Base: %p)", (int)targetID, (void *)s_stableArrayBase);
    int foundCount = 0;
    for (int i = 0; i < 5102; i++) {
      uintptr_t targetBase = s_stableArrayBase + (i * 0x3D0);
      RosterStats s = SafeReadRosterStats(targetBase);
      if (s.valid && s.id_08 == targetID) {
        AddLog(u8"[DEBUG] [PatchMasterData] 대상 발견: Index %d, Base %p", i, (void *)targetBase);
        patchFunc(targetBase);
        foundCount++;
        break;
      }
    }
    if (foundCount == 0) {
      AddLog(u8"[DEBUG] [PatchMasterData] 실패: 마스터 배열에서 ID %d를 찾지 못함", (int)targetID);
    }
    AddLog(u8"[DEBUG] [PatchMasterData] 탐색 종료 (발견: %d)", foundCount);
  }

  void DrawOfficerHeader(uintptr_t pGame, float scale, uintptr_t pViewSnap) {
    const uintptr_t pR = (pViewSnap > 0x10000) ? pViewSnap : pGame;
    auto syncHdrSnap = [&]() {
      if (pViewSnap > 0x10000 && IsValidPtr(pGame, 0x3D0))
        memcpy((void *)pViewSnap, (void *)pGame, 0x3D0);
    };
    auto SetupNextTargetFallback = [&]() {
      s_nextTargetFallback = 0;
      if (s_stableArrayBase > 0x10000 && g_capturedOfficerBase != 0) {
        for (size_t i = 0; i < s_filteredIndices.size(); i++) {
          uintptr_t base = s_stableArrayBase + (s_filteredIndices[i].originalIndex * 0x3D0);
          if (base == g_capturedOfficerBase) {
            if (i + 1 < s_filteredIndices.size())
              s_nextTargetFallback = s_stableArrayBase + (s_filteredIndices[i + 1].originalIndex * 0x3D0);
            else if (i > 0)
              s_nextTargetFallback = s_stableArrayBase + (s_filteredIndices[i - 1].originalIndex * 0x3D0);
            break;
          }
        }
      }
      s_forceFilterRebuild = true;
      s_triggerReselection = true;
    };

    uintptr_t forceAddr = *(uintptr_t *)(pR + 0x18);
    unsigned char vtableByte = *(unsigned char *)(pR + 0x10);

    if (ImGui::BeginTable("DetailInfoTable", 2, ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 130.0f * scale);
      ImGui::TableSetupColumn(u8"내용", ImGuiTableColumnFlags_WidthFixed, 500.0f * scale);

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
        uintptr_t corpsAddr = *(uintptr_t *)(pR + 0x20);
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
      unsigned short currentID = *(unsigned short *)(pR + 0x08);
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
      if (s_officerNameEditId == (int)currentID) {
        ImGui::SetNextItemWidth(120.0f * scale);
        ImGui::PushID("OffNameHdr");
        if (s_focusDetailNameInput) {
          ImGui::SetKeyboardFocusHere();
          s_focusDetailNameInput = false;
        }
        bool enter = ImGui::InputText("##e", s_officerNameEditBuf, sizeof(s_officerNameEditBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
          s_officerNameEditId = -1;
        else if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
          if (SaveOfficerNameToJson((int)currentID, s_officerNameEditBuf))
            AddLog(u8"[이름] ID %u → S8RPK_cheat_char.json 저장", (unsigned)currentID);
          else
            AddLog(u8"[이름] ID %u JSON 저장 실패", (unsigned)currentID);
          s_officerNameEditId = -1;
        }
        ImGui::PopID();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.45f, 0.5f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.2f, 0.5f, 0.55f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Selectable(nameValue.c_str(), false, ImGuiSelectableFlags_None, ImVec2(80.0f * scale, 0))) {
          s_officerNameEditId = (int)currentID;
          strncpy_s(s_officerNameEditBuf, sizeof(s_officerNameEditBuf), nameValue.c_str(), _TRUNCATE);
          s_focusDetailNameInput = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip(u8"클릭하여 이름 편집 · Enter 또는 다른 곳 클릭으로 JSON 저장");
        ImGui::PopStyleColor(4);
      }

      {
        ImGui::SameLine();

        // ImGui::TableNextRow();
        // ImGui::TableSetColumnIndex(0);
        // ImGui::AlignTextToFramePadding();
        // ImGui::TextUnformatted(u8"성별");
        // ImGui::TableSetColumnIndex(1);
        // ImGui::AlignTextToFramePadding();

        v_Gender = *(unsigned char *)(pR + 0x30);
        if (v_Gender == 1) {
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), u8"여성");
        } else if (v_Gender == 0) {
          ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"남성");
        } else {
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"알 수 없음 (%d)", v_Gender);
        }
      }

      auto resolveOfficerPtrToName = [](uintptr_t ptr) -> std::string {
        if (ptr <= 0x10000)
          return u8"없음";
        ptr = ptr & 0x0000FFFFFFFFFFFFULL;
        if (ptr <= 0x10000)
          return u8"없음";
        unsigned short id = 0;
        if (UnsafeRead16(ptr + 0x08, &id) && id >= 1 && id <= 5102) {
          if (g_officerNames.count(id))
            return g_officerNames[id];
          return u8"알수없음";
        }
        return u8"없음";
      };

      uintptr_t famPtr = *(uintptr_t *)(pR + 0x40);
      uintptr_t dadPtr = *(uintptr_t *)(pR + 0x48);
      uintptr_t momPtr = *(uintptr_t *)(pR + 0x50);
      ImGui::SameLine();
      ImGui::TextDisabled(u8" [ 가문: %s | 부: %s | 모: %s ]", resolveOfficerPtrToName(famPtr).c_str(),
                          resolveOfficerPtrToName(dadPtr).c_str(), resolveOfficerPtrToName(momPtr).c_str());

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(u8"성향/성격");
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      uint8_t stratTend = *(uint8_t *)(pR + 0x60);
      uint8_t personality = *(uint8_t *)(pR + 0x64);
      const char *stratStr = u8"?";
      if (stratTend == 1)
        stratStr = u8"소극";
      else if (stratTend == 2)
        stratStr = u8"보통";
      else if (stratTend == 3)
        stratStr = u8"호전";
      else if (stratTend == 4)
        stratStr = u8"적극";
      else if (stratTend == 5)
        stratStr = u8"사욕";
      const char *perStr = u8"?";
      if (personality == 1)
        perStr = u8"대담";
      else if (personality == 2)
        perStr = u8"저돌";
      else if (personality == 3)
        perStr = u8"온화";
      else if (personality == 4)
        perStr = u8"침착";
      else if (personality == 5)
        perStr = u8"나약";
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.6f, 1.0f), u8"전략: %s   성격: %s", stratStr, perStr);

      // [최적화] 거주 도시는 선택 변경 시에만 계산하고 캐시를 재사용
      {
        static uintptr_t s_cachedBaseForCity = 0;
        static std::string s_cachedCityName = u8"정보 없음";

        if (s_cachedBaseForCity != g_capturedOfficerBase) {
          s_cachedBaseForCity = g_capturedOfficerBase;
          s_cachedCityName = u8"정보 없음";

          uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
          uintptr_t cityArrayBase = 0;
          if (exeBase) {
            uintptr_t p1 = *(uintptr_t *)(exeBase + 0x34C8630);
            if (p1 && IsValidPtr(p1, 8)) {
              uintptr_t p2 = *(uintptr_t *)(p1);
              if (p2 && IsValidPtr(p2, 8))
                cityArrayBase = *(uintptr_t *)(p2);
            }
          }

          if (cityArrayBase > 0x10000) {
            uintptr_t cityPtr = *(uintptr_t *)(pR + 0x20);
            if (cityPtr >= cityArrayBase) {
              int idx = (int)((cityPtr - cityArrayBase) / 0x2A0);
              if (idx >= 0 && idx < g_CityCount) {
                s_cachedCityName = g_CityList[idx].cityname;
              }
            }
          }
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(u8"거주 도시");
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%s", s_cachedCityName.c_str()); // 노란색 계열
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
      case 0x38:
        stateStr = u8"두령";
        stateColor = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
        break;
      case 0x48:
        stateStr = u8"동지";
        stateColor = ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
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
              if (p2 && IsValidPtr(p2, 8))
                cityArrayBase = *(uintptr_t *)(p2);
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
            AddLog(u8"[DEBUG] [PatchStatus] 시작: Base %p", (void *)base);
            ModifyStat(base, 0x10, 0x58, 1); // 상태: 재야(0x58)
            ModifyStat(base, 0x36, 255, 2);  // 몰년 연장
            ModifyStat(base, 0xEE, 200, 1);  // 행동력
            ModifyStat(base, 0x374, 0, 4);   // 사망 플래그 제거

            if (targetCityPtr) {
              AddLog(u8"[DEBUG] [PatchStatus] 도시 패치 시도 (Addr:%p -> CityPtr:%p)", (void *)(base + 0x20),
                     (void *)targetCityPtr);
              DWORD oldP;
              if (VirtualProtect((LPVOID)(base + 0x20), 8, PAGE_READWRITE, &oldP)) {
                *(uintptr_t *)(base + 0x20) = targetCityPtr;
                VirtualProtect((LPVOID)(base + 0x20), 8, oldP, &oldP);
                AddLog(u8"[DEBUG] [PatchStatus] 도시 패치 성공");
              } else {
                AddLog(u8"[ERROR] [PatchStatus] 도시 패치 실패 (VirtualProtect 에러)");
              }
            }
            AddLog(u8"[DEBUG] [PatchStatus] 종료");
          };

          unsigned short officerID = *(unsigned short *)(pR + 0x08);
          PatchStatus(pGame);
          PatchMasterData(officerID, PatchStatus);
          SetupNextTargetFallback();
          if (!UpdateOfficerStatusInAllCache((int)officerID, 0x58)) {
            s_requestOfficerListRefresh = true;
          }
          syncHdrSnap();
          AddLog(u8"[LIFE] %s 무장을 주인공 도시(Index:%d)로 부활시켰습니다.", g_officerNames[officerID].c_str(),
                 heroCityIdx);
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
          unsigned short pBaseID = *(unsigned short *)(pR + 0x08);
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
            if (VirtualProtect((LPVOID)(pGame + 0x20), 8, PAGE_READWRITE, &oldP)) {
              *(uintptr_t *)(pGame + 0x20) = targetAddr;
              VirtualProtect((LPVOID)(pGame + 0x20), 8, oldP, &oldP);
            }

            // [패치] 현재 가로챈 객체(UI용)와 마스터 배열 내의 원본을 동시에 수정
            auto PatchStatus = [&](uintptr_t base) {
              AddLog(u8"[DEBUG] [PatchStatus(City)] 시작: Base %p", (void *)base);
              ModifyStat(base, 0x10, 0x58, 1); // 상태: 재야(0x58)
              ModifyStat(base, 0x36, 255, 2);  // 몰년: 수명 연장
              ModifyStat(base, 0xEE, 200, 1);  // 행동력
              ModifyStat(base, 0x374, 0, 4);   // 사망 플래그 제거

              if (targetAddr) {
                AddLog(u8"[DEBUG] [PatchStatus(City)] 도시 패치 시도 (Addr:%p -> CityPtr:%p)", (void *)(base + 0x20),
                       (void *)targetAddr);
                DWORD oldP2;
                if (VirtualProtect((LPVOID)(base + 0x20), 8, PAGE_READWRITE, &oldP2)) {
                  *(uintptr_t *)(base + 0x20) = targetAddr;
                  VirtualProtect((LPVOID)(base + 0x20), 8, oldP2, &oldP2);
                  AddLog(u8"[DEBUG] [PatchStatus(City)] 도시 패치 성공");
                } else {
                  AddLog(u8"[ERROR] [PatchStatus(City)] 도시 패치 실패 (VirtualProtect 에러)");
                }
              }
              AddLog(u8"[DEBUG] [PatchStatus(City)] 종료");
            };

            PatchStatus(pGame);
            PatchMasterData(pBaseID, PatchStatus);
            SetupNextTargetFallback();
            if (!UpdateOfficerStatusInAllCache((int)pBaseID, 0x58)) {
              s_requestOfficerListRefresh = true;
            }
            syncHdrSnap();

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
            AddLog(u8"[DEBUG] [Move] 이동 시도 (Base:%p, Target:%s)", (void *)pGame,
                   g_CityList[s_selectedCityIdx].cityname);
            *(uintptr_t *)(pGame + 0x20) = targetAddr;
            syncHdrSnap();
            AddLog(u8"[이동] %s 무장을 [%s] 도시로 이동시켰습니다!",
                   DX11Base::g_officerNames[*(unsigned short *)(pR + 0x08)].c_str(),
                   g_CityList[s_selectedCityIdx].cityname);
          }
        }
        ImGui::PopStyleColor(3);
      } else if (vtableByte == 0x68 || vtableByte == 0x78) { // 미발견 무장: 재야로 변경 버튼 제공
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(u8"재야", ImVec2(0, 24 * scale))) {
          unsigned short pBaseID = *(unsigned short *)(pR + 0x08);
          auto patchRonin = [&](uintptr_t base) { ModifyStat(base, 0x10, 0x58, 1); };
          patchRonin(pGame);
          PatchMasterData(pBaseID, patchRonin);
          SetupNextTargetFallback();
          if (!UpdateOfficerStatusInAllCache((int)pBaseID, 0x58)) {
            s_requestOfficerListRefresh = true;
          }
          syncHdrSnap();

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

      RenderStatRow(pGame, u8"얼굴 번호", 0x2E, 2, &v_OfficerID, scale);
      RenderStatRow(pGame, u8"모델 번호", 0xA5, 1, &v_ModelNo, scale);
      RenderStatRow(pGame, u8"모델 색상", 0xA6, 1, &v_ModelColor, scale);
      RenderStatRow(pGame, u8"등장년도", 0x32, 2, &v_Appear, scale);
      RenderStatRow(pGame, u8"생년", 0x34, 2, &v_Birth, scale);
      RenderStatRow(pGame, u8"몰년(수명)", 0x36, 2, &v_Death, scale);

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

    // [최적화] 기재 상세 파싱은 선택 변경 시 1회 캐시
    static uintptr_t s_cachedTalentBase = 0;
    static TalentInfo s_cachedTalentInfo[3] = {};
    static bool s_cachedTalentValid[3] = {false, false, false};
    static std::vector<std::string> s_cachedTalentLines[3];
    static bool s_forceTalentCacheRefresh = false;

    if (s_cachedTalentBase != g_capturedOfficerBase || s_forceTalentCacheRefresh) {
      s_cachedTalentBase = g_capturedOfficerBase;
      for (int i = 0; i < 3; i++) {
        TalentInfo info;
        s_cachedTalentValid[i] = GetOfficerTalentDetailed(pBase, i, info);
        if (s_cachedTalentValid[i]) {
          s_cachedTalentInfo[i] = info;
        } else {
          memset(&s_cachedTalentInfo[i], 0, sizeof(TalentInfo));
        }

        // [최적화] 기재 효과 설명 문자열을 선택 변경 시 1회만 생성
        s_cachedTalentLines[i].clear();
        if (s_cachedTalentValid[i]) {
          for (int j = 0; j < 6; j++) {
            if (s_cachedTalentInfo[i].effects[j].effectId == 0)
              break;

            std::string desc = GetFormattedEffectDescription(s_cachedTalentInfo[i].effects[j]);
            if (!desc.empty()) {
              char line[512];
              snprintf(line, sizeof(line), u8"  - %s (ID:%d)", desc.c_str(), s_cachedTalentInfo[i].effects[j].effectId);
              s_cachedTalentLines[i].push_back(line);
            } else {
              char line[512];
              snprintf(line, sizeof(line), u8"  - 효과 %d: %d (값1: %d, 값2: %d)", j + 1,
                       s_cachedTalentInfo[i].effects[j].effectId, s_cachedTalentInfo[i].effects[j].val1,
                       s_cachedTalentInfo[i].effects[j].val2);
              s_cachedTalentLines[i].push_back(line);
            }
          }
        }
      }
      s_forceTalentCacheRefresh = false;
    }

    for (int i = 0; i < 3; i++) {
      TalentInfo info;
      ImGui::PushID(i);
      ImGui::BeginGroup();
      if (s_cachedTalentValid[i]) {
        info = s_cachedTalentInfo[i];
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), u8"[*] 기재 %d : %s (ID %d)", i + 1, GetTalentName(info.id),
                           info.id);
        ImGui::Indent(15.0f * scale);
        for (const auto &line : s_cachedTalentLines[i]) {
          ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", line.c_str());
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
          s_forceTalentCacheRefresh = true;
        }
      }
      ImGui::EndGroup();
      if (i < 2)
        ImGui::Separator();
      ImGui::PopID();
    }
  }

  void DrawSelectedOfficerWindow(ImVec2 mPos, ImVec2 mSize, float scale, bool asChild) {
    // 메타데이터 로딩을 프레임마다 수행하지 않도록 제한 (I/O 스파이크 방지)
    static ULONGLONG s_lastMetaReloadMs = 0;
    ULONGLONG nowMs = GetTickCount64();
    if (s_lastMetaReloadMs == 0 || (nowMs - s_lastMetaReloadMs) >= 2000) {
      LoadOfficerNames();
      LoadEffectDefinitions();
      s_lastMetaReloadMs = nowMs;
    }
    if (!asChild && !bShowSelectedOfficerWin) {
      return;
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
    if (!IsValidPtr(pBase, 0x3D0)) {
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), u8"무장 데이터 포인터가 유효하지 않습니다. (Base: %p)",
                         (void *)pBase);
      ImGui::BulletText(
          u8"특정 세이브(등록무장/고대무장)에서 배열 기준점이 어긋나거나, 임시 객체를 가리키는 경우가 있습니다.");
      if (asChild)
        ImGui::EndChild();
      else
        ImGui::End();
      return;
    }
    // [최적화] 매 프레임 호출되지만 내부에서 유효성 체크 후 즉시 반환함
    RefreshStableOfficerArrayBase(0);
    if (!asChild) {
      ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), u8"● 배열 기반 선택 편집 모드");
      ImGui::Spacing();
      ImGui::Checkbox(u8"게임 화면 클릭 허용 (무장 선택 시 필요)", &bAllowGameClick);
      ImGui::Separator();
    }

    if (bShowDebug) {
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
      // ImGui::SetWindowSize(ImVec2(0, 0)); // 성능 최적화를 위해 제거
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
            for (const auto &info : s_filteredIndices) {
              uintptr_t targetBase = s_stableArrayBase + (info.originalIndex * 0x3D0);
              unsigned short currentID = *(unsigned short *)(targetBase + 0x08);
              unsigned char vByte = *(unsigned char *)(targetBase + 0x10);
              if (vByte == 0x68 || vByte == 0x78) {
                ModifyStatFast(targetBase + 0x10, 0x58, 1);
                count++;
              }
            }
            VirtualProtect((LPVOID)s_stableArrayBase, 5102 * 0x3D0, old, &old);
          }

          // [최적화] 전체 스캔 대신 캐시에서 해당 무장들의 상태를 즉시 업데이트
          for (auto &info : s_allOfficerCache) {
            if (info.statusByte == 0x68 || info.statusByte == 0x78) {
              info.statusByte = 0x58;
            }
          }
        }
        if (count > 0)
          AddLog(u8"[LIFE] 현재 필터링된 %d명의 미발견 무장을 재야(0x58) 상태로 변경했습니다.", count);
        s_nextTargetFallback = 0; // 전부 재야 처리이므로 현재 필터에 남은 무장이 없음
        s_forceFilterRebuild = true;
        s_triggerReselection = true;
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

            // [최적화] 캐시에서 선택된 무장들의 상태 업데이트
            for (auto &info : s_allOfficerCache) {
              if (s_selectedOfficerIDs.count(info.officerID)) {
                info.statusByte = 0x58;
              }
            }
          }

          // [UX] 현재 선택된 무장의 위치를 바탕으로 삭제되지 않을 다음 무장을 찾아서 지정
          s_nextTargetFallback = 0;
          if (s_stableArrayBase > 0x10000 && g_capturedOfficerBase != 0) {
            for (size_t i = 0; i < s_filteredIndices.size(); i++) {
              uintptr_t base = s_stableArrayBase + (s_filteredIndices[i].originalIndex * 0x3D0);
              if (base == g_capturedOfficerBase) {
                for (size_t j = i + 1; j < s_filteredIndices.size(); j++) {
                  if (s_selectedOfficerIDs.count(s_filteredIndices[j].officerID) == 0) {
                    s_nextTargetFallback = s_stableArrayBase + (s_filteredIndices[j].originalIndex * 0x3D0);
                    break;
                  }
                }
                if (s_nextTargetFallback == 0) {
                  for (int j = (int)i - 1; j >= 0; j--) {
                    if (s_selectedOfficerIDs.count(s_filteredIndices[j].officerID) == 0) {
                      s_nextTargetFallback = s_stableArrayBase + (s_filteredIndices[j].originalIndex * 0x3D0);
                      break;
                    }
                  }
                }
                break;
              }
            }
          }

          s_forceFilterRebuild = true;
          s_triggerReselection = true;
          AddLog(u8"[LIFE] 선택한 %d명의 미발견 무장을 재야(0x58) 상태로 변경했습니다.", count);
          s_selectedOfficerIDs.clear();
        }
      }
      ImGui::PopStyleColor();
      ImGui::Spacing();
    }

    // [최적화] 목록에서 클릭하여 무장이 변경되었을 때만 딱 한 번 스냅샷 읽기
    static bool s_hasListSnapshot = false;
    if (pBase != s_capOfficerSnapGame || !s_hasListSnapshot) {
      if (pBase != 0 && IsValidPtr(pBase, 0x3D0)) {
        memcpy(s_capOfficerSnap, (void *)pBase, 0x3D0);
        s_capOfficerSnapGame = pBase;
        s_hasListSnapshot = true;
      } else {
        s_hasListSnapshot = false;
      }
    }
    const uintptr_t pSnap = (uintptr_t)s_capOfficerSnap;
    g_officerInlineReadPtr = pSnap;

    // [요청 반영] 소양(EXP) 탭은 주인공 무장일 때만 표시
    bool isHeroOfficer = false;
    {
      uintptr_t gameBase = DX11Base::GetGameBase();
      if (gameBase > 0x10000) {
        uintptr_t heroBase = *(uintptr_t *)(gameBase + 0xE0);
        if (heroBase > 0x10000 && IsValidPtr(heroBase + 0x08, 2) && IsValidPtr(pBase + 0x08, 2)) {
          uint16_t heroId = *(uint16_t *)(heroBase + 0x08);
          uint16_t selectedId = *(uint16_t *)(pBase + 0x08);
          isHeroOfficer = (heroId == selectedId);
        }
      }
    }

    if (ImGui::BeginTabBar("SelectedOfficerTabs")) {
      if (ImGui::BeginTabItem(u8"상세 정보")) {
        if (currentTabIdx != 0) {
          currentTabIdx = 0;
        }
        DrawOfficerHeader(pBase, scale, pSnap);
        DrawOfficerTalents(pSnap, scale);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(u8"능력/상태")) {
        RenderBasicTab(pBase, scale, true);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(u8"기능")) {
        if (currentTabIdx != 1) {
          currentTabIdx = 1;
        }
        RenderResearchTab(pBase, scale);
        ImGui::EndTabItem();
      }
      if (isHeroOfficer) {
        if (ImGui::BeginTabItem(u8"소양(EXP)")) {
          if (currentTabIdx != 2) {
            currentTabIdx = 2;
          }
          RenderExpTab(pBase, scale);
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    g_officerInlineReadPtr = 0;
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
    static bool s_wasOpen = false;
    static bool s_deferInitialBuild = false; // 창 오픈 직후 1프레임은 무거운 로딩을 미룸
    if (!bShowOfficerListWin) {
      s_wasOpen = false;
      s_deferInitialBuild = false;
      return;
    }

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

    // 리스트 창의 크기를 수동으로 조절 가능하게 하고, AlwaysAutoResize를 제거하여 레이아웃 부하를 없앱니다.
    if (ImGui::Begin(u8"모든 무장 편집 리스트###OfficerListWin", &bShowOfficerListWin, ImGuiWindowFlags_None)) {
      // [최적화] 매 프레임 주소 체인을 탐색하던 로직을 캐싱 방식으로 변경
      static uintptr_t s_cachedMasterArrayBase = 0;
      static bool s_hasMasterArray = false;
      static ULONGLONG s_lastBaseCheckMs = 0;
      ULONGLONG nowMs = GetTickCount64();

      if (s_cachedMasterArrayBase == 0 || (nowMs - s_lastBaseCheckMs) > 2000) {
        s_lastBaseCheckMs = nowMs;
        uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t foundBase = 0;
        if (exe && TryResolveOfficerRosterArrayBase(exe, &foundBase) && foundBase > 0x10000) {
          s_cachedMasterArrayBase = foundBase;
          s_hasMasterArray = true;
        }
      }

      uintptr_t preResolvedArrayBase = s_cachedMasterArrayBase;
      bool hasRosterArray = s_hasMasterArray;

      if (!p1 && !hasRosterArray) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), u8"무장 배열 데이터를 찾을 수 없습니다.");
        ImGui::BulletText(u8"인게임(전략 화면 등)으로 진입해야 활성화됩니다.");
        ImGui::Spacing();
        if (ImGui::Button(u8"닫기")) {
          bShowOfficerListWin = false;
        }
        ImGui::End();
        return;
      }

      // p1이 없어 위에서 return 하면 s_wasOpen을 올리지 않음 → 다음 프레임에도 justOpened로
      // 자동 새로고침이 한 번 실행되도록 함 (첫 프레임에 p1만 비어 있던 경우의 버그 수정)
      bool justOpened = !s_wasOpen;
      s_wasOpen = true;
      if (justOpened) {
        // 첫 프레임은 창만 즉시 띄우고, 다음 프레임부터 캐시 구축 시작
        s_deferInitialBuild = true;
      }

      static char s_searchBuf[64] = "";
      static int s_scrollToIndex = -1;
      static int s_lastFilterForCache = -2;
      static bool s_forceOfficerListRefresh = false;
      static bool s_pendingSearch = false;
      static std::string s_pendingSearchQuery;
      static bool s_cacheBuildInProgress = false;
      static int s_cacheBuildCursor = 0;
      static bool s_cacheFirstChunkDone = false;
      static bool s_cacheSeenIDs[5103] = {};
      static ULONGLONG s_lastFilterInputMs = 0;
      static int s_prevObservedFilter = -2;
      const ULONGLONG nowMsUi = GetTickCount64();
      // 목록 구축은 s_allOfficerCache에 증분 append 하고, 완료 시 1회 정렬한다.
      if (s_requestOfficerListRefresh) {
        s_forceOfficerListRefresh = true;
        s_requestOfficerListRefresh = false;
      }
      if (justOpened) {
        s_forceOfficerListRefresh = true;
        s_capOfficerSnapGame = 0; // 같은 무장 선택 유지 시에도 게임 메모리에서 스냅샷 재수집 (CE 등 외부 변경 반영)
      }
      if (justOpened || s_forceOfficerListRefresh) {
        LoadOfficerNames();
      }
      bool doSearch = false;

      // [최적화] 창 크기 강제 초기화 제거 (성능 저하의 핵심 원인)
      static uintptr_t lastListBase = 0;
      if (g_capturedOfficerBase != lastListBase) {
        lastListBase = g_capturedOfficerBase;
      }

      // UI에서 넘겨준 p1은 배열에 속하지 않은 임시 주소일 수 있음. 주인공 ID는 p1에서만 사용.
      unsigned short heroID_real = 0;
      if (p1 && IsValidPtr((uintptr_t)p1 + 0x08, sizeof(unsigned short)))
        heroID_real = *(unsigned short *)(p1 + 0x08);

      // 배열 베이스는 매 프레임 재탐색하지 않고, 목록 갱신이 필요할 때만 갱신
      static uintptr_t s_cachedListArrayBase = 0;
      uintptr_t arrayBase = s_cachedListArrayBase;
      if (arrayBase == 0 && hasRosterArray) {
        arrayBase = preResolvedArrayBase;
        s_cachedListArrayBase = preResolvedArrayBase;
      }

      ImGui::SetNextItemWidth(100.0f * scale);
      if (ImGui::InputTextWithHint(u8"##search", u8"이름 or ID", s_searchBuf, sizeof(s_searchBuf),
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
        doSearch = true;
      }
      bool isSearchBoxActive = ImGui::IsItemActive();
      ImGui::SameLine();
      if (ImGui::Button(u8"찾기")) {
        doSearch = true;
      }
      // ImGui::SameLine();
      // if (ImGui::Button(u8"목록 새로고침")) {
      //   s_forceOfficerListRefresh = true;
      //   s_capOfficerSnapGame = 0;
      // }

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
          for (const auto &info : s_filteredIndices) {
            s_selectedOfficerIDs.insert(info.officerID);
          }
        }
        ImGui::PopStyleColor();
      }
      ImGui::SameLine();
      if (hasSelections)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"(%zu명)", s_selectedOfficerIDs.size());
      else
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"(0명)");

      if (bShowDebug) {
        ImGui::SameLine();
        ImGui::Checkbox(u8"클릭 차단", &bBlockClickInOfficerList);
        ImGui::SameLine();
        if (ImGui::Button(u8"덤프")) {
          if (g_capturedOfficerBase != 0) {
            DumpOfficerData(g_capturedOfficerBase);
          }
        }
      }

      // 필터 버튼 오른쪽 정렬 [최적화: CalcTextSize 캐싱]
      {
        static float s_cachedTotalFilterWidth = 0.0f;
        if (s_cachedTotalFilterWidth <= 0.0f) {
          const char *filterLabels[] = {u8"군사", u8"일반",   u8"두령", u8"동지", u8"태수", u8"도독", u8"군주",
                                        u8"재야", u8"미발견", u8"사망", u8"NPC",  u8"전부"};
          float fp = ImGui::GetStyle().FramePadding.x;
          for (auto *lbl : filterLabels) {
            s_cachedTotalFilterWidth += ImGui::CalcTextSize(lbl).x + fp * 2.0f;
          }
          s_cachedTotalFilterWidth += ImGui::GetStyle().ItemSpacing.x * (IM_ARRAYSIZE(filterLabels) - 1);
        }

        float posX = ImGui::GetContentRegionMax().x - s_cachedTotalFilterWidth;
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
      DrawFilterButton(u8"두령", 0x38);
      ImGui::SameLine();
      DrawFilterButton(u8"동지", 0x48);
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

      if (s_prevObservedFilter != s_currentFilter) {
        s_prevObservedFilter = s_currentFilter;
        s_lastFilterInputMs = nowMsUi;
      }

      // [최적화] 필터 리스트: 창을 열었을 때 또는 버튼 클릭 시에만 메모리 스캔. 필터 변경은 캐시에서만 처리.
      bool needsUpdate = false;
      const bool immediateReason = s_triggerReselection || s_forceOfficerListRefresh || s_cacheBuildInProgress;
      // 검색어나 필터 변경은 메모리를 다시 읽지 않고 캐시된 데이터(s_allOfficerCache)를 필터링하는 용도로만 사용
      if (!s_deferInitialBuild && immediateReason) {
        needsUpdate = true;
      }

      if (needsUpdate) {
        bool needsRebuildAllCache = s_forceOfficerListRefresh || (s_cachedListArrayBase == 0);
        if (needsRebuildAllCache && !s_cacheBuildInProgress) {
          // 배열 베이스: CE 기준 1번 무장 포인터 체인 우선(OfficerRosterResolve), 실패 시 역산
          RefreshStableOfficerArrayBase(p1);
          arrayBase = s_stableArrayBase;
          s_cachedListArrayBase = arrayBase;

          // [최적화] 전체 캐시 구축을 프레임 분할 처리로 시작
          s_cacheBuildInProgress = true;
          s_cacheBuildCursor = 0;
          s_cacheFirstChunkDone = false;
          memset(s_cacheSeenIDs, 0, sizeof(s_cacheSeenIDs));
          s_allOfficerCache.clear();
          s_allOfficerCache.reserve(5102);
          s_filteredIndices.clear();
        } else {
          arrayBase = s_cachedListArrayBase;
        }

        if (s_forceOfficerListRefresh && s_cacheBuildInProgress) {
          // 재빌드 시작 이후에는 플래그를 내리고, in-progress 상태로 계속 진행
          s_forceOfficerListRefresh = false;
        }
        s_lastFilterForCache = s_currentFilter;

        if (s_cacheBuildInProgress) {
          arrayBase = s_cachedListArrayBase;
          // 요청사항 반영:
          // 1) 창이 뜨는 즉시 20명 먼저 표시
          // 2) 나머지는 프레임 분할로 백그라운드 처리
          const int kChunkPerFrame = s_cacheFirstChunkDone ? 384 : 20;
          int endIdx = s_cacheBuildCursor + kChunkPerFrame;
          if (endIdx > 5102)
            endIdx = 5102;

          for (int i = s_cacheBuildCursor; i < endIdx; i++) {
            uintptr_t targetBase = arrayBase + (i * 0x3D0);

            RosterStats s = SafeReadRosterStats(targetBase);
            if (!s.valid)
              continue;
            if (s.id_08 < 1 || s.id_08 > 5102)
              continue;
            if (s_cacheSeenIDs[s.id_08])
              continue;
            if (!IsValidPtr(targetBase + 0x10, 1))
              continue;

            s_cacheSeenIDs[s.id_08] = true;
            uint8_t status = *(uint8_t *)(targetBase + 0x10);
            std::string name = u8"???";
            auto it = g_officerNames.find((int)s.id_08);
            if (it != g_officerNames.end()) {
              name = it->second;
            }
            s_allOfficerCache.push_back({i, (int)s.id_08, status, name});
          }

          s_cacheBuildCursor = endIdx;
          if (!s_cacheFirstChunkDone) {
            s_cacheFirstChunkDone = true;
          }

          if (s_cacheBuildCursor >= 5102) {
            // 구축 완료 시 1회만 정렬
            std::sort(s_allOfficerCache.begin(), s_allOfficerCache.end(),
                      [](const CachedOfficer &a, const CachedOfficer &b) { return a.officerID < b.officerID; });
            s_cacheBuildInProgress = false;
          }
        }
      }

      // [최적화] 필터링 수행: 캐시가 변경되었거나 필터가 바뀌었을 때만 1회 수행
      static int s_lastAppliedFilter = -2;
      static size_t s_lastCacheSize = 0;
      bool filterTrigger = (s_lastAppliedFilter != s_currentFilter) || (s_lastCacheSize != s_allOfficerCache.size()) ||
                           s_forceFilterRebuild;

      if (filterTrigger || s_forceOfficerListRefresh) {
        s_forceFilterRebuild = false;
        s_filteredIndices.clear();
        s_filteredIndices.reserve(s_allOfficerCache.size());
        for (const auto &info : s_allOfficerCache) {
          if (s_currentFilter == -1 || info.statusByte == s_currentFilter ||
              (s_currentFilter == 0x68 && info.statusByte == 0x78)) {
            s_filteredIndices.push_back(info);
          }
        }
        s_lastAppliedFilter = s_currentFilter;
        s_lastCacheSize = s_allOfficerCache.size();
      }

      if (s_cacheBuildInProgress) {
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.3f, 1.0f), u8"목록 로딩 중... (%d/5102)", s_cacheBuildCursor);
      } else if (s_deferInitialBuild) {
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.3f, 1.0f), u8"목록 로딩 준비 중...");
        // 다음 프레임부터 실제 캐시 구축/분할 로딩 시작
        s_deferInitialBuild = false;
      }

      // [최적화] 매 프레임 수천 번 돌던 '선택 유효성 확인' 루프를 필터링 시에만 수행하도록 변경
      static bool s_selectedExistsInFiltered = false;
      static int s_lastFilterForSelection = -2;
      if (filterTrigger || s_triggerReselection) {
        s_selectedExistsInFiltered = false;
        if (g_capturedOfficerBase != 0 && arrayBase != 0) {
          for (const auto &info : s_filteredIndices) {
            uintptr_t base = arrayBase + (info.originalIndex * 0x3D0);
            if (base == g_capturedOfficerBase) {
              s_selectedExistsInFiltered = true;
              break;
            }
          }
        }
      }

      bool needAutoReselect = (s_lastFilterForSelection != s_currentFilter) || s_triggerReselection;
      if (!s_selectedExistsInFiltered) {
        needAutoReselect = true;
      }

      if (needAutoReselect) {
        uintptr_t newBase = 0;

        if (s_nextTargetFallback != 0) {
          for (const auto &info : s_filteredIndices) {
            uintptr_t base = arrayBase + (info.originalIndex * 0x3D0);
            if (base == s_nextTargetFallback) {
              newBase = base;
              break;
            }
          }
        }

        if (newBase == 0 && !s_filteredIndices.empty()) {
          newBase = arrayBase + (s_filteredIndices[0].originalIndex * 0x3D0);
        }

        if (newBase != 0) {
          g_capturedOfficerBase = newBase;
          s_lastCapturedByUI = newBase;
          s_selectedExistsInFiltered = true;
        } else {
          g_capturedOfficerBase = 0;
          s_selectedExistsInFiltered = false;
        }
        s_lastFilterForSelection = s_currentFilter;
        s_triggerReselection = false;
        s_nextTargetFallback = 0;
      }

      if (doSearch && s_searchBuf[0] != '\0') {
        s_pendingSearch = true;
        s_pendingSearchQuery = s_searchBuf;
      }

      // [수정] 분할 로딩 중 입력된 검색 요청을 보존하고, 캐시가 준비되면 자동 실행
      if (s_pendingSearch && !s_filteredIndices.empty() && !s_cacheBuildInProgress) {
        s_scrollToIndex = -1;
        bool foundInCurrentFilter = false;
        for (size_t idx = 0; idx < s_filteredIndices.size(); idx++) {
          const auto &info = s_filteredIndices[idx];
          if (std::to_string(info.officerID) == s_pendingSearchQuery ||
              info.displayName.find(s_pendingSearchQuery) != std::string::npos) {
            s_scrollToIndex = static_cast<int>(idx);
            foundInCurrentFilter = true;
            break;
          }
        }

        if (!foundInCurrentFilter) {
          // 현재 필터에서 못 찾으면 전체 캐시에서 검색
          const CachedOfficer *foundGlobal = nullptr;
          for (const auto &info : s_allOfficerCache) {
            if (std::to_string(info.officerID) == s_pendingSearchQuery ||
                info.displayName.find(s_pendingSearchQuery) != std::string::npos) {
              foundGlobal = &info;
              break;
            }
          }

          if (foundGlobal) {
            // 결과가 필터에 걸려 숨겨진 경우 전부 필터로 전환 후 다음 프레임에 다시 검색
            if (s_currentFilter != -1) {
              s_currentFilter = -1;
              s_lastFilterForCache = -2;
              s_triggerReselection = true;
            } else if (arrayBase != 0) {
              // 이미 전부 필터인데 못 찾았던 경우(캐시 재구성 직후 등) 바로 선택 갱신
              g_capturedOfficerBase = arrayBase + (foundGlobal->originalIndex * 0x3D0);
              s_lastCapturedByUI = g_capturedOfficerBase;
            }
            // 전부 필터 재구성 후 스크롤 포커스를 위해 pending 유지
          } else {
            // 전체에서도 못 찾는 경우 요청 종료
            s_pendingSearch = false;
          }
        } else {
          s_pendingSearch = false;
        }
      }

      ImGui::Separator();

      // Left Split Pane 너비를 380에서 280으로 축소하여 콤팩트하게 만듭니다.
      ImGui::BeginChild("OfficerListPane", ImVec2(280 * scale, 0), true);

      // 행 높이를 별도로 하드코딩하지 않아 클리퍼(Clipper) 스크롤 끝이 잘리지 않게 합니다.
      const float APPROX_HEIGHT = ImGui::GetTextLineHeightWithSpacing() + 3.0f;

      if (ImGui::BeginTable("OfficerListTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
        // 검색 성공 직후: 대략적인 위치로 먼저 이동
        if (s_scrollToIndex >= 0 && s_scrollToIndex < (int)s_filteredIndices.size()) {
          ImGui::SetScrollY(s_scrollToIndex * APPROX_HEIGHT);
          g_capturedOfficerBase = arrayBase + (s_filteredIndices[s_scrollToIndex].originalIndex * 0x3D0);
          s_lastCapturedByUI = g_capturedOfficerBase;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(u8"V", ImGuiTableColumnFlags_WidthFixed, 22.0f * scale);
        ImGui::TableSetupColumn(u8"무장", ImGuiTableColumnFlags_WidthFixed, 140.0f * scale);
        ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)s_filteredIndices.size(), APPROX_HEIGHT);
        while (clipper.Step()) {
          for (int row_idx = clipper.DisplayStart; row_idx < clipper.DisplayEnd; row_idx++) {
            const auto &info = s_filteredIndices[row_idx];
            uintptr_t targetBase = arrayBase + (info.originalIndex * 0x3D0);

            ImGui::TableNextRow();
            ImGui::AlignTextToFramePadding();

            const int officerID = info.officerID;
            const std::string &displayName = info.displayName;
            bool isSelected = (g_capturedOfficerBase == targetBase);

            ImGui::PushID(info.originalIndex);

            // 1열: 체크박스
            ImGui::TableNextColumn();
            bool isMultiSelected = s_selectedOfficerIDs.count(officerID) > 0;
            if (ImGui::Checkbox("##sel", &isMultiSelected)) {
              if (isMultiSelected)
                s_selectedOfficerIDs.insert(officerID);
              else
                s_selectedOfficerIDs.erase(officerID);
            }

            // 2열: 무장 이름
            ImGui::TableNextColumn();
            char label[256];
            if (officerID == heroID_real)
              snprintf(label, sizeof(label), u8"★ %s (주인공)", displayName.c_str());
            else
              snprintf(label, sizeof(label), "%s", displayName.c_str());

            if (s_officerNameEditId == officerID) {
              ImGui::SetNextItemWidth(-1.0f);
              ImGui::PushID("OffNameInput");
              if (ImGui::InputText("##e", s_officerNameEditBuf, sizeof(s_officerNameEditBuf),
                                   ImGuiInputTextFlags_EnterReturnsTrue) ||
                  ImGui::IsItemDeactivatedAfterEdit()) {
                if (SaveOfficerNameToJson(officerID, s_officerNameEditBuf))
                  AddLog(u8"[이름] ID %d → S8RPK_cheat_char.json 저장", officerID);
                s_officerNameEditId = -1;
              }
              if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                s_officerNameEditId = -1;
              ImGui::PopID();
            } else {
              if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_None)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                  s_officerNameEditId = officerID;
                  strncpy_s(s_officerNameEditBuf, sizeof(s_officerNameEditBuf), displayName.c_str(), _TRUNCATE);
                } else if (IsValidPtr(targetBase, 0x3D0)) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                }
              }
              if (ImGui::IsItemFocused() && g_capturedOfficerBase != targetBase) {
                if (IsValidPtr(targetBase, 0x3D0)) {
                  g_capturedOfficerBase = targetBase;
                  s_lastCapturedByUI = targetBase;
                }
              }
              if (s_scrollToIndex == row_idx) {
                ImGui::SetKeyboardFocusHere(-1);
                ImGui::SetScrollHereY(0.5f);
                s_scrollToIndex = -1;
              }
            }

            // 3열: 무장 ID
            ImGui::TableNextColumn();
            char idStr[16];
            snprintf(idStr, sizeof(idStr), "%d", officerID);
            if (ImGui::Selectable(idStr, isSelected, ImGuiSelectableFlags_None)) {
              if (IsValidPtr(targetBase, 0x3D0)) {
                g_capturedOfficerBase = targetBase;
                s_lastCapturedByUI = targetBase;
              }
            }
            if (ImGui::IsItemFocused() && g_capturedOfficerBase != targetBase) {
              if (IsValidPtr(targetBase, 0x3D0)) {
                g_capturedOfficerBase = targetBase;
                s_lastCapturedByUI = targetBase;
                ImGui::SetScrollHereY(0.5f);
              }
            }
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndChild(); // 왼쪽 리스트 패널 종료

      ImGui::SameLine();

      ImGui::BeginChild("OfficerDetailPane", ImVec2(520 * scale, 0), true);

      if (isSearchBoxActive) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), u8"검색 입력 중...");
        ImGui::Separator();
        ImGui::TextDisabled(u8"입력 반응 속도를 위해 상세 패널 갱신을 잠시 중지합니다.");
      } else if (g_capturedOfficerBase != 0) {
        DrawSelectedOfficerWindow(ImVec2(0, 0), ImVec2(0, 0), scale, true);
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"왼쪽 리스트에서 상세 편집할 무장을 클릭 하세요.");
      }
      ImGui::EndChild(); // 오른쪽 패널 종료

      DrawOfficerDumpPopup(scale);
    } else {
      // 창이 닫힐 때 플래그 초기화
      s_wasOpen = false;
    }
    ImGui::End();
  }

  // --- 배우자 스캐너 상태 ---
  static std::atomic<bool> s_isSpouseScanning{false};
  static std::atomic<float> s_spouseScanProgress{0.0f};

  struct SpouseEntry {
    uintptr_t pointerAddr; // HeroPtr address in relationship
    uintptr_t targetBase;  // Spouse address
    bool selected;
    uint8_t context[64]; // [New] Memory context around bitAddr
  };
  static std::vector<SpouseEntry> s_spouseList;
  static std::recursive_mutex s_spouseMutex;

  static bool UnsafeRead8(uintptr_t addr, uint8_t *out) {
    __try {
      *out = *(uint8_t *)addr;
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }
  static bool UnsafeRead16(uintptr_t addr, unsigned short *out) {
    __try {
      *out = *(unsigned short *)addr;
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }
  static bool UnsafeRead32(uintptr_t addr, uint32_t *out) {
    __try {
      *out = *(uint32_t *)addr;
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }
  static bool UnsafeReadPtr(uintptr_t addr, uintptr_t *out) {
    __try {
      *out = *(uintptr_t *)addr;
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }
  static bool UnsafeReadMem(uintptr_t addr, void *buf, size_t size) {
    __try {
      memcpy(buf, (void *)addr, size);
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }

  void StartSpouseScannerAsync() {
    if (s_isSpouseScanning)
      return;
    if (g_savedHeroAddr <= 0x10000) {
      AddLog(u8"[배우자 검색] 주인공 주소가 유효하지 않습니다.");
      return;
    }

    s_isSpouseScanning = true;
    s_spouseScanProgress = 0.0f;
    {
      std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
      s_spouseList.clear();
    }

    std::thread([heroAddr = g_savedHeroAddr]() {
      MEMORY_BASIC_INFORMATION mbi;
      std::vector<MEMORY_BASIC_INFORMATION> regions;
      uintptr_t addr = 0;
      unsigned long long totalSize = 0;

      while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
          regions.push_back(mbi);
          totalSize += mbi.RegionSize;
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      }

      unsigned long long processedSize = 0;
      for (const auto &region : regions) {
        uintptr_t start = (uintptr_t)region.BaseAddress;
        uintptr_t end = start + region.RegionSize;

        // 8바이트 정렬된 메모리 영역을 순회하며 heroAddr를 찾음
        const size_t bufferSize = 4096 * 16;
        std::vector<unsigned char> buffer(bufferSize + 8);

        uintptr_t curr = start;
        while (curr < end) {
          size_t remaining = (size_t)(end - curr);
          size_t toRead = (std::min)(remaining, bufferSize); // 매크로 충돌 방지

          if (toRead < 8)
            break;

          // 안전하게 메모리 읽기 (SEH 처리)
          bool readSuccess = UnsafeReadMem(curr, buffer.data(), toRead);

          if (readSuccess) {
            for (size_t i = 0; i <= toRead - 8; i++) {
              uintptr_t *pPtr = (uintptr_t *)(buffer.data() + i);
              if (*pPtr == heroAddr) {
                uintptr_t hitAddr = curr + i;

                // 배우자 플래그 검사 (-8 위치)
                if (hitAddr >= 8) {
                  uint8_t flag = 0;
                  bool flagSuccess = UnsafeRead8(hitAddr - 8, &flag);

                  if (flagSuccess && flag == 2) {
                    // 배우자 포인터 얻기 (+8 위치)
                    uintptr_t spousePtr = 0;
                    bool ptrSuccess = UnsafeReadPtr(hitAddr + 8, &spousePtr);

                    if (ptrSuccess && spousePtr > 0x10000) {
                      // 유효한 무장인지 아이디 확인
                      unsigned short spouseID = 0;
                      bool idSuccess = UnsafeRead16(spousePtr + 0x08, &spouseID);

                      if (idSuccess && spouseID >= 1 && spouseID <= 5102) {
                        // 성별 확인: 여자(1)만 리스트업
                        uint8_t gender = 0;
                        if (UnsafeRead8(spousePtr + 0x30, &gender) && gender == 1) {
                          std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
                          // 중복 검사
                          bool exists = false;
                          for (const auto &e : s_spouseList) {
                            if (e.targetBase == spousePtr) {
                              exists = true;
                              break;
                            }
                          }
                          if (!exists) {
                            SpouseEntry entry;
                            entry.pointerAddr = hitAddr;
                            entry.targetBase = spousePtr;
                            entry.selected = false;

                            // 주변 메모리 64바이트 덤프 (hitAddr - 24 ~ +40)
                            memset(entry.context, 0, 64);
                            UnsafeReadMem(hitAddr - 24, entry.context, 64);

                            s_spouseList.push_back(entry);
                            AddLog(u8"[배우자 검색] 여성 배우자 발견! 주소: %p (ID: %d)", (void *)spousePtr, spouseID);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }

          processedSize += toRead;
          curr += toRead;
          s_spouseScanProgress = (float)processedSize / (float)totalSize;
        }
      }

      s_isSpouseScanning = false;
      AddLog(u8"[배우자 검색] 스캔 완료.");
    }).detach();
  }

  void DrawSpouseListWindow(float scale) {
    static bool s_wasSpouseListWinOpen = false;
    if (!bShowSpouseListWin) {
      if (s_wasSpouseListWinOpen) {
        s_wasSpouseListWinOpen = false;
        std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
        s_spouseList.clear();
        s_spouseList.shrink_to_fit();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        AddLog(u8"[SYSTEM] 배우자 창 종료: 워킹셋(메모리) 강제 반환 완료.");
      }
      return;
    }
    s_wasSpouseListWinOpen = true;

    ImGui::SetNextWindowSize(ImVec2(400 * scale, 500 * scale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"주인공 배우자 목록", &bShowSpouseListWin)) {
      if (s_isSpouseScanning) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"배우자 정보를 메모리에서 스캔 중입니다...");
        ImGui::ProgressBar(s_spouseScanProgress, ImVec2(-1, 0));
      } else {
        std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
        if (s_spouseList.empty()) {
          ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), u8"조회된 배우자가 없습니다.");
          ImGui::TextDisabled(u8"주인공이 미혼이거나 게임 메모리 구조가 다를 수 있습니다.");
        } else {
          ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), u8"총 %d 명의 배우자를 찾았습니다.",
                             (int)s_spouseList.size());
          ImGui::Separator();

          ImGui::TextDisabled(u8"주소/ID 클릭 시 복사, 컬럼 경계 드래그로 너비 조절");
          if (ImGui::BeginTable("SpouseTable", 5,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                    ImGuiTableFlags_Resizable,
                                ImVec2(0, 300 * scale))) {
            ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 30 * scale);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150 * scale);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50 * scale);
            ImGui::TableSetupColumn(u8"슬롯", ImGuiTableColumnFlags_WidthFixed, 50 * scale);
            ImGui::TableSetupColumn(u8"이름", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < s_spouseList.size(); i++) {
              auto &entry = s_spouseList[i];
              uintptr_t relationAddr = entry.pointerAddr;
              uintptr_t spouseAddr = entry.targetBase;
              unsigned short officerID = 0;
              uint32_t slotNumber = 0;
              bool hasSlotNumber = UnsafeRead32(relationAddr + 0x28, &slotNumber);
              if (!UnsafeRead16(spouseAddr + 0x08, &officerID))
                continue;

              ImGui::TableNextRow();

              ImGui::TableSetColumnIndex(0);
              ImGui::PushID((int)i);
              ImGui::Checkbox("##sel", &entry.selected);
              ImGui::PopID();

              ImGui::TableSetColumnIndex(1);
              char addrText[32];
              snprintf(addrText, sizeof(addrText), "%p", (void *)relationAddr);
              char addrLabel[48];
              snprintf(addrLabel, sizeof(addrLabel), "%s##addr_%d", addrText, (int)i);
              if (ImGui::Selectable(addrLabel, false)) {
                ImGui::SetClipboardText(addrText);
                AddLog(u8"[복사] 관계 포인터 주소 복사: %s", addrText);
              }
              if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip(u8"Hex View 기준 주소 (클릭하면 복사)");
              }

              ImGui::TableSetColumnIndex(2);
              char idText[16];
              snprintf(idText, sizeof(idText), "%d", officerID);
              char idLabel[32];
              snprintf(idLabel, sizeof(idLabel), "%s##id_%d", idText, (int)i);
              if (ImGui::Selectable(idLabel, false)) {
                ImGui::SetClipboardText(idText);
                AddLog(u8"[복사] 배우자 ID 복사: %s", idText);
              }
              if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip(u8"클릭하면 ID 복사");
              }

              ImGui::TableSetColumnIndex(3);
              if (hasSlotNumber) {
                ImGui::Text("%u", slotNumber);
              } else {
                ImGui::TextUnformatted("-");
              }

              ImGui::TableSetColumnIndex(4);
              std::string name = u8"알 수 없음";
              if (g_officerNames.count(officerID)) {
                name = g_officerNames[officerID];
              }

              char label[128];
              snprintf(label, sizeof(label), "%s##%p", name.c_str(), (void *)spouseAddr);
              if (ImGui::Selectable(label)) {
                bShowSelectedOfficerWin = true;
                g_capturedOfficerBase = spouseAddr;
                bForceCenterSelectedOfficer = true;
              }

              // [New] 메모리 분석 섹션
              ImGui::PushID((int)(i + 1000));
              if (ImGui::CollapsingHeader(u8"메모리 분석 (Hex View)")) {
                ImGui::BeginChild("HexChild", ImVec2(0, 150 * scale), true);
                ImGui::Text(u8"기준 주소(관계 포인터): %p", (void *)relationAddr);
                ImGui::Text(u8"배우자 주소: %p", (void *)spouseAddr);
                ImGui::Separator();

                for (int row = 0; row < 4; row++) {
                  int offset = (row * 16) - 24;
                  ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Offset %s%02X: ", (offset >= 0 ? "+" : "-"),
                                     abs(offset));
                  ImGui::SameLine();

                  for (int col = 0; col < 16; col++) {
                    int idx = row * 16 + col;
                    uint8_t val = entry.context[idx];

                    // 중요 데이터 색상 강조
                    ImVec4 color = ImVec4(1, 1, 1, 1);
                    if (idx >= 24 && idx < 32)
                      color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // Hero Ptr (P1)
                    else if (idx >= 32 && idx < 40)
                      color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f); // Spouse Ptr
                    else if (idx >= 16 && idx < 24)
                      color = ImVec4(0.4f, 0.4f, 1.0f, 1.0f); // Flag (02)

                    ImGui::TextColored(color, "%02X", val);
                    if (col < 15)
                      ImGui::SameLine();
                  }
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"녹색: 주인공 주소");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), u8" 주황: 배우자 주소");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), u8" 파랑: 플래그(02)");

                ImGui::EndChild();
              }
              ImGui::PopID();
            }
            ImGui::EndTable();
          }

          ImGui::Spacing();
          int selectedCount = 0;
          std::vector<int> selIndices;
          for (size_t i = 0; i < s_spouseList.size(); i++) {
            if (s_spouseList[i].selected) {
              selectedCount++;
              selIndices.push_back((int)i);
            }
          }

          if (selectedCount == 2) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
            if (ImGui::Button(u8"체크된 배우자 둘 맞바꾸기", ImVec2(-1, 30 * scale))) {
              auto &e1 = s_spouseList[selIndices[0]];
              auto &e2 = s_spouseList[selIndices[1]];

              DWORD old1, old2;
              bool s1 = VirtualProtect((LPVOID)e1.pointerAddr, 8, PAGE_READWRITE, &old1);
              bool s2 = VirtualProtect((LPVOID)e2.pointerAddr, 8, PAGE_READWRITE, &old2);

              if (s1 && s2) {
                *(uintptr_t *)e1.pointerAddr = e2.targetBase;
                *(uintptr_t *)e2.pointerAddr = e1.targetBase;

                VirtualProtect((LPVOID)e1.pointerAddr, 8, old1, &old1);
                VirtualProtect((LPVOID)e2.pointerAddr, 8, old2, &old2);

                AddLog(u8"[배우자 검색] 두 배우자 순서를 교환했습니다!");
                StartSpouseScannerAsync(); // 갱신
              } else {
                AddLog(u8"[ERROR] 배우자 순서 교환 실패 (메모리 접근 오류).");
              }
            }
            ImGui::PopStyleColor();
          } else {
            ImGui::BeginDisabled();
            ImGui::Button(u8"체크된 배우자 둘 맞바꾸기 (2명 선택 필요)", ImVec2(-1, 30 * scale));
            ImGui::EndDisabled();
          }
        }

        ImGui::Spacing();
        if (ImGui::Button(u8"다시 스캔", ImVec2(-1, 30 * scale))) {
          StartSpouseScannerAsync();
        }
      }
    }
    ImGui::End();
  }

} // namespace DX11Base