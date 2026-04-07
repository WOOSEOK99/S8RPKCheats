#include "MenuSections.h"
#include "Cheats.h"
#include "Cheats/BattleMapShuffle.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Bigcityconvert.h"
#include "Cheats/Catapult.h"
#include "Cheats/Celestia.h"
#include "Cheats/Defatkboost.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/Dongto.h"
#include "Cheats/FactionLordBonus.h"
#include "Cheats/Fastrelationship.h"
#include "Cheats/Infinitegift.h"
#include "Cheats/Infinitetalk.h"
#include "Cheats/InstantLoveCave.h"
#include "Cheats/Loyaltycave.h"
#include "Cheats/Resonancecave.h"
#include "Cheats/Roadblock.h"
#include "Cheats/Selfheal.h"
#include "Cheats/SkillCondition.h"
#include "Cheats/SpeedHack.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/Techzero.h"
#include "Cheats/TengiCave.h"
#include "Cheats/Terrainignore.h"
#include "Config.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"

namespace DX11Base {
  // 글로벌/네임스페이스 변수들에 대한 extern 선언 (정의는 다른 cpp 파일에 있음)
  extern void SetInstantAttitude(bool enable);
  extern bool g_initThreadRunning;
  extern bool g_loyaltyThreadRunning;
  extern bool marriageApplied;
  extern void ToggleMarriageCondition();
  extern void SetMarriageCondition(bool enable);
} // namespace DX11Base

namespace DX11Base {
  namespace MenuSections {

    // ── 섹션 테두리 헬퍼 ──────────────────────────────────────────
    // 사용법: BeginSection() → 위젯들 → EndSection(padding)
    static void BeginSection() {
      ImGui::Spacing();
      ImGui::BeginGroup();
      // 컬럼 너비 끝까지 채워서 테두리 오른쪽을 컬럼 경계에 맞춤
      ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));
    }

    static void EndSection(float pad = 6.0f) {
      ImGui::EndGroup();
      ImVec2 min = ImGui::GetItemRectMin();
      ImVec2 max = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                          IM_COL32(255, 165, 0, 140), // 주황 계열 반투명
                                          8.0f,                       // 둥근 반경
                                          0,                          // flags
                                          1.2f                        // 두께
      );
      ImGui::Spacing();
    }
    //
    // ─────────────────────────────────────────────────────────────

    void DrawStatRow(const char *label, int offset, int size, int *inputVal, uintptr_t p1, uintptr_t gameBase,
                     float scale) {
      // 1. 현재 값 미리 읽기
      bool useP1 = (p1 != 0 && offset < 0x5000);
      uintptr_t targetAddr = (useP1) ? p1 : gameBase;
      unsigned int current = 0;
      bool valid = false;

      if (targetAddr > 0x10000) {
        valid = true;
        if (size == 1)
          current = *(unsigned char *)(targetAddr + offset);
        else if (size == 2)
          current = *(unsigned short *)(targetAddr + offset);
        else
          current = *(unsigned int *)(targetAddr + offset);
      }

      // 2. UI 그리기
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", label);

      // 레이블 이후 정렬 위치 고정 (테이블 없이 SameLine으로 깔끔하게 처리)
      ImGui::SameLine(100.0f * scale);

      ImGui::PushID(label);
      // 1. [-] 버튼
      if (ImGui::Button("-", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)--;
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          *(unsigned int *)(gameBase + offset) = (unsigned int)*inputVal;
      }
      ImGui::SameLine();

      // 2. 직접 입력 가능한 수치 박스 (InputInt)
      ImGui::SetNextItemWidth(70 * scale);
      // EnterReturnsTrue를 제거하여 자판 입력 시 즉시 변수에 반영되도록 함 (숫자만 입력 가능하도록 플래그 추가)
      ImGui::InputInt("##val", inputVal, 0, 0, ImGuiInputTextFlags_CharsDecimal);

      // 포커스를 잃거나 Enter를 쳤을 때(Deactivated) 수정한 내역이 있다면 저장
      bool justFinished = ImGui::IsItemDeactivatedAfterEdit();
      if (justFinished) {
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }

      // [중요] 사용자가 입력 중(포커스 상태)이거나, 막 입력이 끝난 프레임에는 메모리 값을 덮어씌우지 않음
      if (!ImGui::IsItemActive() && !justFinished && targetAddr > 0x10000) {
        if (size == 1)
          *inputVal = (int)(*(unsigned char *)(targetAddr + offset));
        else if (size == 2)
          *inputVal = (int)(*(unsigned short *)(targetAddr + offset));
        else
          *inputVal = (int)(*(unsigned int *)(targetAddr + offset));
      }
      ImGui::SameLine();

      // 3. [+] 버튼
      if (ImGui::Button("+", ImVec2(25 * scale, 25 * scale))) {
        (*inputVal)++;
        if (useP1 && p1)
          DX11Base::ModifyStat(p1, offset, *inputVal, size);
        else if (!useP1 && gameBase)
          DX11Base::ModifyStat(gameBase, offset, *inputVal, size);
      }
      ImGui::PopID();
    }

    void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 자원 및 도시 활동 ]");
        DrawStatRow(u8"금", 0x300, 2, &v_Gold, p1, gameBase, scale);
        DrawStatRow(u8"행동력", 0xEE, 1, &v_AP, p1, gameBase, scale);
        DrawStatRow(u8"우호의 증표", 0xF8, 2, &v_Token, 0, gameBase, scale);
        if (ImGui::Checkbox(u8"[내정] 행동력 무한", &bInfiniteAP))
          SaveConfig();

        // --- 대도시 전환 추가 ---
        bool wasBigCityRunning = DX11Base::g_bigCityThreadRunning;
        if (wasBigCityRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"[도시] 기술도시로 전환", &bBigCity)) {
          DX11Base::SetBigCityConvert(bBigCity);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 낙양, 장안, 허창, 업, 양양, 건업, 성도");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"개발 : 9000");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"상업 : 12000");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"방어 : 9000");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"기술 : 4000");
          ImGui::EndTooltip();
        }

        if (wasBigCityRunning) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"처리 중...");
          ImGui::EndDisabled();
        }
        // -----------------------

        if (ImGui::Checkbox(u8"[도시] 견문 시 민심 최대", &bAttitudeHack)) {
          ::DX11Base::SetInstantAttitude(bAttitudeHack);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"도시에서 견문을 1회만 해도 민심 수치가 100이 됩니다.");
          ImGui::EndTooltip();
        }

        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 명성치 편집 ]");
        DrawStatRow(u8"무명", 0x106, 2, &v_RepM, p1, gameBase, scale);
        DrawStatRow(u8"문명", 0x104, 2, &v_RepL, p1, gameBase, scale);
        DrawStatRow(u8"악명", 0x108, 2, &v_RepI, p1, gameBase, scale);

        if (ImGui::Checkbox(u8"악명 항상 0 유지", &bZeroInfamy)) {
          SaveConfig();
        }

        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 및 진급 관련 ]");
        DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP, p1, gameBase, scale);
        DrawStatRow(u8"공적", 0x100, 2, &v_Merit, p1, gameBase, scale);
        DrawStatRow(u8"특권", 0xEA, 1, &v_Priv, 0, gameBase, scale);

        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"전기 발생 무제한", &bInfTengi)) {
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"매 평정 마다 새로운 전기가 발생합니다.");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                             u8"이미 전기가 발생 중이었다면, 전기 발생이 끝난뒤부터 적용됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"만병 습득 조건 해제", &bSkillCondition)) {
          DX11Base::ApplySkillCondition(bSkillCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"만병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"중지 성성 취소", &bCancelCastleEvent)) {
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"중지 성성이 발생하면 즉시 취소합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"상병 습득 조건 해제", &bSangbyeongCondition)) {
          DX11Base::ApplySangbyeongCondition(bSangbyeongCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"상병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        float btnWidth = 120.0f * scale; // 버튼 간격 줄여서 빈공간 최소화
        float btnHeight = 26.0f * scale;
        float spacing = 10.0f * scale; // 버튼 사이의 여백

        if (ImGui::Button(u8"전기발생 취소", ImVec2(btnWidth, btnHeight))) {
          // 일회용 버튼: 현재 캡처된 주소가 있으면 값과 무관하게 취소(플래그 0으로 처리)
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[수동] 전기 취소 (플래그 적용)");
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"전기 발생을 즉시 취소합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"유목기병 습득 조건 해제", &bYumokCondition)) {
          DX11Base::ApplyYumokCondition(bYumokCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"유목기병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        // 훅/캡처 상태를 로그로 출력 (상태 변경 시 1회만)
        {
          static uintptr_t s_lastHookAddr = 0;
          static uintptr_t s_lastCaptAddr = 0;
          uintptr_t hookAddr = DX11Base::GetTengiHookAddr();
          uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();

          if (hookAddr != s_lastHookAddr) {
            if (hookAddr != 0) {
              DX11Base::AddLog(u8"[전기] 훅 지점 발견: %p (+0x%llX)", (void *)hookAddr,
                               (unsigned long long)DX11Base::GetTengiHookOffset());
            }
            s_lastHookAddr = hookAddr;
          }

          if (captAddr != s_lastCaptAddr) {
            if (captAddr != 0) {
              DX11Base::AddLog(u8"[전기] 캡처 주소 확보: %p", (void *)captAddr);
            }
            s_lastCaptAddr = captAddr;
          }
        }
        EndSection(); // 평정 및 진급
      }

      BeginSection();

      if (p1 != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"[ 보주 설정 ]");
        DrawStatRow(u8"담력", 0x5BB8, 4, &v_Brave, 0, gameBase, scale);
        if (ImGui::Checkbox(u8"보주 교체 무제한", &bFastJewel))
          SaveConfig();
      }

      EndSection(); // 보주 설정

      BeginSection();
      if (p1 != 0) {

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 무장 정보 ]");

        float btnWidth = 80.0f * scale; // 버튼 간격 줄여서 빈공간 최소화
        float btnHeight = 26.0f * scale;
        float spacing = 20.0f * scale; // 버튼 사이의 여백

        if (ImGui::Button(u8"주인공", ImVec2(btnWidth, btnHeight))) {
          bShowOfficerDetail = !bShowOfficerDetail;
        }

        ImGui::SameLine(0, spacing); // 이전 항목 끝에서 spacing만큼 띄움
        if (ImGui::Button(u8"모든 무장", ImVec2(btnWidth, btnHeight))) {
          DX11Base::bShowOfficerListWin = !DX11Base::bShowOfficerListWin;
        }
        ImGui::SameLine(0, spacing);
        if (ImGui::Button(u8"선택 무장", ImVec2(btnWidth, btnHeight))) {
          bShowSelectedOfficerWin = !bShowSelectedOfficerWin;
        }
      }

      EndSection(); // 무장 정보
    }

    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.82f, 0.7f, 0.55f, 1.0f), u8"[ 결혼/인연 관련 ]");

      bool wasRunning = ::DX11Base::g_initThreadRunning;

      auto DrawLoveCheckbox = [&](const char *label, bool *var, LoveMode mode) {
        if (wasRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(label, var)) {
          if (*var) {
            if (mode == LoveMode::Normal)
              bHateCave = false;
            else
              bLoveCave = false;
            DX11Base::SetInstantLoveCave(false);

            // [상호 배제] 인연 기능 켜지면 결혼 무제한 끄기
            if (::DX11Base::marriageApplied) {
              ::DX11Base::SetMarriageCondition(false);
            }
          }
          DX11Base::SetInstantLoveCave(*var, mode);
          SaveConfig();
        }
        if (wasRunning && *var) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), u8"초기화 중...");
        }
        if (wasRunning)
          ImGui::EndDisabled();
      };

      DrawLoveCheckbox(u8"[인연] 즉시 경애 맺기", &bLoveCave, LoveMode::Normal);
      DrawLoveCheckbox(u8"[인연] 혐오/상극 무시 경애", &bHateCave, LoveMode::HateIgnore);

      if (::DX11Base::g_resonanceThreadRunning)
        ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"[담화] 무조건 공명 발생", &bResonance)) {
        DX11Base::SetInstantResonance(bResonance);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"담화시 공명 갯수가 1개라도 있으면 무조건 공명 발생");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 1개도 없으면 공명발생하지 않음.");
        ImGui::EndTooltip();
      }

      if (::DX11Base::g_resonanceThreadRunning) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"초기화 중...");
        ImGui::EndDisabled();
      }

      if (ImGui::Checkbox(u8"[담화] 경애 시 무조건 공명", &bFastRelationship)) {
        DX11Base::SetFastRelationship(bFastRelationship);
        SaveConfig();
      }

      if (::DX11Base::g_loyaltyThreadRunning)
        ImGui::BeginDisabled();
      if (ImGui::Checkbox(u8"[교류] 무장 충성도 100", &bLoyalty)) {
        DX11Base::SetInstantLoyalty(bLoyalty);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"교류 클릭시 목록에 있는 모든 무장의 충성이 100이 됨.");
        ImGui::EndTooltip();
      }

      if (::DX11Base::g_loyaltyThreadRunning) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"초기화 중...");
        ImGui::EndDisabled();
      }

      if (ImGui::Checkbox(u8"[교류] 선물 기증 무제한", &bInfiniteGift)) {
        DX11Base::SetInfiniteGift(bInfiniteGift);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"[교류] 담화 실행 무제한", &bInfiniteTalk)) {
        DX11Base::SetInfiniteTalk(bInfiniteTalk);
        SaveConfig();
      }

      bool tempMarriage = ::DX11Base::marriageApplied;
      if (ImGui::Checkbox(u8"[결혼] 결혼 무제한", &tempMarriage)) {
        // [상호 배제] 결혼 무제한 켜지면 인연 기능 끄기
        if (tempMarriage) {
          if (bLoveCave || bHateCave) {
            bLoveCave = false;
            bHateCave = false;
            DX11Base::SetInstantLoveCave(false);
          }
        }
        ::DX11Base::SetMarriageCondition(tempMarriage);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"배우자가 있어도 무조건 결혼이 됩니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 대신 타 세력의 경우 등용은 안되네요.");
        ImGui::EndTooltip();
      }
      EndSection(); // 결혼/인연
    }

    void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");

      if (ImGui::Checkbox(u8"[전법 강화] 치료", &bSelfHeal)) {
        DX11Base::SetSelfHeal(bSelfHeal);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 치료 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 1 : 본인 부대 치료");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 2 : 본인 부대 치료 / 아군 부대 1칸 전체 치료");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 3 : 본인 부대 치료 / 아군 부대 2칸 전체 치료");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 동토", &bDongto)) {
        DX11Base::SetDongto(bDongto);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 동토 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 1 : 동토 확률 100%%");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 2 : 동토 확률 100%% / 상태이상 50%%");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨 3 : 동토 확률 100%% / 상태이상 100%%");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 격류/낙석", &bTerrainIgnore)) {
        DX11Base::SetTerrainIgnore(bTerrainIgnore);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"발동 조건 : 비");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"레벨별 데미지는 기존 데미지의 1/2");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 투석 병기", &bCatapult)) {
        DX11Base::SetCatapultCheat(bCatapult);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"사거리 증가: 2->5");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"광역딜");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"데미지증가 20->100");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[전법 강화] 천계", &bCelestial)) {
        DX11Base::SetCelestialMod(bCelestial);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"레벨별 천계 능력 강화");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"레벨 1 : 치료 효과 2000 / 광범위");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"레벨 2 : 치료 효과 3500 / 광범위");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"레벨 3 : 치료 효과 7000 / 광범위");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[수비] 방어 건물 사거리 강화", &bDefBuilding)) {
        DX11Base::SetDefBuildingBoost(bDefBuilding);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"수비측의 모든 건물의 사거리/시야 1 증가");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 전투중인 상태로 저장된 게임을 불러올때에는 반영이 안됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"[수비] 방어 건물 공격력 강화", &bDefAtk)) {
        DX11Base::SetDefAtkBoost(bDefAtk);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"수비측의 모든 건물의 공격력이 2배 증가합니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
        DX11Base::SetBattleMapShuffle(bBattleMapShuffle);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"매 분기 평정 기간 마다 모든 도시의 전투맵 데이터를 랜덤하게 섞습니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 평정 종료 시 자동으로 원상 복구됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"세력 군주 보너스 자동 배정", &bFactionLordBonus)) {
        DX11Base::SetFactionLordBonus(bFactionLordBonus);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"관직 보너스");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"황제 : 모든 능력치 +5, 병력 +5000");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"왕 : 모든 능력치 +4, 병력 +3000");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"공 : 모든 능력치 +3, 병력 +2000");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"주목 : 모든 능력치 +2, 병력 +1000");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"그냥 군주 : 모든 능력치 +1");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 지역별 왕이나 공의 차이는 없음");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 군주 관작 중 승상, 대장군은 주목과 동격");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), u8"※ 방랑군 두령은 보너스를 적용받지 않음");

        ImGui::EndTooltip();
      }

      EndSection(); // 전쟁
    }

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.0f, 1.0f), u8"[ 시나리오 ]");

      if (ImGui::Checkbox(u8"모든 세력 기술 초기화", &bTechZero)) {
        DX11Base::SetTechZero(bTechZero);
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           u8"체크한 상태로 새로운 시나리오 시작시 모든 세력의 기술이 초기화 됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"교지-건녕 / 교지-회계 도로 차단", &bRoadBlock)) {
        DX11Base::SetRoadBlock(bRoadBlock);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"재야 장수 등장 알림", &bMonitorRonin)) {
        SaveConfig();
      }

      // (전기 관련 UI는 '평정 및 진급 관련' 섹션으로 이동됨)
      EndSection(); // 시나리오

      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), u8"[ 위젯 ]");
      if (ImGui::Checkbox(u8"전기취소##WIDGET", &DX11Base::bShowWidgetTengi)) {
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"주인공##WIDGET", &DX11Base::bShowWidgetHero)) {
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"모든무장##WIDGET", &DX11Base::bShowWidgetAllOfficers)) {
        SaveConfig();
      }
      EndSection(); // 위젯
    }
  } // namespace MenuSections
} // namespace DX11Base
