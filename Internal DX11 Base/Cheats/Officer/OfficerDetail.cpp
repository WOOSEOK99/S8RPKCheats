#include "../../pch.h"
#include <string>
#include <vector>
#include <mutex>
#include <iomanip>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include "OfficerDetail.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "OfficerData.h"
#include "SelectOfficercapture.h"
#include "CustomTraitDisplay.h"
#include "../Civilian/CityData.h"
#include "../../Framework/imgui.h"
#include "../../showlog.h"
#include "../System/SkillCountManager.h"

extern ImGuiWindowFlags Flags;

namespace DX11Base {
  bool bShowOfficerDetail = false;
  extern bool bForceCenterOfficerDetail;

  // --- [성능 최적화용 캐시 & 스냅샷] ---
  static uintptr_t s_cachedExeBase = 0;
  static uintptr_t s_cachedCityArrayBase = 0;
  static std::string s_cachedTitleCity = "";
  static uintptr_t s_lastProcessedP1 = 0;
  static ULONGLONG s_lastCityUpdateMs = 0;
  
  static unsigned char s_officerSnapshot[0x3D0] = { 0 };
  static uintptr_t s_lastCapturedAddress = 0;
  static bool s_hasSnapshot = false;

  // --- [변수 선언부] ---
  static int v_Lead = 0, v_War = 0, v_Intel = 0, v_Pol = 0, v_Cha = 0;
  static int v_Rep = 0, v_RepM = 0, v_Notoriety = 0;
  static int v_Gold = 0, v_Action = 0, v_Contr = 0, v_ForceColor = 0;

  // 전법 변수
  static int v_S_Inf1 = 0, v_S_Inf2 = 0, v_S_Inf3 = 0, v_S_Inf4 = 0, v_S_Inf5 = 0;
  static int v_S_Cav1 = 0, v_S_Cav2 = 0, v_S_Cav3 = 0, v_S_Cav4 = 0, v_S_Cav5 = 0;
  static int v_S_Bow1 = 0, v_S_Bow2 = 0, v_S_Bow3 = 0, v_S_Bow4 = 0, v_S_Bow5 = 0;
  static int v_S_Shp1 = 0, v_S_Shp2 = 0, v_S_Shp3 = 0, v_S_Shp4 = 0, v_S_Shp5 = 0;
  static int v_S_Str1 = 0, v_S_Str2 = 0, v_S_Str3 = 0, v_S_Str4 = 0, v_S_Str5 = 0;
  static int v_S_Sup1 = 0, v_S_Sup2 = 0, v_S_Sup3 = 0, v_S_Sup4 = 0, v_S_Sup5 = 0;
  static int v_S_Mag1 = 0, v_S_Mag2 = 0, v_S_Mag3 = 0, v_S_Mag4 = 0, v_S_Mag5 = 0;
  static int v_S_Wep1 = 0, v_S_Wep2 = 0, v_S_Wep3 = 0, v_S_Wep4 = 0, v_S_Wep5 = 0;

  // 특기 변수
  static int v_A_M1 = 0, v_A_M2 = 0, v_A_M3 = 0, v_A_M4 = 0, v_A_M5 = 0, v_A_M6 = 0;
  static int v_A_I1 = 0, v_A_I2 = 0, v_A_I3 = 0, v_A_I4 = 0, v_A_I5 = 0, v_A_I6 = 0;
  static int v_A_U1 = 0, v_A_U2 = 0, v_A_U3 = 0, v_A_U4 = 0, v_A_U5 = 0, v_A_U6 = 0;
  static int v_A_W1 = 0, v_A_W2 = 0, v_A_W3 = 0, v_A_W4 = 0, v_A_W5 = 0, v_A_W6 = 0;
  static int v_A_C1 = 0, v_A_C2 = 0, v_A_C3 = 0, v_A_C4 = 0, v_A_C5 = 0, v_A_C6 = 0;
  static int v_A_D1 = 0, v_A_D2 = 0, v_A_D3 = 0, v_A_D4 = 0, v_A_D5 = 0, v_A_D6 = 0;

  // 소양 변수
  static int v_Exp_Inf = 0, v_Exp_Cav = 0, v_Exp_Arch = 0, v_Exp_Nav = 0, v_Exp_Str = 0, v_Exp_Sup = 0, v_Exp_Dis = 0;
  static int v_Exp_Task = 0, v_Exp_Intel = 0, v_Exp_War = 0, v_Exp_Mil = 0;

  // 주인공(Main) 무장 명성/행동력 변수
  static int v_AP = 0, v_Token = 0, v_Loyalty = 0, v_StrPoint = 0;

  // 주인공 외형/특징 변수
  static int v_OfficerID = 0, v_ModelNo = 0, v_ModelColor = 0, v_Appear = 0, v_Birth = 0, v_Death = 0;

  // uintptr_t는 64비트 주소를 담는 표준 타입입니다.
  uintptr_t v_Talent1 = 0;
  uintptr_t v_Talent2 = 0;
  uintptr_t v_Talent3 = 0;

  extern int *pSelectedVar;
  extern std::string currentLabel;

  // 스크립트의 traitsListEnglish 순서와 동일해야 합니다.
  const char *traitNames[] = {
      u8"대덕",     u8"의협",     u8"만인적",   u8"일신시담", u8"금마초",   u8"노당익장", u8"복룡",     u8"봉추",
      u8"기린아",   u8"초세지걸", u8"왕좌",     u8"불요불굴", u8"낭고",     u8"병귀신속", u8"료래료래", u8"금강불괴",
      u8"위서심공", u8"산도강습", u8"강동맹호", u8"소패왕",   u8"용재",     u8"화신",     u8"냉염",     u8"괄목",
      u8"방울감녕", u8"원모심려", u8"천하무쌍", u8"수화폐월", u8"명가위광", u8"악역무도", u8"구심",     u8"황천",
      u8"전장의꽃", u8"호위",     u8"기습병",   u8"간파",     u8"군규",     u8"냉정",     u8"기략",     u8"진법",
      u8"궤계",     u8"맹공",     u8"견수",     u8"불굴",     u8"산전",     u8"삼전",     u8"강창",     u8"조기통제",
      u8"북방마술", u8"질주궁",   u8"수신",     u8"상조교",   u8"재녀",     u8"교화",     u8"호랑지심", u8"부가",
      u8"능리",     u8"근면",     u8"시재",     u8"유심",     u8"경성",     u8"직정",     u8"자만",     u8"반감",
      u8"소심",     u8"조급",     u8"성걸",     u8"이기적",   u8"병약",     u8"거만"};

  // --- [공용 헬퍼 함수군] ---

  // 테이블 헤더 설정 헬퍼
  void SetupTableHeaders(float scale) {
    ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 90.0f * scale);
    ImGui::TableSetupColumn(u8"편집", ImGuiTableColumnFlags_WidthFixed, 140.0f * scale);
    ImGui::TableHeadersRow();
  }

  // --- [공용 헬퍼 함수 1: 수치 행 그리기] ---
  static inline void SyncInlineReadBufFromWrite(uintptr_t pWrite) {
    if (!IsValidPtr(pWrite, 0x3D0))
      return;

    // 1. 소양/능력 팝업용 로컬 스냅샷 동기화
    if (s_lastCapturedAddress != 0 && s_lastCapturedAddress == pWrite) {
      memcpy(s_officerSnapshot, (void *)pWrite, 0x3D0);
    }

    // 2. 모든 무장 리스트용 전역 스냅샷 동기화
    if (g_officerInlineReadPtr > 0x10000 && pWrite == g_capturedOfficerBase) {
      memcpy((void *)g_officerInlineReadPtr, (void *)pWrite, 0x3D0);
    }
  }

  // --- [추가] 정적 스냅샷 갱신 헬퍼 ---
  static void UpdateOfficerSnapshot(uintptr_t p1) {
    if (p1 != 0 && IsValidPtr(p1, 0x3D0)) {
      memcpy(s_officerSnapshot, (void *)p1, 0x3D0);
      s_lastCapturedAddress  = p1;
      s_hasSnapshot          = true;
      g_officerInlineReadPtr = (uintptr_t)s_officerSnapshot;
    } else {
      s_hasSnapshot          = false;
    }
  }

  void RenderStatRow(uintptr_t p1, const char *label, uintptr_t offset, int size, int *inputVal, float scale) {
    // [수정] 목록 창의 전역 스냅샷(g_officerInlineReadPtr) 또는 로컬 창 스냅샷(s_officerSnapshot) 중 적절한 리드 버퍼 선택
    uintptr_t pR = 0;
    if (g_officerInlineReadPtr > 0x10000 && p1 == g_capturedOfficerBase)
      pR = g_officerInlineReadPtr;
    else if (s_hasSnapshot && p1 == s_lastCapturedAddress)
      pR = (uintptr_t)s_officerSnapshot;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
 
    ImGui::TableNextColumn();
    ImGui::PushID(label);
    
    // 1. [-] 버튼
    if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
      (*inputVal)--;
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
      SyncInlineReadBufFromWrite(p1);
      // [다중 선택] 체크된 모든 무장에 동일 값 적용
      if (GetSelectedOfficerIDCount() > 0) {
        const int applyVal = *inputVal;
        ApplyPatchToSelectedOfficers([offset, applyVal, size](uintptr_t base) {
          DX11Base::ModifyStat(base, offset, applyVal, size);
        });
      }
    }
    ImGui::SameLine();
 
    // 2. 직접 입력 가능한 수치 박스 (InputInt)
    ImGui::SetNextItemWidth(70 * scale);
    ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);
    
    bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
    if (justFinished) {
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
      SyncInlineReadBufFromWrite(p1);
      // [다중 선택] 체크된 모든 무장에 동일 값 적용
      if (GetSelectedOfficerIDCount() > 0) {
        const int applyVal = *inputVal;
        ApplyPatchToSelectedOfficers([offset, applyVal, size](uintptr_t base) {
          DX11Base::ModifyStat(base, offset, applyVal, size);
        });
      }
    }
    
    // [중요] 사용자가 입력 중이 아닐 때만 적절한 스냅샷 버퍼(pR)에서 값을 가져와 표시
    if (!ImGui::IsItemActive() && !justFinished && pR > 0x10000) {
      if (size == 1)
        *inputVal = (int)(*(unsigned char *)(pR + offset));
      else if (size == 2)
        *inputVal = (int)(*(unsigned short *)(pR + offset));
      else
        *inputVal = (int)(*(unsigned int *)(pR + offset));
    }
  
    ImGui::SameLine();

    if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
      (*inputVal)++;
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
      SyncInlineReadBufFromWrite(p1);
      // [다중 선택] 체크된 모든 무장에 동일 값 적용
      if (GetSelectedOfficerIDCount() > 0) {
        const int applyVal = *inputVal;
        ApplyPatchToSelectedOfficers([offset, applyVal, size](uintptr_t base) {
          DX11Base::ModifyStat(base, offset, applyVal, size);
        });
      }
    }
    ImGui::PopID();
  }

  // --- [공용 헬퍼 함수 2: 연구 트리용 콤팩트] ---
  void RenderCompactSkill(uintptr_t p1, const char *label, uintptr_t offset, int *val, float scale) {
    uintptr_t pR = 0;
    if (g_officerInlineReadPtr > 0x10000 && p1 == g_capturedOfficerBase)
      pR = g_officerInlineReadPtr;
    else if (s_hasSnapshot && p1 == s_lastCapturedAddress)
      pR = (uintptr_t)s_officerSnapshot;

    if (pR > 0x10000)
      *val = (int)(*(unsigned char *)(pR + offset));

    // 무장 ID 가져오기 (0x139 등의 오프셋은 무장별 전법 데이터)
    int officerID = 0;
    if (pR > 0x10000) {
        officerID = (int)(*(unsigned short*)(pR + 0x08));
    }

    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 3);

    // --- [ 왼쪽: 레벨 버튼 ] ---
    // 병기 전법(0x15C~0x160)만 레벨 버튼을 숨기고, 나머지는(일반 전법 & 특기) 표시합니다.
    bool isWeaponTactic = (offset >= 0x15C && offset <= 0x160);
    if (!isWeaponTactic) {
        bool hasCustomColor = false;
        if (*val == 1) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
            hasCustomColor = true;
        } else if (*val == 2) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
            hasCustomColor = true;
        } else if (*val == 3) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
            hasCustomColor = true;
        }
        
        if (hasCustomColor) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }

        char btnLabelLvl[16];
        snprintf(btnLabelLvl, sizeof(btnLabelLvl), "%d##lvl", *val);
        if (ImGui::Button(btnLabelLvl, ImVec2(30 * scale, 25 * scale))) {
            *val = (*val + 1) % 4;
            DX11Base::ModifyStat(p1, offset, *val, 1);
            SyncInlineReadBufFromWrite(p1);
            // [다중 선택] 체크된 모든 무장에 동일 레벨 적용
            if (GetSelectedOfficerIDCount() > 0) {
              const int applyVal = *val;
              ApplyPatchToSelectedOfficers([offset, applyVal](uintptr_t base) {
                DX11Base::ModifyStat(base, offset, applyVal, 1);
              });
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 순환 (0->1->2->3)");
            ImGui::EndTooltip();
        }

        if (hasCustomColor) {
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine(0, 4);
    }

    // --- [ 오른쪽: 횟수 버튼 ] ---
    // 전법(Tactics)일 때만 횟수 버튼 표시 (특기는 횟수 개념이 없음)
    // 전법 오프셋 범위: 0x139 ~ 0x160 (일반 전법 + 병기 전법)
    if (offset >= 0x139 && offset <= 0x160) {
        ImGui::SameLine(0, 4);
        
        int targetCount = GetTargetSkillCount(officerID, offset);
        if (targetCount < 0) targetCount = 0;

        char btnLabelCnt[32];
        if (targetCount == 0) snprintf(btnLabelCnt, sizeof(btnLabelCnt), "-##cnt");
        else snprintf(btnLabelCnt, sizeof(btnLabelCnt), "%d##cnt", targetCount);

        // 아주 연한 회색 (배경과 조화되도록)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
        if (ImGui::Button(btnLabelCnt, ImVec2(30 * scale, 25 * scale))) {
            targetCount = (targetCount + 1) % 10;
            SetTargetSkillCount(officerID, offset, targetCount);
            // [다중 선택] 체크된 모든 무장에 동일 횟수 적용
            if (GetSelectedOfficerIDCount() > 0) {
              const int applyCount = targetCount;
              ApplyPatchToSelectedOfficers([offset, applyCount](uintptr_t base) {
                if (!IsValidPtr(base + 0x08, 2)) return;
                int id = (int)(*(unsigned short*)(base + 0x08));
                SetTargetSkillCount(id, offset, applyCount);
              });
            }
        }
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"전법 사용 횟수 설정");
            ImGui::Text(u8"설정된 값은 전투 시작 시 자동으로 적용됩니다.");
            ImGui::EndTooltip();
        }
    }

    ImGui::PopID();
  }

  // --- [공용 헬퍼 함수 3: 전법/특기 행 그리기] ---
  void RenderResearchRow(uintptr_t pBase, const char *catLabel, const char *items[], uintptr_t offsets[], int *vars[],
                         int count, float scale, int expOffset) {
    if (!items || !offsets || !vars) return;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    if (catLabel && catLabel[0] != '\0') {
        ImGui::Text(u8"%s", catLabel);
    }

    for (int i = 0; i < count; i++) {
        if (!items[i] || !vars[i]) continue;
        ImGui::TableSetColumnIndex(i + 1);
        RenderCompactSkill(pBase, items[i], offsets[i], vars[i], scale);
    }
  }

  // --- [기재 관련 함수군] ---

  static std::unordered_map<uint16_t, uintptr_t> s_traitObjectById;

  static bool TryReadPtr(uintptr_t address, uintptr_t &out) {
    __try {
      out = *reinterpret_cast<uintptr_t *>(address);
      return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      out = 0;
      return false;
    }
  }

  static bool TryReadU16(uintptr_t address, uint16_t &out) {
    __try {
      out = *reinterpret_cast<uint16_t *>(address);
      return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      out = 0;
      return false;
    }
  }

  static bool IsReadablePage(DWORD protect) {
    if (protect & PAGE_GUARD)
      return false;
    const DWORD p = protect & 0xFF;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
  }

  static bool IsBuiltInTraitId(uint16_t id) {
    return (id >= 1 && id <= 70) || id == 201 || id == 202;
  }

  static bool IsConfiguredCustomTraitId(uint16_t id) {
    if (id == 0)
      return false;
    CustomTraitDisplayInfo info;
    return GetCustomTraitDisplayInfo(id, info) && !info.name.empty();
  }

  static bool IsAssignableTraitId(uint16_t id) {
    return IsBuiltInTraitId(id) || IsConfiguredCustomTraitId(id);
  }

  static bool ValidateTraitObject(uintptr_t pTrait, uint16_t expectedId = 0, uintptr_t expectedVtable = 0) {
    if (pTrait < 0x10000)
      return false;

    uintptr_t vtable = 0;
    uint16_t id = 0;
    if (!TryReadPtr(pTrait, vtable) || vtable < 0x10000 || !TryReadU16(pTrait + 0x08, id))
      return false;
    if (expectedId != 0 && id != expectedId)
      return false;
    if (expectedVtable != 0 && vtable != expectedVtable)
      return false;
    return IsAssignableTraitId(id);
  }

  static void CacheTraitObject(uintptr_t pTrait) {
    uint16_t id = 0;
    if (!ValidateTraitObject(pTrait) || !TryReadU16(pTrait + 0x08, id))
      return;
    s_traitObjectById[id] = pTrait;
  }

  uint16_t GetTraitID(uintptr_t officerBase, int slotIndex) {
    if (officerBase < 0x10000 || slotIndex < 0 || slotIndex >= 3)
      return 0;

    uintptr_t pTrait = 0;
    uint16_t id = 0;
    if (!TryReadPtr(officerBase + 0x88 + slotIndex * 0x08, pTrait) ||
        !TryReadU16(pTrait + 0x08, id))
      return 0;

    CacheTraitObject(pTrait);
    return id;
  }

  static void CacheTraitObjectsFromOfficerArray() {
    uintptr_t gameBase = GetGameBase();
    if (!gameBase)
      return;

    uintptr_t officerArr = 0;
    if (!TryReadPtr(gameBase + 0x3B8, officerArr) || officerArr < 0x10000)
      return;

    // 잘못된 한 슬롯 때문에 전체 검색이 중단되지 않도록 슬롯 단위로 안전하게 읽습니다.
    for (int officerIndex = 0; officerIndex < 5102; ++officerIndex) {
      const uintptr_t officerBase = officerArr + static_cast<uintptr_t>(officerIndex) * 0x3D0;
      for (int slot = 0; slot < 3; ++slot) {
        uintptr_t pTrait = 0;
        if (!TryReadPtr(officerBase + 0x88 + slot * 0x08, pTrait))
          continue;
        CacheTraitObject(pTrait);
      }
    }
  }

  static uintptr_t ScanReadableRegionForTrait(uintptr_t begin, size_t size,
                                               uintptr_t vtable, uint16_t traitID) {
    if (begin < 0x10000 || size < 0x10 || vtable < 0x10000)
      return 0;

    const uintptr_t end = begin + size;
    if (end <= begin)
      return 0;

    uintptr_t p = (begin + 7) & ~static_cast<uintptr_t>(7);
    __try {
      for (; p + 0x0A <= end; p += 8) {
        if (*reinterpret_cast<uintptr_t *>(p) != vtable)
          continue;
        if (*reinterpret_cast<uint16_t *>(p + 0x08) == traitID)
          return p;
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      return 0;
    }
    return 0;
  }

  static uintptr_t FindTraitObjectInProcessMemory(uint16_t traitID, uintptr_t vtable) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    uintptr_t address = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    while (address < maxAddress) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
        break;

      const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      if (mbi.State == MEM_COMMIT && IsReadablePage(mbi.Protect)) {
        const uintptr_t found = ScanReadableRegionForTrait(regionBase, mbi.RegionSize, vtable, traitID);
        if (found)
          return found;
      }

      const uintptr_t next = regionBase + mbi.RegionSize;
      if (next <= address)
        break;
      address = next;
    }
    return 0;
  }

  static uintptr_t FindTraitDataPointerByID(uint16_t traitID, uintptr_t sourceOfficer) {
    if (traitID == 0)
      return 0;

    auto cached = s_traitObjectById.find(traitID);
    if (cached != s_traitObjectById.end() && ValidateTraitObject(cached->second, traitID))
      return cached->second;

    // 먼저 현재 무장과 전체 무장 슬롯에서 이미 사용 중인 기재 객체를 수집합니다.
    uintptr_t sourceVtable = 0;
    if (sourceOfficer > 0x10000) {
      for (int slot = 0; slot < 3; ++slot) {
        uintptr_t pTrait = 0;
        if (!TryReadPtr(sourceOfficer + 0x88 + slot * 0x08, pTrait))
          continue;
        CacheTraitObject(pTrait);
        if (!sourceVtable && ValidateTraitObject(pTrait))
          TryReadPtr(pTrait, sourceVtable);
      }
    }

    CacheTraitObjectsFromOfficerArray();

    cached = s_traitObjectById.find(traitID);
    if (cached != s_traitObjectById.end() && ValidateTraitObject(cached->second, traitID)) {
      AddLog(u8"[기재변경] ID:%d 기재 객체를 무장 슬롯 캐시에서 확인", traitID);
      return cached->second;
    }

    // 현재 아무 무장도 사용하지 않는 사용자 기재도 원본 편집기에서는 선택할 수 있습니다.
    // 같은 기재 클래스(vtable)의 객체를 프로세스 메모리에서 찾아 원본 게임 객체를 재사용합니다.
    if (!sourceVtable) {
      for (const auto &entry : s_traitObjectById) {
        if (ValidateTraitObject(entry.second)) {
          TryReadPtr(entry.second, sourceVtable);
          if (sourceVtable)
            break;
        }
      }
    }

    if (sourceVtable) {
      const uintptr_t found = FindTraitObjectInProcessMemory(traitID, sourceVtable);
      if (found && ValidateTraitObject(found, traitID, sourceVtable)) {
        s_traitObjectById[traitID] = found;
        AddLog(u8"[기재변경] ID:%d 기재 객체를 게임 메모리 카탈로그에서 확인", traitID);
        return found;
      }
    }

    return 0;
  }

  bool SetTraitID(uintptr_t officerBase, int slotIndex, uint16_t traitID) {
    if (officerBase < 0x10000 || slotIndex < 0 || slotIndex >= 3 || traitID == 0)
      return false;

    const uintptr_t targetTrait = FindTraitDataPointerByID(traitID, officerBase);
    if (targetTrait < 0x10000) {
      AddLog(u8"[기재변경] ID:%d 기재 객체를 찾지 못했습니다.", traitID);
      return false;
    }

    uintptr_t *slotPtr = reinterpret_cast<uintptr_t *>(officerBase + 0x88 + slotIndex * 0x08);
    DWORD old = 0, tmp = 0;
    if (!VirtualProtect(slotPtr, sizeof(uintptr_t), PAGE_READWRITE, &old))
      return false;

    *slotPtr = targetTrait;
    VirtualProtect(slotPtr, sizeof(uintptr_t), old, &tmp);

    const uint16_t verifyId = GetTraitID(officerBase, slotIndex);
    if (verifyId != traitID) {
      AddLog(u8"[기재변경] 슬롯%d 쓰기 검증 실패: 요청 ID:%d / 읽힘 ID:%d",
             slotIndex + 1, traitID, verifyId);
      return false;
    }

    AddLog(u8"[기재변경] 슬롯%d → ID:%d 변경 및 검증 완료", slotIndex + 1, traitID);
    return true;
  }


  bool SeedTraitObjectsForBatch(
      const std::vector<uint16_t>& traitIDs,
      std::unordered_map<uint16_t, uintptr_t>& outObjects,
      uintptr_t& outTraitVtable) {
    outObjects.clear();
    outTraitVtable = 0;
    if (traitIDs.empty())
      return false;

    auto isWanted = [&traitIDs](uint16_t id) {
      return std::find(traitIDs.begin(), traitIDs.end(), id) != traitIDs.end();
    };

    // 이미 캐시된 객체가 있으면 먼저 재사용합니다.
    for (const auto& entry : s_traitObjectById) {
      if (!ValidateTraitObject(entry.second, entry.first))
        continue;

      if (!outTraitVtable)
        TryReadPtr(entry.second, outTraitVtable);

      if (isWanted(entry.first))
        outObjects[entry.first] = entry.second;
    }

    if (outTraitVtable)
      return true;

    // 캐시가 비어 있다면 전체 무장 배열을 끝까지 훑지 않고,
    // 첫 정상 기재 객체 하나만 찾아 vtable 기준값으로 사용합니다.
    uintptr_t gameBase = GetGameBase();
    if (!gameBase)
      return false;

    uintptr_t officerArr = 0;
    if (!TryReadPtr(gameBase + 0x3B8, officerArr) || officerArr < 0x10000)
      return false;

    for (int officerIndex = 0; officerIndex < 5102; ++officerIndex) {
      const uintptr_t officerBase =
          officerArr + static_cast<uintptr_t>(officerIndex) * 0x3D0;

      for (int slot = 0; slot < 3; ++slot) {
        uintptr_t pTrait = 0;
        if (!TryReadPtr(officerBase + 0x88 + slot * 0x08, pTrait))
          continue;
        if (!ValidateTraitObject(pTrait))
          continue;

        uint16_t id = 0;
        if (!TryReadU16(pTrait + 0x08, id))
          continue;

        CacheTraitObject(pTrait);
        TryReadPtr(pTrait, outTraitVtable);
        if (isWanted(id))
          outObjects[id] = pTrait;

        return outTraitVtable != 0;
      }
    }

    return false;
  }

  bool ScanTraitObjectsForBatchStep(
      const std::vector<uint16_t>& traitIDs,
      std::unordered_map<uint16_t, uintptr_t>& outObjects,
      uintptr_t traitVtable,
      uintptr_t& scanAddress,
      size_t maxReadableBytes,
      bool& finished) {
    finished = false;
    if (traitIDs.empty() || !traitVtable) {
      finished = true;
      return false;
    }

    if (outObjects.size() >= traitIDs.size()) {
      finished = true;
      return true;
    }

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t minAddress =
        reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maxAddress =
        reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    if (scanAddress < minAddress)
      scanAddress = minAddress;
    if (scanAddress >= maxAddress) {
      finished = true;
      return !outObjects.empty();
    }

    size_t scannedReadable = 0;

    auto isWanted = [&traitIDs](uint16_t id) {
      return std::find(traitIDs.begin(), traitIDs.end(), id) != traitIDs.end();
    };

    while (scanAddress < maxAddress && scannedReadable < maxReadableBytes) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery(reinterpret_cast<LPCVOID>(scanAddress), &mbi, sizeof(mbi)) == 0) {
        finished = true;
        break;
      }

      const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      const uintptr_t regionEnd = regionBase + mbi.RegionSize;
      if (regionEnd <= scanAddress) {
        finished = true;
        break;
      }

      if (mbi.State != MEM_COMMIT || !IsReadablePage(mbi.Protect) || mbi.RegionSize < 0x10) {
        scanAddress = regionEnd;
        continue;
      }

      uintptr_t begin = scanAddress > regionBase ? scanAddress : regionBase;
      begin = (begin + 7) & ~static_cast<uintptr_t>(7);

      size_t budgetLeft = maxReadableBytes - scannedReadable;
      uintptr_t budgetEnd = begin + budgetLeft;
      if (budgetEnd < begin || budgetEnd > regionEnd)
        budgetEnd = regionEnd;

      for (uintptr_t p = begin; p + 0x0A <= budgetEnd; p += 8) {
        uintptr_t vtable = 0;
        if (!TryReadPtr(p, vtable) || vtable != traitVtable)
          continue;

        uint16_t id = 0;
        if (!TryReadU16(p + 0x08, id))
          continue;
        if (!isWanted(id) || outObjects.count(id) != 0)
          continue;

        if (ValidateTraitObject(p, id, traitVtable)) {
          s_traitObjectById[id] = p;
          outObjects[id] = p;
          if (outObjects.size() >= traitIDs.size()) {
            finished = true;
            scanAddress = p + 8;
            return true;
          }
        }
      }

      const size_t consumed =
          budgetEnd > begin ? static_cast<size_t>(budgetEnd - begin) : 0;
      scannedReadable += consumed;

      if (budgetEnd < regionEnd) {
        scanAddress = budgetEnd;
        break;
      }

      scanAddress = regionEnd;
    }

    if (scanAddress >= maxAddress)
      finished = true;

    return !outObjects.empty();
  }

  bool SetTraitObjectFast(
      uintptr_t officerBase, int slotIndex, uint16_t traitID, uintptr_t traitObject) {
    if (officerBase < 0x10000 || slotIndex < 0 || slotIndex >= 3 ||
        traitID == 0 || traitObject < 0x10000)
      return false;

    if (!ValidateTraitObject(traitObject, traitID))
      return false;

    uintptr_t* slotPtr =
        reinterpret_cast<uintptr_t*>(officerBase + 0x88 + slotIndex * 0x08);

    __try {
      *slotPtr = traitObject;
      return *slotPtr == traitObject;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }

  uintptr_t GetSelectedOfficerBase() {
    uintptr_t heroBase = g_capturedOfficerBase;
    if (heroBase < 0x10000)
      return 0;
    uint16_t selectedID = *(uint16_t *)(heroBase + 0x8);
    if (selectedID == 0)
      return 0;
    uintptr_t gameBase = GetGameBase();
    if (!gameBase)
      return 0;
    uintptr_t officerArr = *(uintptr_t *)(gameBase + 0x3B8);
    if (officerArr < 0x10000)
      return 0;
    return officerArr + (selectedID - 1) * 0x3D0;
  }

  // --- [공용 탭 렌더링 함수군] ---

  void RenderBasicTab(uintptr_t pBase, float scale, bool isCaptured) {
    static ImGuiTableFlags tflags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("BasicStatTable", 2, tflags)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 100.0f * scale);
      ImGui::TableSetupColumn(u8"편집", ImGuiTableColumnFlags_WidthStretch);

      if (isCaptured) {
        // [선택 무장 전용 오프셋]
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 1.0f, 1.0f));
        ImGui::Selectable(u8" [ 능력 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"통솔", 0xAA, 1, &v_Lead, scale);
        RenderStatRow(pBase, u8"무력", 0xAB, 1, &v_War, scale);
        RenderStatRow(pBase, u8"지력", 0xAC, 1, &v_Intel, scale);
        RenderStatRow(pBase, u8"정치", 0xAD, 1, &v_Pol, scale);
        RenderStatRow(pBase, u8"매력", 0xAE, 1, &v_Cha, scale);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
        ImGui::Selectable(u8" [ 명성 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"문명", 0x104, 2, &v_Rep, scale);
        RenderStatRow(pBase, u8"무명", 0x106, 2, &v_RepM, scale);
        RenderStatRow(pBase, u8"악명", 0x108, 2, &v_Notoriety, scale);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.6f, 0.1f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        ImGui::Selectable(u8" [ 기타 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"공적", 0x100, 2, &v_Contr, scale);
        RenderStatRow(pBase, u8"봉록", 0xE8, 4, &v_Gold, scale); // [수정] 4바이트 적용
        RenderStatRow(pBase, u8"충성", 0xEC, 1, &v_Loyalty, scale);
        RenderStatRow(pBase, u8"전략포인트", 0xED, 1, &v_StrPoint, scale);
        RenderStatRow(pBase, u8"행동력", 0xEE, 1, &v_Action, scale);
      } else {
        // [주인공 무장 전용]
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 1.0f, 1.0f));
        ImGui::Selectable(u8" [ 능력 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"통솔", 0xAA, 1, &v_Lead, scale);
        RenderStatRow(pBase, u8"무력", 0xAB, 1, &v_War, scale);
        RenderStatRow(pBase, u8"지력", 0xAC, 1, &v_Intel, scale);
        RenderStatRow(pBase, u8"정치", 0xAD, 1, &v_Pol, scale);
        RenderStatRow(pBase, u8"매력", 0xAE, 1, &v_Cha, scale);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
        ImGui::Selectable(u8" [ 명성 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"문명", 0x104, 2, &v_Rep, scale);
        RenderStatRow(pBase, u8"무명", 0x106, 2, &v_RepM, scale);
        RenderStatRow(pBase, u8"악명", 0x108, 2, &v_Notoriety, scale);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.6f, 0.1f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        ImGui::Selectable(u8" [ 기타 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"공적", 0x100, 2, &v_Contr, scale);
        RenderStatRow(pBase, u8"봉록", 0xE8, 4, &v_Gold, scale); // [수정] 4바이트 적용
        RenderStatRow(pBase, u8"충성", 0xEC, 1, &v_Loyalty, scale);
        RenderStatRow(pBase, u8"전략포인트", 0xED, 1, &v_StrPoint, scale);
        RenderStatRow(pBase, u8"행동력", 0xEE, 1, &v_Action, scale);

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
      }
      ImGui::EndTable();
    }
  }

  void RenderResearchTab(uintptr_t pBase, float scale) {
    // --- [ 1. 전법 (Tactics) 테이블 ] ---
    static ImGuiTableFlags tflags_tac = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("ResearchTacticsTable", 6, tflags_tac)) {
      ImGui::TableSetupColumn(u8"분류", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
      for (int i = 0; i < 5; i++)
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 1.0f, 1.0f));
      ImGui::Selectable(u8" [ 전법 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);

      {
        const char *s[] = {u8"강격", u8"난격", u8"교란", u8"맹돌", u8"창금"};
        uintptr_t o[] = {0x139, 0x13A, 0x13B, 0x13C, 0x13D};
        int *v[] = {&v_S_Inf1, &v_S_Inf2, &v_S_Inf3, &v_S_Inf4, &v_S_Inf5};
        RenderResearchRow(pBase, u8"보병", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"연격", u8"돌격", u8"급습", u8"기사", u8"차현"};
        uintptr_t o[] = {0x13E, 0x13F, 0x140, 0x141, 0x142};
        int *v[] = {&v_S_Cav1, &v_S_Cav2, &v_S_Cav3, &v_S_Cav4, &v_S_Cav5};
        RenderResearchRow(pBase, u8"기병", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"제사", u8"난사", u8"화시", u8"원사", u8"시람"};
        uintptr_t o[] = {0x143, 0x144, 0x145, 0x146, 0x147};
        int *v[] = {&v_S_Bow1, &v_S_Bow2, &v_S_Bow3, &v_S_Bow4, &v_S_Bow5};
        RenderResearchRow(pBase, u8"궁병", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"화전", u8"난전", u8"연환", u8"돌관", u8"폭선"};
        uintptr_t o[] = {0x148, 0x149, 0x14A, 0x14B, 0x14C};
        int *v[] = {&v_S_Shp1, &v_S_Shp2, &v_S_Shp3, &v_S_Shp4, &v_S_Shp5};
        RenderResearchRow(pBase, u8"함선", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"열화", u8"격류", u8"낙석", u8"요격", u8"동토"};
        uintptr_t o[] = {0x14D, 0x14E, 0x14F, 0x150, 0x151};
        int *v[] = {&v_S_Str1, &v_S_Str2, &v_S_Str3, &v_S_Str4, &v_S_Str5};
        RenderResearchRow(pBase, u8"군략", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"분기", u8"고무", u8"매성", u8"치료", u8"천계"};
        uintptr_t o[] = {0x152, 0x153, 0x154, 0x155, 0x156};
        int *v[] = {&v_S_Sup1, &v_S_Sup2, &v_S_Sup3, &v_S_Sup4, &v_S_Sup5};
        RenderResearchRow(pBase, u8"보조", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"풍변", u8"천변", u8"요술", u8"환술", u8"낙뢰"};
        uintptr_t o[] = {0x157, 0x158, 0x159, 0x15A, 0x15B};
        int *v[] = {&v_S_Mag1, &v_S_Mag2, &v_S_Mag3, &v_S_Mag4, &v_S_Mag5};
        RenderResearchRow(pBase, u8"둔갑", s, o, v, 5, scale);
      }
      {
        const char *s[] = {u8"충차", u8"정란", u8"투석", u8"운제", u8"상병"};
        uintptr_t o[] = {0x15C, 0x15D, 0x15E, 0x15F, 0x160};
        int *v[] = {&v_S_Wep1, &v_S_Wep2, &v_S_Wep3, &v_S_Wep4, &v_S_Wep5};
        RenderResearchRow(pBase, u8"병기", s, o, v, 5, scale);
      }
      ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- [ 2. 특기 (Trait) 테이블 ] ---
    static ImGuiTableFlags tflags_tra = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("ResearchTraitTable", 7, tflags_tra)) {
      ImGui::TableSetupColumn(u8"분류", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
      for (int i = 0; i < 6; i++)
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
      ImGui::Selectable(u8" [ 특기 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);

      {
        const char *s[] = {u8"경작", u8"상재", u8"축성", u8"경비", u8"발명", u8"천성"};
        uintptr_t o[] = {0x1D0, 0x1D1, 0x1D2, 0x1D3, 0x1D4, 0x1D5};
        int *v[] = {&v_A_U1, &v_A_U2, &v_A_U3, &v_A_U4, &v_A_U5, &v_A_U6};
        RenderResearchRow(pBase, u8"임무", s, o, v, 6, scale, 0xCB);
      }
      {
        const char *s[] = {u8"교섭", u8"허보", u8"공작", u8"화술", u8"열변", u8"귀모"};
        uintptr_t o[] = {0x1D6, 0x1D7, 0x1D8, 0x1D9, 0x1DA, 0x1DB};
        int *v[] = {&v_A_D1, &v_A_D2, &v_A_D3, &v_A_D4, &v_A_D5, &v_A_D6};
        RenderResearchRow(pBase, u8"지모", s, o, v, 6, scale, 0xCC);
      }
      {
        const char *s[] = {u8"보장", u8"기장", u8"궁장", u8"수군", u8"조기", u8"신산"};
        uintptr_t o[] = {0x1DC, 0x1DD, 0x1DE, 0x1DF, 0x1E0, 0x1E1};
        int *v[] = {&v_A_C1, &v_A_C2, &v_A_C3, &v_A_C4, &v_A_C5, &v_A_C6};
        RenderResearchRow(pBase, u8"병과", s, o, v, 6, scale, 0xCD);
      }
      {
        const char *s[] = {u8"원호", u8"파성", u8"행군", u8"여력", u8"과감", u8"위풍"};
        uintptr_t o[] = {0x1E2, 0x1E3, 0x1E4, 0x1E5, 0x1E6, 0x1E7};
        int *v[] = {&v_A_W1, &v_A_W2, &v_A_W3, &v_A_W4, &v_A_W5, &v_A_W6};
        RenderResearchRow(pBase, u8"군사", s, o, v, 6, scale, 0xCE);
      }
      ImGui::EndTable();
    }
  }

  void RenderExpTab(uintptr_t pBase, float scale) {
    static ImGuiTableFlags tflags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("ExpTable", 2, tflags)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 100.0f * scale);
      ImGui::TableSetupColumn(u8"편집", ImGuiTableColumnFlags_WidthStretch);

      // --- [ 전법 ] ---
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 1.0f, 1.0f));
      ImGui::Selectable(u8" [ 전법 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);

      RenderStatRow(pBase, u8"보병소양", 0xC4, 1, &v_Exp_Inf, scale);
      RenderStatRow(pBase, u8"기병소양", 0xC5, 1, &v_Exp_Cav, scale);
      RenderStatRow(pBase, u8"궁병소양", 0xC6, 1, &v_Exp_Arch, scale);
      RenderStatRow(pBase, u8"함선소양", 0xC7, 1, &v_Exp_Nav, scale);
      RenderStatRow(pBase, u8"군략소양", 0xC8, 1, &v_Exp_Str, scale);
      RenderStatRow(pBase, u8"보조소양", 0xC9, 1, &v_Exp_Sup, scale);
      RenderStatRow(pBase, u8"둔갑소양", 0xCA, 1, &v_Exp_Dis, scale);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Separator();

      // --- [ 특기 ] ---
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
        ImGui::Selectable(u8" [ 특기 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor(2);

        RenderStatRow(pBase, u8"임무소양", 0xCB, 1, &v_Exp_Task, scale);
        RenderStatRow(pBase, u8"지모소양", 0xCC, 1, &v_Exp_Intel, scale);
        RenderStatRow(pBase, u8"병과소양", 0xCD, 1, &v_Exp_War, scale);
        RenderStatRow(pBase, u8"군사소양", 0xCE, 1, &v_Exp_Mil, scale);
        ImGui::EndTable();
    }
  }

  void DrawOfficerDetailWindow(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
    // 같은 주인공을 닫았다 다시 열어도 게임 원본 편집 결과를 다시 읽어야 합니다.
    // 기존 코드는 숨겨진 프레임에서 s_prevVisible을 false로 내리기 전에 return하여
    // 재오픈을 감지하지 못했습니다.
    static bool s_prevVisible = false;
    if (!bShowOfficerDetail) {
      s_prevVisible = false;
      return;
    }

    const bool bJustOpened = !s_prevVisible;
    s_prevVisible = true;

    if (p1 != s_lastCapturedAddress || !s_hasSnapshot || bJustOpened) {
      UpdateOfficerSnapshot(p1);
    }

    ImGui::SetNextItemWidth(580 * scale);

    // [최적화] 도시 정보 캐싱 및 검색 루프 제한
    ULONGLONG now = GetTickCount64();
    if (p1 != s_lastProcessedP1 || (now - s_lastCityUpdateMs) > 1000) {
      s_lastProcessedP1 = p1;
      s_lastCityUpdateMs = now;
      s_cachedTitleCity = "";

      if (s_cachedExeBase == 0) s_cachedExeBase = (uintptr_t)GetModuleHandle(NULL);
      
      if (s_cachedExeBase) {
        // 도시 배열 베이스 주소가 바뀌는 경우는 거의 없으므로 1회만 캐싱
        if (s_cachedCityArrayBase == 0) {
          uintptr_t pp1 = *(uintptr_t *)(s_cachedExeBase + 0x34C8630);
          if (pp1 && IsValidPtr(pp1, 8)) {
            uintptr_t pp2 = *(uintptr_t *)(pp1);
            if (pp2 && IsValidPtr(pp2, 8)) s_cachedCityArrayBase = *(uintptr_t *)(pp2);
          }
        }

        if (s_cachedCityArrayBase > 0x10000 && p1 > 0x10000) {
          uintptr_t cityPtr = *(uintptr_t *)(p1 + 0x20);
          if (cityPtr >= s_cachedCityArrayBase) {
            int idx = (int)((cityPtr - s_cachedCityArrayBase) / 0x2A0);
            if (idx >= 0 && idx < g_CityCount) {
              s_cachedTitleCity = " - [" + std::string(g_CityList[idx].cityname) + "]";
            }
          }
        }
      }
    }

    char titleBuf[128];
    sprintf_s(titleBuf, u8"주인공 무장 상세 편집%s###OffDetailWin", s_cachedTitleCity.c_str());

    static ImGuiWindowFlags OffDetailFlags = 
    ImGuiWindowFlags_AlwaysAutoResize |ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

    // 기본 가로 크기 설정 (원하시는 대로 숫자를 키우시면 됩니다)
    ImGui::SetNextWindowSize(ImVec2(800 * scale, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(titleBuf, &bShowOfficerDetail, OffDetailFlags)) {
      if (p1 == 0) {
        if (ImGui::Button(u8"새로고침", ImVec2(80 * scale, 0))) {
          UpdateOfficerSnapshot(p1);
          AddLog(u8"[정보] 주인공 데이터 강제 갱신 완료.");
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"닫기", ImVec2(80 * scale, 0))) {
          bShowOfficerDetail = false;
        }
        ImGui::End();
        return;
      }

      // 우측 상단 혹은 탭 이전에 새로고침 버튼 배치
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 100 * scale);
      if (ImGui::Button(u8"새로고침", ImVec2(80 * scale, 0))) {
        UpdateOfficerSnapshot(p1);
        AddLog(u8"[정보] 주인공 데이터 갱신 완료.");
      }
      ImGui::Spacing();

      unsigned short currentID = *(unsigned short *)(p1 + 0x08);
      std::string nameValue = u8"주인공";
      if (g_officerNames.count(currentID)) {
        nameValue = g_officerNames[currentID];
      }

      if (ImGui::BeginTabBar("OfficerTabs")) {

        if (ImGui::BeginTabItem(u8"상세 정보")) {
          // if (currentTabIdx != 0) {
          //   ImGui::SetWindowSize(ImVec2(0, 0));
          //   currentTabIdx = 0;
          // }
          DrawOfficerHeader(p1, scale);
          DrawOfficerTalents(p1, scale, p1);
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"능력/상태")) {
          RenderBasicTab(p1, scale, true);
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"기능")) {
          RenderResearchTab(p1, scale);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"특수 기능")) {
          RenderSpecialAbilityTab(p1, scale);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"소양(EXP)")) {
          RenderExpTab(p1, scale);
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::End();
    }
  }

  // ══════════════════════════════════════════════════════
  //  특수 기능 탭 렌더링
  //  군악대 / 무쌍보병 / 불꽃기병 / 원격궁병 /
  //  등갑군 / 총사령관 / 기습부대 / 대군사
  // ══════════════════════════════════════════════════════
  void RenderSpecialAbilityTab(uintptr_t pBase, float scale) {
    if (!IsValidPtr(pBase, 0x10)) return;
    int currentID = *(unsigned short*)(pBase + 0x08);

    ImGui::Spacing();
    // [다중 선택] 배너 표시
    size_t selCount = GetSelectedOfficerIDCount();
    if (selCount > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.5f, 1.0f));
      ImGui::Text(u8"★ %zu명 동시 편집 중 — 아래 변경은 체크된 모든 무장에 적용됩니다", selCount);
      ImGui::PopStyleColor();
      ImGui::Separator();
    }

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.0f, 1.0f), u8"[ 특수 능력 설정 ]");
    ImGui::SameLine();
    ImGui::TextDisabled(u8"(선택 무장에게 특수 유닛 능력을 부여합니다)");
    ImGui::TextDisabled(u8"해당 무장이 전투에 참여를 하면 해당 부대에 적용이 됩니다.");
    ImGui::Separator();
    ImGui::Spacing();

    struct AbilityEntry {
      const char* label;
      const char* desc;
      uintptr_t   vOffset;
    };

    AbilityEntry abilities[] = {
      { u8"군악대",    u8"분기/고무 효율 증가 1,2레벨 효과 +5. 3레벨 광역기",    0x1000 },
      { u8"무쌍 보병", u8"강격 관통 공격, 맹돌이 난격처럼 주변 동시 타격",       0x1001 },
      { u8"불꽃 기병", u8"연격/기사 공격시 50, 70, 100% 확률로 점화",           0x1002 },
      { u8"원격 궁병", u8"궁병 전법 최대 사거리 1증가. 시람은 최소 사거리도 증가",0x1003 },
      { u8"등갑군",    u8"턴이 올때 해당 부대가 염상 상태면 즉시 병력 -1000, 전의 -10이 감소합니다. 이후 실제 게임상 염상 피해는 별도 적용됩니다.", 0x1004 },
      { u8"총사령관",  u8"자신 턴에 모든 병종 통상 사거리 2칸이 됨 (무반격, 원호 효율 급등)",             0x1005 },
      { u8"기습부대",  u8"교란, 급습, 요격 상태이상 확률 대폭 증가(전법 레벨에 따라 50, 70, 100%)",       0x1006 },
      { u8"대군사",    u8"군사 보주와 동일한 광역 군략계 사용 (단, 동토 제외)",   0x1007 },
    };

    constexpr int COLS = 2;
    if (ImGui::BeginTable("SpecialAbilityTable", COLS,
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {

      for (int i = 0; i < (int)(sizeof(abilities) / sizeof(abilities[0])); i++) {
        ImGui::TableNextColumn();

        bool isEnabled = GetTargetSkillCount(currentID, abilities[i].vOffset) > 0;
        char checkId[64];
        snprintf(checkId, sizeof(checkId), "%s##sa_%d", abilities[i].label, i);

        if (ImGui::Checkbox(checkId, &isEnabled)) {
          // 체크 시 1, 해제 시 0 저장 (0일 경우 SkillCountManager에서 자동 삭제됨)
          SetTargetSkillCount(currentID, abilities[i].vOffset, isEnabled ? 1 : 0);
          AddLog(u8"[특수기능] 무장[%d] %s %s", currentID, abilities[i].label,
                 isEnabled ? u8"활성화 (저장됨)" : u8"비활성화 (삭제됨)");
          // [다중 선택] 체크된 모든 무장에 동일 특수 기능 적용
          if (GetSelectedOfficerIDCount() > 0) {
            const uintptr_t applyOffset = abilities[i].vOffset;
            const int       applyVal    = isEnabled ? 1 : 0;
            // 배열 베이스를 이용하여 무장 ID를 얻어 SetTargetSkillCount 호출
            ApplyPatchToSelectedOfficers([applyOffset, applyVal](uintptr_t base) {
              if (!IsValidPtr(base + 0x08, 2)) return;
              int id = (int)(*(unsigned short*)(base + 0x08));
              SetTargetSkillCount(id, applyOffset, applyVal);
            });
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(abilities[i].desc);
          ImGui::EndTooltip();
        }
      }

      ImGui::EndTable();
    }

    {
      bool isMusinEnabled = (GetTargetSkillCount(currentID, 0x1008) > 0);
      if (ImGui::Checkbox(u8"만능의 군세! 무신(병종 제약 무시)##musin", &isMusinEnabled)) {
        SetTargetSkillCount(currentID, 0x1008, isMusinEnabled ? 1 : 0);
        AddLog(u8"[특수기능] 무장[%d] 무신(병종제약무시) %s", currentID,
               isMusinEnabled ? u8"활성화 (저장됨)" : u8"비활성화 (삭제됨)");
        if (GetSelectedOfficerIDCount() > 0) {
          const int applyVal = isMusinEnabled ? 1 : 0;
          ApplyPatchToSelectedOfficers([applyVal](uintptr_t base) {
            if (!IsValidPtr(base + 0x08, 2)) return;
            int id = (int)(*(unsigned short*)(base + 0x08));
            SetTargetSkillCount(id, 0x1008, applyVal);
          });
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"해당 장수가 포함된 부대는 병종과 관계없이 모든 전법을 구사합니다");
        ImGui::EndTooltip();
      }

      bool isShipWeaponEnabled = (GetTargetSkillCount(currentID, 0x1009) > 0);
      if (ImGui::Checkbox(u8"함선 병기화 (지상에서 함선전법 활성화)##shipw", &isShipWeaponEnabled)) {
        SetTargetSkillCount(currentID, 0x1009, isShipWeaponEnabled ? 1 : 0);
        AddLog(u8"[특수기능] 무장[%d] 함선 병기화 %s", currentID,
               isShipWeaponEnabled ? u8"활성화 (저장됨)" : u8"비활성화 (삭제됨)");
        if (GetSelectedOfficerIDCount() > 0) {
          const int applyVal = isShipWeaponEnabled ? 1 : 0;
          ApplyPatchToSelectedOfficers([applyVal](uintptr_t base) {
            if (!IsValidPtr(base + 0x08, 2)) return;
            int id = (int)(*(unsigned short*)(base + 0x08));
            SetTargetSkillCount(id, 0x1009, applyVal);
          });
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"이 무장이 속한 부대가 지상에 있을 때 함선 전법을 사용할 수 있게 합니다.");
        ImGui::EndTooltip();
      }

    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"※ 설정값은 S8RPK_skill_counts.json 파일에 전법 횟수와 함께 저장/불러오기 됩니다.");
  }

} // namespace DX11Base