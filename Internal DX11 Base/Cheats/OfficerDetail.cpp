#include "OfficerDetail.h"
#include "Cheats.h"
#include "MenuState.h"
#include "OfficerData.h"
#include "SelectOfficercapture.h"
#include "CityData.h"
#include "pch.h"
#include <cstring>
#include "Framework/imgui.h"
#include "showlog.h"

extern ImGuiWindowFlags Flags;

namespace DX11Base {
  bool bShowOfficerDetail = false;
  extern bool bForceCenterOfficerDetail;

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

  // 특기 변수
  static int v_A_M1 = 0, v_A_M2 = 0, v_A_M3 = 0, v_A_M4 = 0, v_A_M5 = 0, v_A_M6 = 0;
  static int v_A_I1 = 0, v_A_I2 = 0, v_A_I3 = 0, v_A_I4 = 0, v_A_I5 = 0, v_A_I6 = 0;
  static int v_A_U1 = 0, v_A_U2 = 0, v_A_U3 = 0, v_A_U4 = 0, v_A_U5 = 0, v_A_U6 = 0;
  static int v_A_W1 = 0, v_A_W2 = 0, v_A_W3 = 0, v_A_W4 = 0, v_A_W5 = 0, v_A_W6 = 0;

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
    if (g_officerInlineReadPtr <= 0x10000 || g_officerInlineReadPtr == pWrite)
      return;
    if (pWrite != g_capturedOfficerBase)
      return; // 스냅샷은 캡처된 무장 본문(0x3D0)에만 대응
    if (!IsValidPtr(pWrite, 0x3D0))
      return;
    memcpy((void *)g_officerInlineReadPtr, (void *)pWrite, 0x3D0);
  }

  void RenderStatRow(uintptr_t p1, const char *label, uintptr_t offset, int size, int *inputVal, float scale) {
    const uintptr_t pR = (g_officerInlineReadPtr > 0x10000 && p1 == g_capturedOfficerBase) ? g_officerInlineReadPtr : p1;

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
    }
    ImGui::SameLine();
 
    // 2. 직접 입력 가능한 수치 박스 (InputInt)
    ImGui::SetNextItemWidth(70 * scale);
    // EnterReturnsTrue를 제거하여 자판 입력 시 즉시 변수에 반영되도록 함 (숫자만 입력 가능하도록 플래그 추가)
    ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);
    
    // 포커스를 잃거나 Enter를 쳤을 때(Deactivated) 수정한 내역이 있다면 저장
    bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
    if (justFinished) {
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
      SyncInlineReadBufFromWrite(p1);
    }
    
    // [중요] 사용자가 입력 중(포커스 상태)이거나, 막 입력이 끝난 프레임에는 메모리 값을 덮어씌우지 않음
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
    }
    ImGui::PopID();
  }

  // --- [공용 헬퍼 함수 2: 연구 트리용 콤팩트] ---
  void RenderCompactSkill(uintptr_t p1, const char *label, uintptr_t offset, int *val, float scale) {
    const uintptr_t pR = (g_officerInlineReadPtr > 0x10000 && p1 == g_capturedOfficerBase) ? g_officerInlineReadPtr : p1;
    if (pR > 0x10000)
      *val = (int)(*(unsigned char *)(pR + offset));

    ImGui::PushID(label);

    // 레벨에 따른 색상 정의 (0:기본, 1:파랑, 2:초록, 3:주황)
    bool hasCustomColor = false;
    if (*val == 1) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 1.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.3f, 0.6f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // 흰색 글씨
      hasCustomColor = true;
    } else if (*val == 2) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.4f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // 흰색 글씨
      hasCustomColor = true;
    } else if (*val == 3) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.3f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // 흰색 글씨
      hasCustomColor = true;
    }

    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 3);

    char btnLabel[16];
    snprintf(btnLabel, sizeof(btnLabel), "%d##btn", *val);

    // 버튼 크기를 체크박스 정도로 키움 (25x25)
    if (ImGui::Button(btnLabel, ImVec2(25 * scale, 25 * scale))) {
      *val = (*val + 1) % 4; // 0, 1, 2, 3 순환
      DX11Base::ModifyStat(p1, offset, *val, 1);
      SyncInlineReadBufFromWrite(p1);
    }

    if (hasCustomColor) {
      ImGui::PopStyleColor(4);
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"클릭하여 레벨 순환 (0->1->2->3)");
      ImGui::EndTooltip();
    }

    ImGui::PopID();
  }

  // --- [공용 헬퍼 함수 3: 전법/특기 행 그리기] ---
  void RenderResearchRow(uintptr_t pBase, const char *catLabel, const char *items[], uintptr_t offsets[], int *vars[],
                         int count, float scale) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(catLabel);
    for (int i = 0; i < count; i++) {
      ImGui::TableSetColumnIndex(i + 1);
      RenderCompactSkill(pBase, items[i], offsets[i], vars[i], scale);
    }
  }

  // --- [기재 관련 함수군] ---

  uint16_t GetTraitID(uintptr_t officerBase, int slotIndex) {
    if (officerBase < 0x10000)
      return 0;
    uintptr_t pA = *(uintptr_t *)(officerBase + 0x88);
    if (pA < 0x10000)
      return 0;
    uintptr_t pSlot = *(uintptr_t *)(pA + slotIndex * 0x08);
    if (pSlot < 0x10000)
      return 0;
    return *(uint16_t *)(pSlot + 0x08);
  }

  void SetTraitID(uintptr_t officerBase, int slotIndex, uint16_t traitID) {
    if (officerBase < 0x10000)
      return;
    uintptr_t pA = *(uintptr_t *)(officerBase + 0x88);
    if (pA < 0x10000)
      return;
    uintptr_t pSlot = *(uintptr_t *)(pA + slotIndex * 0x08);
    if (pSlot < 0x10000)
      return;

    DWORD old, tmp;
    if (VirtualProtect((LPVOID)(pSlot + 0x08), 2, PAGE_READWRITE, &old)) {
      *(uint16_t *)(pSlot + 0x08) = traitID;
      VirtualProtect((LPVOID)(pSlot + 0x08), 2, old, &tmp);
      AddLog(u8"[기재변경] 슬롯%d → ID:%d 적용", slotIndex + 1, traitID);
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
    if (ImGui::BeginTable("BasicStatTable", 2, ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 130.0f * scale);
      ImGui::TableSetupColumn(u8"편집", ImGuiTableColumnFlags_WidthFixed, 160.0f * scale);

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
    static ImGuiTableFlags tflags =
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("ResearchTreeTable", 7, tflags)) {
      ImGui::TableSetupColumn(u8"분류", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
      for (int i = 0; i < 6; i++)
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      // --- [ 전법 (Tactics) ] ---
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      // 배경색을 살짝 깔고 전체 열을 차지하게 함 (잘림 방지)
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.5f, 0.7f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 1.0f, 1.0f));
      ImGui::Selectable(u8" [ 전법 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);
      ImGui::TableNextRow();

      // 전법 상세
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

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Separator();

      // --- [ 특기 (Skills) ] ---
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      // 배경색을 살짝 깔고 전체 열을 차지하게 함 (잘림 방지)
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.4f, 0.0f, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
      ImGui::Selectable(u8" [ 특기 ]", true, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
      ImGui::PopStyleColor(2);
      ImGui::TableNextRow();

      // 특기 상세
      {
        const char *s[] = {u8"연전", u8"급습", u8"질주", u8"견수", u8"매복", u8"신속"};
        uintptr_t o[] = {0x1C4, 0x1C5, 0x1C6, 0x1C7, 0x1C8, 0x1C9};
        int *v[] = {&v_A_M1, &v_A_M2, &v_A_M3, &v_A_M4, &v_A_M5, &v_A_M6};
        RenderResearchRow(pBase, u8"무용", s, o, v, 6, scale);
      }
      {
        const char *s[] = {u8"전식", u8"공성", u8"수성", u8"복병", u8"수상", u8"강창"};
        uintptr_t o[] = {0x1CA, 0x1CB, 0x1CC, 0x1CD, 0x1CE, 0x1CF};
        int *v[] = {&v_A_I1, &v_A_I2, &v_A_I3, &v_A_I4, &v_A_I5, &v_A_I6};
        RenderResearchRow(pBase, u8"기술", s, o, v, 6, scale);
      }
      {
        const char *s[] = {u8"치료", u8"진정", u8"정비", u8"격려", u8"질타", u8"선동"};
        uintptr_t o[] = {0x1D0, 0x1D1, 0x1D2, 0x1D3, 0x1D4, 0x1D5};
        int *v[] = {&v_A_U1, &v_A_U2, &v_A_U3, &v_A_U4, &v_A_U5, &v_A_U6};
        RenderResearchRow(pBase, u8"보조", s, o, v, 6, scale);
      }
      {
        const char *s[] = {u8"보장", u8"기장", u8"궁장", u8"수군", u8"조기", u8"신산"};
        uintptr_t o[] = {0x1DC, 0x1DD, 0x1DE, 0x1DF, 0x1E0, 0x1E1};
        int *v[] = {&v_A_U1, &v_A_U2, &v_A_U3, &v_A_U4, &v_A_U5, &v_A_U6};
        RenderResearchRow(pBase, u8"병과", s, o, v, 6, scale);
      }
      {
        const char *s[] = {u8"원호", u8"파성", u8"행군", u8"여력", u8"과감", u8"위풍"};
        uintptr_t o[] = {0x1E2, 0x1E3, 0x1E4, 0x1E5, 0x1E6, 0x1E7};
        int *v[] = {&v_A_W1, &v_A_W2, &v_A_W3, &v_A_W4, &v_A_W5, &v_A_W6};
        RenderResearchRow(pBase, u8"군사", s, o, v, 6, scale);
      }
      ImGui::EndTable();
    }
  }

  void RenderExpTab(uintptr_t pBase, float scale) {
    if (ImGui::BeginTable("ExpTable", 2, ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn(u8"항목", ImGuiTableColumnFlags_WidthFixed, 130.0f * scale);
      ImGui::TableSetupColumn(u8"편집", ImGuiTableColumnFlags_WidthFixed, 160.0f * scale);

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
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
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
    if (!bShowOfficerDetail)
      return;

    // 데이터 로딩 보장
    LoadOfficerNames();
    LoadEffectDefinitions();

    if (bForceCenterOfficerDetail) {
      ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      bForceCenterOfficerDetail = false;
    } else {
      ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(ImVec2(580 * scale, 750 * scale), ImGuiCond_FirstUseEver);

    // [신규] 주인공 도시 정보 실시간 확보
    std::string titleCity = u8"";
    {
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      uintptr_t cityArrayBase = 0;
      if (exeBase) {
        uintptr_t pp1 = *(uintptr_t *)(exeBase + 0x34C8630);
        if (pp1 && IsValidPtr(pp1, 8)) {
          uintptr_t pp2 = *(uintptr_t *)(pp1);
          if (pp2 && IsValidPtr(pp2, 8)) cityArrayBase = *(uintptr_t *)(pp2);
        }
      }
      if (cityArrayBase > 0x10000 && p1 > 0x10000) {
        uintptr_t cityPtr = *(uintptr_t *)(p1 + 0x20);
        if (cityPtr >= cityArrayBase) {
          int idx = (int)((cityPtr - cityArrayBase) / 0x2A0);
          if (idx >= 0 && idx < g_CityCount) {
            titleCity = " - [" + std::string(g_CityList[idx].cityname) + "]";
          }
        }
      }
    }

    char titleBuf[128];
    sprintf_s(titleBuf, u8"주인공 무장 상세 편집%s###OffDetailWin", titleCity.c_str());

    if (ImGui::Begin(titleBuf, &bShowOfficerDetail, Flags)) {
      if (p1 == 0) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), u8"캡처된 주인공 데이터가 없습니다.");
        ImGui::BulletText(u8"인게임(전략 화면 등)으로 진입해야 활성화됩니다.");
        ImGui::Spacing();
        if (ImGui::Button(u8"닫기")) {
          bShowOfficerDetail = false;
        }
        ImGui::End();
        return;
      }

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
          DrawOfficerTalents(p1, scale);
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
        if (ImGui::BeginTabItem(u8"소양(EXP)")) {
          RenderExpTab(p1, scale);
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::End();
    }
  }

} // namespace DX11Base