#include "OfficerDetail.h"
#include "Cheats.h"
#include "SelectOfficercapture.h"
#include "pch.h"
#include "showlog.h"

extern ImGuiWindowFlags Flags;

namespace DX11Base {
  bool bShowOfficerDetail = false;

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
  void RenderStatRow(uintptr_t p1, const char *label, uintptr_t offset, int size, int *inputVal, float scale) {
    if (p1 > 0x10000) {
      if (size == 1)
        *inputVal = (int)(*(unsigned char *)(p1 + offset));
      else if (size == 2)
        *inputVal = (int)(*(unsigned short *)(p1 + offset));
      else
        *inputVal = (int)(*(unsigned int *)(p1 + offset));
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableNextColumn();
    ImGui::PushID(label);
    if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
      (*inputVal)--;
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
    }
    ImGui::SameLine();

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%d##val", *inputVal);
    if (ImGui::Button(valBuf, ImVec2(70 * scale, 25 * scale))) {
      pSelectedVar = inputVal;
      currentLabel = std::string(label) + u8" 입력기";
    }
    ImGui::SameLine();

    if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
      (*inputVal)++;
      DX11Base::ModifyStat(p1, offset, *inputVal, size);
    }
    ImGui::PopID();
  }

  // --- [공용 헬퍼 함수 2: 연구 트리용 콤팩트] ---
  void RenderCompactSkill(uintptr_t p1, const char *label, uintptr_t offset, int *val, float scale) {
    if (p1 > 0x10000)
      *val = (int)(*(unsigned char *)(p1 + offset));

    ImGui::PushID(label);

    // 레벨에 따른 색상 정의
    ImVec4 color;
    if (*val == 0)
      color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Grey
    else if (*val == 1)
      color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // Green
    else if (*val == 2)
      color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // Bright Green
    else
      color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Gold/Yellow

    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 3);

    char btnLabel[16];
    snprintf(btnLabel, sizeof(btnLabel), "%d##btn", *val);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    // 버튼 크기를 체크박스 정도로 키움 (25x25)
    if (ImGui::Button(btnLabel, ImVec2(25 * scale, 25 * scale))) {
      *val = (*val + 1) % 4; // 0, 1, 2, 3 순환
      DX11Base::ModifyStat(p1, offset, *val, 1);
    }
    ImGui::PopStyleColor();

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

        RenderStatRow(pBase, u8"행동력", 0xEE, 1, &v_Action, scale);
        RenderStatRow(pBase, u8"공적", 0x100, 2, &v_Contr, scale);
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
    if (!bShowOfficerDetail || p1 == 0)
      return;

    ImGui::SetNextWindowPos(ImVec2(mPos.x + mSize.x + 10.0f * scale, mPos.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(580 * scale, 750 * scale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(u8"주인공 무장 상세 편집###OffDetailWin", &bShowOfficerDetail, Flags)) {
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1), u8"[ 주인공 실시간 정보 ]");
      ImGui::Text(u8"인식된 무장 ID: %d", *(unsigned short *)(p1 + 0x08));

      uintptr_t forceAddr = *(uintptr_t *)(p1 + 0x18);
      if (forceAddr > 0x10000) {
        ImGui::TextUnformatted(u8"세력 색상:");
        ImGui::SameLine();

        v_ForceColor = (int)(*(unsigned char *)(forceAddr + 0x09));

        ImGui::PushID("MainForceColorBtn");
        if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
          v_ForceColor--;
          if (v_ForceColor < 1)
            v_ForceColor = 1;
          ModifyStat(forceAddr, 0x09, v_ForceColor, 1);
        }
        ImGui::SameLine();

        char buf[32];
        snprintf(buf, sizeof(buf), "%d##val", v_ForceColor);
        if (ImGui::Button(buf, ImVec2(60 * scale, 25 * scale))) {
          pSelectedVar = &v_ForceColor;
          currentLabel = u8"세력 색상 입력기";
        }
        ImGui::SameLine();

        if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
          v_ForceColor++;
          if (v_ForceColor > 114)
            v_ForceColor = 114;
          ModifyStat(forceAddr, 0x09, v_ForceColor, 1);
        }
        ImGui::PopID();
      }
      ImGui::Separator();

      if (ImGui::BeginTabBar("OfficerTabs")) {
        if (ImGui::BeginTabItem(u8"기본/명성")) {
          RenderBasicTab(p1, scale, false);
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