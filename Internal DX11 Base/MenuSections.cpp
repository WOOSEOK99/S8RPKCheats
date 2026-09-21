#include "MenuSections.h"
#include "Cheats.h"
#include "debug.h"
#include "Cheats/Civilian/BangmokCity.h"
#include "Cheats/Civilian/Bigcityconvert.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/Civilian/JewelSettings.h"
#include "Cheats/Civilian/DomesticsMult.h"
#include "Cheats/Civilian/NonggyeongCity.h"
#include "Cheats/Civilian/SangeopCity.h"
#include "Cheats/Civilian/Techpointcave.h"
#include "Cheats/Civilian/Techzero.h"
#include "Cheats/Officer/OfficerRosterResolve.h"
#include "Cheats/Officer/OfficerData.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Social/Fastrelationship.h"
#include "Cheats/Social/ChildEarlyAppearance.h"
#include "Cheats/Social/Infinitegift.h"
#include "Cheats/Social/Infinitetalk.h"
#include "Cheats/Social/InstantLoveCave.h"
#include "Cheats/Social/Loyaltycave.h"
#include "Cheats/Social/Resonancecave.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/SpeedHack.h"
#include "Cheats/Officer/OfficerDetail.h"
#include "Cheats/Officer/TraitViewerFeature.h"
#include "Cheats/Officer/TraitTextEditorWindow.h"
#include "Cheats/System/FactionTechEditor.h"
#include "Cheats/System/StartSetting.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/War/BattleMapShuffle.h"
#include "Cheats/War/Battleunitcapture.h"
#include "Cheats/War/Catapult.h"
#include "Cheats/War/Celestia.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/Dongto.h"
#include "Cheats/War/FactionLordBonus.h"
#include "Cheats/War/GovernorPrisonerDisposal.h"
#include "Cheats/War/Roadblock.h"
#include "Cheats/War/ReinforcementArrivalAction.h"
#include "Cheats/War/ReinforcementDefenderPlacement.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/War/ShortBattleCooldown.h"
#include "Cheats/War/TroopCountCombatScaling.h"
#include "Cheats/War/Terrainignore.h"
#include "Config.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"

namespace DX11Base {
  // 글로벌/네임스페이스 변수들에 대한 extern 선언 (정의는 다른 cpp 파일에 있음)
  extern void SetInstantAttitude(bool enable);
  extern bool g_initThreadRunning;
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

    // ------------------------------------------------------------------------------------------------
    // 3. 수치 입력 및 메모리 동기화 (간소화 버전 - 한 줄 표시용)
    // ------------------------------------------------------------------------------------------------
    static void DrawStatMini(const char *label, int *val, int offset, int size, uintptr_t baseAddr, int inputsize,
                             float scale) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(label);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(inputsize * scale);
      ImGui::PushID(label);
      if (ImGui::InputInt("##val", val, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
        if (baseAddr > 0x10000)
          DX11Base::ModifyStat(baseAddr, offset, *val, size);
      }
      if (!ImGui::IsItemActive() && baseAddr > 0x10000) {
        if (size == 1)
          *val = (int)(*(unsigned char *)(baseAddr + offset));
        else if (size == 2)
          *val = (int)(*(unsigned short *)(baseAddr + offset));
        else
          *val = (int)(*(unsigned int *)(baseAddr + offset));
      }
      ImGui::PopID();
    }
    //
    // ─────────────────────────────────────────────────────────────
    void DrawCivilianSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      if (p1) {
        BeginSection();

        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), u8"[ 자원 및 도시 활동 ]");
        // DrawStatRow(u8"금", 0x300, 2, &v_Gold, p1, gameBase, scale);
        // DrawStatRow(u8"행동력", 0xEE, 1, &v_AP, p1, gameBase, scale);
        // DrawStatRow(u8"우호의 증표", 0xF8, 2, &v_Token, 0, gameBase, scale);

        DrawStatMini(u8"금", &v_Gold, 0x300, 4, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"행동력", &v_AP, 0xEE, 1, p1, 40, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"우호의 증표", &v_Token, 0xF8, 2, gameBase, 40, scale);

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"행동력 무한", &bInfiniteAP)) {
          NotifyFeatureToggle(u8"행동력 무한", bInfiniteAP);
          SaveConfig();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"견문 시 민심 최대", &bAttitudeHack)) {
          ::DX11Base::SetInstantAttitude(bAttitudeHack);
          NotifyFeatureToggle(u8"견문 시 민심 최대", bAttitudeHack);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"도시에서 견문을 1회만 해도 민심 수치가 100이 됩니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"명품 자동 배분 (평정 끝날 때)", &bAutoFillSpecialties)) {
          NotifyFeatureToggle(u8"명품 자동 배분 (평정 끝날 때)", bAutoFillSpecialties);
          SaveConfig();
        }

        if (ImGui::Checkbox(u8"청부 무한 유지 (주점)", &bInfiniteTavernRequests)) {
          NotifyFeatureToggle(u8"청부 무한 유지 (주점)", bInfiniteTavernRequests);
          SaveConfig();
        }

        // -----------------------
        // ImGui::Separator();
        // ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), u8"[ 내정 배율 설정 ]");

        if (ImGui::Checkbox(u8"내정 배율 적용", &bDomestics)) {
          ::DX11Base::SetDomesticsMult(bDomestics);
          NotifyFeatureToggle(u8"내정 배율 적용", bDomestics);
          SaveConfig();
        }
        bool domesticsHov = ImGui::IsItemHovered(); // SameLine 전에 캡처

        // [도시 정보] 버튼 – 내정 배율 체크박스 오른쪽
        ImGui::SameLine(160.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button,
          bShowCityInfoWin ? ImVec4(0.18f, 0.55f, 0.18f, 1.f)
                           : ImVec4(0.15f, 0.30f, 0.55f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.80f, 1.f));
        if (ImGui::Button(u8"도시 정보", ImVec2(70.f * scale, 0.f)))
          bShowCityInfoWin = !bShowCityInfoWin;
        ImGui::PopStyleColor(2);

        if (domesticsHov) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"내정(개발, 보수 등) 시 배율을 적용합니다.");
          ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"현재 선택된 무장이 플레이어로 자동 등록됩니다.");
          ImGui::EndTooltip();
        }

        if (bDomestics) {
          ImGui::Indent();
          if (ImGui::SliderFloat(u8"플레이어 배율", &fDomesticsPlayer, 1.0f, 10.0f, "%.1fx")) {
            ::DX11Base::SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
            SaveConfig();
          }
          if (ImGui::SliderFloat(u8"세력 배율", &fDomesticsForce, 1.0f, 10.0f, "%.1fx")) {
            ::DX11Base::SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
            SaveConfig();
          }
          ImGui::Unindent();
        }

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        // --- 대도시 전환 추가 ---
        bool wasBigCityRunning = DX11Base::g_bigCityThreadRunning.load();
        if (wasBigCityRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"기술도시로 전환", &bBigCity)) {
          DX11Base::SetBigCityConvert(bBigCity);
          NotifyFeatureToggle(u8"기술도시로 전환", bBigCity);
          SaveConfig();
        }
        if (wasBigCityRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 낙양, 장안, 허창, 업, 양양, 건업, 성도");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 기술도시로 변환 및 최대 수치 한도 보정");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        bool wasBangmokRunning = DX11Base::g_bangmokThreadRunning.load();
        if (wasBangmokRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"방목도시 황폐화", &bBangmokCity)) {
          DX11Base::SetBangmokCity(bBangmokCity);
          NotifyFeatureToggle(u8"방목도시 황폐화", bBangmokCity);
          SaveConfig();
        }
        if (wasBangmokRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 오환, 강, 선비, 저, 남만 등 방목도시");
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                             u8"내용 : 방목도시의 능력치를 저하시키고 최대 수치를 고정합니다.");
          ImGui::EndTooltip();
        }

        bool wasNongRunning = DX11Base::g_nongCityThreadRunning.load();
        if (wasNongRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"농경도시 버프", &bNonggyeongCity)) {
          DX11Base::SetNonggyeongCity(bNonggyeongCity);
          NotifyFeatureToggle(u8"농경도시 버프", bNonggyeongCity);
          SaveConfig();
        }
        if (wasNongRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 남피, 평원, 북해, 제남, 하비, 소패, 계양 등");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 농경도시로 변환 및 농촌/상가 수치 한도 상향");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        bool wasSagRunning = DX11Base::g_sagCityThreadRunning.load();
        if (wasSagRunning)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(u8"상업도시 버프", &bSangeopCity)) {
          DX11Base::SetSangeopCity(bSangeopCity);
          NotifyFeatureToggle(u8"상업도시 버프", bSangeopCity);
          SaveConfig();
        }
        if (wasSagRunning)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"대상 도시 : 무희, 제남, 요동, 업, 성도, 건업 등 (기술도시 제외)");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"내용 : 상업도시로 변환 및 농촌/상가 수치 한도 상향");
          ImGui::EndTooltip();
        }

        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 명성치 편집 ]");

        DrawStatMini(u8"무명", &v_RepM, 0x106, 2, p1, 60, scale);
        ImGui::SameLine(100 * scale);
        DrawStatMini(u8"문명", &v_RepL, 0x104, 2, p1, 60, scale);
        ImGui::SameLine(200 * scale);
        DrawStatMini(u8"악명", &v_RepI, 0x108, 2, p1, 60, scale);

        ImGui::SetCursorPosX(200 * scale); // '악명' 라벨이 시작되는 위치와 동일하게 설정
        if (ImGui::Checkbox(u8"악명 항상 0 유지", &bZeroInfamy)) {
          NotifyFeatureToggle(u8"악명 항상 0 유지", bZeroInfamy);
          SaveConfig();
        }

        EndSection();

        BeginSection();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 및 진급 관련 ]");
        DrawStatRow(u8"전략 포인트", 0xED, 1, &v_SP, p1, gameBase, scale);
        DrawStatRow(u8"공적", 0x100, 2, &v_Merit, p1, gameBase, scale);
        DrawStatRow(u8"특권", 0xEA, 1, &v_Priv, 0, gameBase, scale);

        // DrawStatMini(u8"전략P", &v_SP, 0xED, 1, p1, 60, scale);
        // ImGui::SameLine(110 * scale);
        // DrawStatMini(u8"공적", &v_Merit, 0x100, 2, p1, 60, scale);
        // ImGui::SameLine(210 * scale);
        // DrawStatMini(u8"특권", &v_Priv, 0xEA, 1, p1, 60, scale);

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"전기 발생 무제한", &bInfTengi)) {
          NotifyFeatureToggle(u8"전기 발생 무제한", bInfTengi);
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

        if (ImGui::Checkbox(u8"중지 성성 취소", &bCancelCastleEvent)) {
          NotifyFeatureToggle(u8"중지 성성 취소", bCancelCastleEvent);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"중지 성성이 발생하면 즉시 취소합니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Button(u8"전기발생 취소", ImVec2(120, 26))) {
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

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"만병 습득 조건 해제", &bSkillCondition)) {
          DX11Base::ApplySkillCondition(bSkillCondition);
          NotifyFeatureToggle(u8"만병 습득 조건 해제", bSkillCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"만병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);
        if (ImGui::Checkbox(u8"상병 습득 조건 해제", &bSangbyeongCondition)) {
          DX11Base::ApplySangbyeongCondition(bSangbyeongCondition);
          NotifyFeatureToggle(u8"상병 습득 조건 해제", bSangbyeongCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"상병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        if (ImGui::Checkbox(u8"유목기병 습득 조건 해제", &bYumokCondition)) {
          DX11Base::ApplyYumokCondition(bYumokCondition);
          NotifyFeatureToggle(u8"유목기병 습득 조건 해제", bYumokCondition);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"유목기병 특기를 조건 없이 즉시 습득 가능한 상태로 변경합니다.");
          ImGui::EndTooltip();
        }

        ImGui::Spacing(); // 위아래 여백
        ImGui::Separator();
        ImGui::Spacing(); // 위아래 여백

        if (ImGui::Checkbox(u8"능력치 한계돌파", &bAutoStatUp99)) {
          NotifyFeatureToggle(u8"능력치 한계돌파", bAutoStatUp99);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"평정 기간 진입 시, 모든 장수의 능력치 중 99인 항목을 100으로 올립니다.");
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
        DrawStatMini(u8"담력", &v_Brave, 0x5BB8, 4, gameBase, 60, scale);
        ImGui::SameLine(120 * scale);
        if (ImGui::Checkbox(u8"보주 교체 무제한", &bFastJewel)) {
          NotifyFeatureToggle(u8"보주 교체 무제한", bFastJewel);
          SaveConfig();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool allJewelsOpen = DX11Base::IsAllJewelsOpenPreferred();
        if (ImGui::Checkbox(u8"보주 전체 개방", &allJewelsOpen)) {
          if (DX11Base::SetAllJewelsOpen(allJewelsOpen)) {
            DX11Base::AddNotification(allJewelsOpen ? u8"보주 전체 개방 ON" : u8"보주 전체 개방 OFF");
            SaveConfig();
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"모든 보주가 개방됩니다.");
          ImGui::EndTooltip();
        }

        bool allSecondaryJewels = DX11Base::IsAllSecondaryJewelsEnabled();
        if (ImGui::Checkbox(u8"보조 보주 전체 사용", &allSecondaryJewels)) {
          if (DX11Base::SetAllSecondaryJewelsEnabled(allSecondaryJewels)) {
            DX11Base::AddNotification(allSecondaryJewels ? u8"보조 보주 전체 사용 ON"
                                                         : u8"보조 보주 전체 사용 OFF");
            SaveConfig();
          }
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"모든 보주를 사용할수 있도록 설정합니다.");
          ImGui::EndTooltip();
        }
      }

      EndSection(); // 보주 설정
    }

    void DrawSocialSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(
          ImVec4(0.82f, 0.7f, 0.55f, 1.0f),
          u8"[ 교류 ]");

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
          }

          DX11Base::SetInstantLoveCave(*var, mode);
          NotifyFeatureToggle(label, *var);
          SaveConfig();
        }

        if (wasRunning)
          ImGui::EndDisabled();
      };

      if (ImGui::BeginTable(
              "SocialInteractionLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(
            "SocialUnlimited",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);
        ImGui::TableSetupColumn(
            "SocialOthers",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);

        // 1행: 선물 기증 무제한 / 즉시 경애
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"선물 기증 무제한", &bInfiniteGift)) {
          DX11Base::SetInfiniteGift(bInfiniteGift);
          NotifyFeatureToggle(u8"선물 기증 무제한", bInfiniteGift);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"즉시 경애 맺기", &bFastRelationship)) {
          DX11Base::SetFastRelationship(bFastRelationship);
          NotifyFeatureToggle(u8"즉시 경애 맺기", bFastRelationship);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"교류로 관계가 갱신되는 대상의 친밀도를 100으로 처리해 즉시 경애 상태로 진입시킵니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 기존 즉시 경애 패치 대신 CT ID 321 방식을 사용합니다.");
          ImGui::EndTooltip();
        }

        // 2행: 담화 무제한 / 혐오·상극 무시
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"담화 실행 무제한", &bInfiniteTalk)) {
          DX11Base::SetInfiniteTalk(bInfiniteTalk);
          NotifyFeatureToggle(u8"담화 실행 무제한", bInfiniteTalk);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        DrawLoveCheckbox(
            u8"혐오/상극 무시 경애",
            &bHateCave,
            LoveMode::HateIgnore);

        // 3행: 대련 무제한 / 공명
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"대련 실행 무제한", &bInfiniteDuel)) {
          DX11Base::SetInfiniteDuel(bInfiniteDuel);
          NotifyFeatureToggle(u8"대련 실행 무제한", bInfiniteDuel);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        {
          const bool resonanceBusy =
              ::DX11Base::g_resonanceThreadRunning.load();

          if (resonanceBusy)
            ImGui::BeginDisabled();

          if (ImGui::Checkbox(u8"무조건 공명 발생", &bResonance)) {
            DX11Base::SetInstantResonance(bResonance);
            NotifyFeatureToggle(u8"무조건 공명 발생", bResonance);
            SaveConfig();
          }

          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(
                ImVec4(1, 1, 0, 1),
                u8"무조건 공명갯수 4개로 되고,");
            ImGui::TextColored(
                ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                u8"다음번 담화때 상생 발생함.");
            ImGui::EndTooltip();
          }

          if (resonanceBusy)
            ImGui::EndDisabled();
        }

        // 4행: 토론 무제한 / 충성도
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"토론 실행 무제한", &bInfiniteDebate)) {
          DX11Base::SetInfiniteDebate(bInfiniteDebate);
          NotifyFeatureToggle(u8"토론 실행 무제한", bInfiniteDebate);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        if (::DX11Base::g_loyaltyThreadRunning.load())
          ImGui::BeginDisabled();

        if (ImGui::Checkbox(u8"무장 충성도 100", &bLoyalty)) {
          DX11Base::SetInstantLoyalty(bLoyalty);
          NotifyFeatureToggle(u8"무장 충성도 100", bLoyalty);
          SaveConfig();
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"교류 클릭시 목록에 있는 모든 무장의 충성이 100이 됨.");
          ImGui::EndTooltip();
        }

        if (::DX11Base::g_loyaltyThreadRunning.load())
          ImGui::EndDisabled();

        // 5행: 중개 무제한 / AI 친밀도 가속
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"중개 무제한", &bInfiniteMediation)) {
          if (bInfiniteMediation)
            ::DX11Base::TickInfiniteMediation();
          NotifyFeatureToggle(u8"중개 무제한", bInfiniteMediation);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"중개 실행 후 생기는 사용 완료 플래그(+0x71E5 bit6)만 자동으로 해제합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 다른 상태 비트는 그대로 유지합니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(
                u8"AI 친밀도 가속",
                &g_autoAffinityGrowthEnabled)) {
          ResetAutoAffinityGrowthState();
          NotifyFeatureToggle(
              u8"AI 친밀도 가속",
              g_autoAffinityGrowthEnabled);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"매 분기 평정월(1·4·7·10월) 시작 시 AI 무장끼리 친밀도를 추가 상승시킵니다.");
          ImGui::TextUnformatted(
              u8"같은 세력·같은 도시에 있는 AI 쌍만 대상이며 주인공과 친밀도 -1 이하인 쌍은 제외합니다.");
          ImGui::TextUnformatted(
              u8"상성과 흥미·중시의 일치 정도를 반영하며 최대 친밀도는 100입니다.");
          ImGui::TextUnformatted(
              u8"친밀도만 가속하며 부부·의형제·상생 관계를 직접 생성하지 않습니다.");
          ImGui::TextDisabled(
              u8"※ 체크 상태는 설정 파일에 저장됩니다.");
          ImGui::EndTooltip();
        }

        // 6행: 연회 무제한 / 자녀 관리
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"연회 무제한", &bInfiniteBanquet)) {
          if (bInfiniteBanquet)
            ::DX11Base::TickInfiniteBanquet();
          NotifyFeatureToggle(u8"연회 무제한", bInfiniteBanquet);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"연회 실행 후 생기는 사용 완료 플래그만 자동으로 해제하여 계속 연회할 수 있게 합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 다른 상태 비트는 그대로 유지합니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(
                u8"자녀 관리",
                ImVec2(120.0f * scale, 0))) {
          bShowChildManagerWin = true;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"감지된 자녀 목록을 열어 자녀별로 임관 시점을 설정합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 새로 태어난 자녀도 이후 자녀 처리 시 자동으로 목록에 추가됩니다.");
          ImGui::EndTooltip();
        }

        // 7행: 결혼 무제한 / 빈칸
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool tempMarriage = ::DX11Base::marriageApplied;
        if (ImGui::Checkbox(u8"결혼 무제한", &tempMarriage)) {
          ::DX11Base::SetMarriageCondition(tempMarriage);
          NotifyFeatureToggle(u8"결혼 무제한", tempMarriage);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"배우자가 있어도 무조건 결혼이 됩니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 상대가 경애 상태일 때 기존 배우자 제한을 우회합니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 대신 타 세력의 경우 등용은 안되네요.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      EndSection(); // 교류
    }

        void DrawWarSection(uintptr_t p1, uintptr_t gameBase, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), u8"[ 전쟁 관련 ]");

      if (ImGui::BeginTable(
              "WarOptionsLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(
            "WarLeft",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);
        ImGui::TableSetupColumn(
            "WarRight",
            ImGuiTableColumnFlags_WidthStretch,
            1.0f);

        // 1행: 모든 무장 성향 적극 / AI 전투 개선
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"모든 무장 성향 적극", &bAllAggressive)) {
          DX11Base::NotifyFeatureToggle(u8"모든 무장 성향 적극 자동 적용", bAllAggressive);
          DX11Base::SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
              u8"체크 시, 게임 진입(주인공 포착) 순간 모든 유효 무장의 전략 성향이 '적극'으로 자동 적용됩니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 로드할 때 딱 한 번 적용되며 계속 유지해야 다음 플레이 시에도 반영됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"AI 전투 개선", &bAIWarImprove)) {
          const bool requested = bAIWarImprove;
          DX11Base::SetAIWarImprove(requested);
          DX11Base::NotifyFeatureToggle(u8"AI 전투 개선", bAIWarImprove);
          DX11Base::SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"AI가 더 적극적으로 전쟁을 걸고 공백지도 더 잘 점령하도록 조정합니다.");
          ImGui::TextUnformatted(u8"- 한 세력이 한 턴에 여러 세력을 공격할 수 있게 변경");
          ImGui::TextUnformatted(u8"- 주인공만 지나치게 공격하는 행동을 줄임");
          ImGui::TextUnformatted(u8"- 일부 군주가 빈 도시를 점령하지 않는 현상을 완화");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 게임 버전이 달라 예상한 데이터와 다르면 적용하지 않습니다.");
          ImGui::EndTooltip();
        }

        // 2행: 전투맵 랜덤 / 도독 포로 직접 처분
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"전투맵 랜덤(관문제외)", &bBattleMapShuffle)) {
          DX11Base::SetBattleMapShuffle(bBattleMapShuffle);
          NotifyFeatureToggle(u8"전투맵 랜덤(관문제외)", bBattleMapShuffle);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"매 분기 평정 기간 마다 모든 도시의 전투맵 데이터를 랜덤하게 섞습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 평정 종료 시 자동으로 원상 복구됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"도독 포로 직접 처분", &bGovernorPrisonerDisposal)) {
          const bool requested = bGovernorPrisonerDisposal;
          if (!DX11Base::SetGovernorPrisonerDisposal(requested))
            bGovernorPrisonerDisposal = DX11Base::IsGovernorPrisonerDisposalApplied();
          NotifyFeatureToggle(u8"도독 포로 직접 처분", bGovernorPrisonerDisposal);
          SaveConfig();
        }
        const bool governorPrisonerHovered = ImGui::IsItemHovered();

        ImGui::Indent(18.0f * scale);
        if (ImGui::Checkbox(u8"특권 1개 소비", &bGovernorPrisonerConsumePrivilege)) {
          SaveConfig();
        }
        const bool governorPrivilegeHovered = ImGui::IsItemHovered();
        ImGui::Unindent(18.0f * scale);

        if (governorPrisonerHovered) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"도독이 통치권 내 도시의 정규군 전투에서 승리했을 때 포로를 직접 처분할지 선택할 수 있게 합니다.");
          ImGui::TextUnformatted(u8"- 조건을 만족하면 포로 처분 전에 예/아니오 질문이 표시됩니다.");
          ImGui::TextUnformatted(u8"- 아니오를 선택하면 원래 게임의 포로 처분 흐름을 그대로 따릅니다.");
          ImGui::TextUnformatted(u8"- 체크 상태와 특권 소비 옵션은 설정 파일에 저장됩니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 게임 버전의 후킹 지점 바이트가 다르면 안전을 위해 적용하지 않습니다.");
          ImGui::EndTooltip();
        }

        if (governorPrivilegeHovered) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(u8"OFF: 예를 선택해도 특권을 소비하지 않습니다.");
          ImGui::TextUnformatted(u8"ON: 예를 선택하면 특권 1개를 소비한 뒤 직접 처분합니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 특권이 0개이면 특권 소비 모드에서 직접 처분할 수 없습니다.");
          ImGui::EndTooltip();
        }

        // 3행: 원군 도착 턴 즉시 행동 / 수비측 원군 총대장 근처 배치
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"원군 도착 턴 즉시 행동", &bReinforcementArrivalAction)) {
          const bool requested = bReinforcementArrivalAction;
          if (!DX11Base::SetReinforcementArrivalAction(requested))
            bReinforcementArrivalAction = DX11Base::IsReinforcementArrivalActionApplied();
          NotifyFeatureToggle(u8"원군 도착 턴 즉시 행동", bReinforcementArrivalAction);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"전투에 도착한 원군이 도착한 그 턴부터 바로 행동할 수 있게 합니다.");
          ImGui::TextUnformatted(u8"- 원군 도착 처리를 명령 처리보다 먼저 실행하도록 순서를 변경합니다.");
          ImGui::TextUnformatted(u8"- 공격측/수비측 원군의 배치 위치는 이 옵션에서 변경하지 않습니다.");
          ImGui::TextUnformatted(u8"- 체크 상태는 설정 파일에 저장되어 다음 실행 시 다시 적용됩니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 전투 턴 전환 중에는 이 옵션을 켜거나 끄지 마세요.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"수비측 원군 총대장 근처 배치", &bReinforcementDefenderPlacement)) {
          const bool requested = bReinforcementDefenderPlacement;
          if (!DX11Base::SetReinforcementDefenderPlacement(requested))
            bReinforcementDefenderPlacement = DX11Base::IsReinforcementDefenderPlacementApplied();
          NotifyFeatureToggle(u8"수비측 원군 총대장 근처 배치", bReinforcementDefenderPlacement);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"수비측 원군이 도착할 때 총대장과 가까운 이동 가능한 빈 타일에 배치합니다.");
          ImGui::TextUnformatted(u8"- 총대장 기준 최대 5칸 범위에서 가까운 순서로 배치 위치를 탐색합니다.");
          ImGui::TextUnformatted(u8"- 점유된 타일, 사용 불가 지형, 통행 불가 타일은 제외합니다.");
          ImGui::TextUnformatted(u8"- 5칸 범위 안에 적절한 위치가 없으면 게임의 원래 배치 방식을 사용합니다.");
          ImGui::TextUnformatted(u8"- 총대장을 찾지 못하거나 판별이 애매해도 게임의 원래 배치 방식을 사용합니다.");
          ImGui::TextUnformatted(u8"- 공격측 원군의 배치 위치는 변경하지 않습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 원군 도착 순간 일시적인 화면 끊김이 발생할 수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 다수의 원군이 동시에 도착할 경우 더 눈에 띌 수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 전투 턴 전환 중에는 이 옵션을 켜거나 끄지 마세요.");
          ImGui::EndTooltip();
        }

        // 4행: 병력수 공방 반영 / 빈칸
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"병력수 공방 반영", &bTroopCountCombatScaling)) {
          const bool requested = bTroopCountCombatScaling;
          if (!DX11Base::SetTroopCountCombatScaling(requested))
            bTroopCountCombatScaling = DX11Base::IsTroopCountCombatScalingApplied();
          NotifyFeatureToggle(u8"병력수 공방 반영", bTroopCountCombatScaling);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
              u8"오리지널 시절의 방식처럼 병력 수가 부대 공격/방어 계산에 더 직접적으로 반영되게 합니다.");
          ImGui::TextUnformatted(
              u8"- 공격/방어 공용 계산 함수에서 게임에 남아 있는 선형 병력 환산 경로를 사용합니다.");
          ImGui::TextUnformatted(
              u8"- 병력이 많을수록 공방 계산에서 더 유리하고, 병력이 적을수록 상대적으로 불리해지는 방향입니다.");
          ImGui::TextUnformatted(
              u8"- 공격력/방어력에 고정 보너스를 더하는 기능이 아니라 계산 분기 자체를 오리지널식 경로로 바꿉니다.");
          ImGui::TextUnformatted(
              u8"- 병력 몇 명당 공방이 몇 상승하는지 같은 정확한 수치 공식은 CT 스크립트에 기재되어 있지 않습니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 게임 버전의 해당 바이트가 예상값과 다르면 안전을 위해 적용하지 않습니다.");
          ImGui::TextColored(
              ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
              u8"※ 체크 해제 시 원래 계산 분기로 복구합니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      // 단기접전 대기일수는 체크 상태와 수정값이 하나의 설정임을 명확히 보이도록 묶습니다.
      ImGui::PushStyleColor(
          ImGuiCol_Border,
          ImVec4(0.62f, 0.54f, 0.28f, 0.90f));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * scale);
      ImGui::BeginChild(
          "##ShortBattleCooldownGroup",
          ImVec2(0.0f, 42.0f * scale),
          true,
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

      bool shortCooldownHovered = false;
      if (ImGui::Checkbox(u8"단기접전 대기일수 변경", &bShortBattleCooldownEnabled)) {
        const bool requested = bShortBattleCooldownEnabled;
        if (!DX11Base::SetShortBattleCooldown(requested, iShortBattleCooldownDays))
          bShortBattleCooldownEnabled = DX11Base::IsShortBattleCooldownApplied();
        NotifyFeatureToggle(u8"단기접전 대기일수 변경", bShortBattleCooldownEnabled);
        SaveConfig();
      }
      shortCooldownHovered |= ImGui::IsItemHovered();

      ImGui::SameLine(0.0f, 14.0f * scale);
      ImGui::SetNextItemWidth(55.0f * scale);
      const int previousShortCooldownDays = iShortBattleCooldownDays;
      if (ImGui::InputInt("##ShortBattleCooldownDays", &iShortBattleCooldownDays, 0, 0,
                          ImGuiInputTextFlags_CharsDecimal)) {
        if (bShortBattleCooldownEnabled &&
            !DX11Base::SetShortBattleCooldown(true, iShortBattleCooldownDays)) {
          iShortBattleCooldownDays = previousShortCooldownDays;
        }
        SaveConfig();
      }
      shortCooldownHovered |= ImGui::IsItemHovered();

      ImGui::SameLine();
      ImGui::TextDisabled(u8"기본값 : 10");

      if (shortCooldownHovered) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"단기접전(일기토)이 다시 발생하기까지의 대기 날짜를 설정합니다.");
        ImGui::TextUnformatted(u8"- 예: 3으로 설정하면 단기접전 발생 후 3일 동안은 다시 단기접전이 발생하지 않습니다.");
        ImGui::TextUnformatted(u8"- 장수별 개별 대기시간이 아니라 전쟁에 출전한 부대 전체에 공통으로 적용됩니다.");
        ImGui::TextUnformatted(u8"- 기본값은 10일입니다.");
        ImGui::TextUnformatted(u8"- 체크 상태와 적용값은 설정 파일에 저장되어 다음 실행 시 다시 불러옵니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                           u8"※ 체크 해제 시 활성화 전에 읽어둔 원래 값으로 복구합니다.");
        ImGui::EndTooltip();
      }

      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();

      ImGui::Spacing();

      if (ImGui::Button(u8"전투 환경 및 조건 설정", ImVec2(150 * scale, 30 * scale))) {
        bShowBattleEnvWin = !bShowBattleEnvWin;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"모든 무장 일괄 편집", ImVec2(-1, 30 * scale))) {
        bShowBatchOfficerEditWin = !bShowBatchOfficerEditWin;
      }

      if (ImGui::Button(u8"세력별 기술력 편집", ImVec2(-1, 30 * scale))) {
        bShowFactionTechEditor = !bShowFactionTechEditor;
      }
      ::DX11Base::DrawBatchOfficerEditWindow(scale);
      ::DX11Base::DrawFactionTechEditor(scale);

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"클릭하여 날씨, 일자, 지형, 여울(공성전) 등의 상세 설정을 엽니다.");
        ImGui::EndTooltip();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 전법 및 건물 ]");
      ImGui::SameLine();
      if (ImGui::Button(u8"수정", ImVec2(100.0f * scale, 25.0f * scale))) {
        bShowTacticsEditWin = !bShowTacticsEditWin;
      }

      if (ImGui::Checkbox(u8"치료", &bSelfHeal)) {
        DX11Base::SetSelfHeal(bSelfHeal);
        NotifyFeatureToggle(u8"치료", bSelfHeal);
        SaveConfig();
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"동토", &bDongto)) {
        DX11Base::SetDongto(bDongto);
        NotifyFeatureToggle(u8"동토", bDongto);
        SaveConfig();
      }
      ImGui::SameLine();

      if (ImGui::Checkbox(u8"천계", &bCelestial)) {
        DX11Base::SetCelestialMod(bCelestial);
        NotifyFeatureToggle(u8"천계", bCelestial);
        SaveConfig();
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"투석", &bCatapult)) {
        DX11Base::SetCatapultCheat(bCatapult);
        NotifyFeatureToggle(u8"투석", bCatapult);
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"격류/낙석", &bTerrainIgnore)) {
        DX11Base::SetTerrainIgnore(bTerrainIgnore);
        NotifyFeatureToggle(u8"격류/낙석", bTerrainIgnore);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"방어 건물 강화", &bDefBuilding)) {
        DX11Base::SetDefBuildingBoost(bDefBuilding);
        NotifyFeatureToggle(u8"방어 건물 강화", bDefBuilding);
        SaveConfig();
      }

      EndSection(); // 전쟁
    }

    void DrawScenarioSection(uintptr_t p1, float scale) {
      BeginSection();
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.0f, 1.0f), u8"[ 시나리오 ]");

      ImGui::PushID(u8"ScenarioDate");
      {
        static int s_scenarioYearEdit = 200;
        static int s_scenarioMonthEdit = 1;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("[");
        ImGui::SameLine(0, 0);
        ImGui::SetNextItemWidth(40.0f * scale);
        ImGui::InputInt(u8"##scY", &s_scenarioYearEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        const bool yearDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool yearActive = ImGui::IsItemActive();

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("]");
        ImGui::SameLine(0, 4.0f * scale);
        ImGui::Text(u8"년");
        ImGui::SameLine(0, 10.0f * scale);
        ImGui::TextUnformatted("[");
        ImGui::SameLine(0, 0);
        ImGui::SetNextItemWidth(20.0f * scale);
        ImGui::InputInt(u8"##scM", &s_scenarioMonthEdit, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        const bool monthDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool monthActive = ImGui::IsItemActive();

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("]");
        ImGui::SameLine(0, 4.0f * scale);
        ImGui::Text(u8"월");

        if (yearDeactivatedAfterEdit)
          UpdateYear((unsigned short)s_scenarioYearEdit);
        if (monthDeactivatedAfterEdit) {
          if (s_scenarioMonthEdit >= 1 && s_scenarioMonthEdit <= 12)
            UpdateMonth((uint8_t)s_scenarioMonthEdit);
          else
            DX11Base::AddLog(u8"[시나리오 날짜] 월은 1~12만 가능합니다.");
        }

        // 매 프레임 VirtualQuery 폭주 방지: 짧게 스로틀 + 한 번에 연·월 읽기
        if (!yearActive && !yearDeactivatedAfterEdit && !monthActive && !monthDeactivatedAfterEdit) {
          static unsigned long long s_lastScenarioDatePoll = 0;
          const unsigned long long now = GetTickCount64();
          if (now - s_lastScenarioDatePoll >= 250ull) {
            s_lastScenarioDatePoll = now;
            unsigned short cy = 0;
            uint8_t cm = 0;
            if (ReadScenarioDate(&cy, &cm)) {
              if ((int)cy != s_scenarioYearEdit)
                s_scenarioYearEdit = (int)cy;
              if ((int)cm != s_scenarioMonthEdit)
                s_scenarioMonthEdit = (int)cm;
            }
          }
        }
      }
      ImGui::PopID();

      ImGui::SameLine(160.0f * scale);
      if (ImGui::Checkbox(u8"세력 군주 보너스 자동 배정", &bFactionLordBonus)) {
        DX11Base::SetFactionLordBonus(bFactionLordBonus);
        NotifyFeatureToggle(u8"세력 군주 보너스 자동 배정", bFactionLordBonus);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"관작 보너스");
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

      if (ImGui::Checkbox(u8"시나리오 수정", &bStartSetting)) {
        DX11Base::SetStartSetting(bStartSetting);
        NotifyFeatureToggle(u8"시나리오 수정", bStartSetting);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"시나리오 설정");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"체크시 새로운 시나리오 시작시 자동으로 적용이 됩니다.");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"※ 시나리오 변경(수정) 내용 ※");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"관우진군 : 관우-조홍 원수 버그 수정");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"지장집결 : 제갈량의 기술력 (연노병, 투석기까지 개발)");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"범장집결 : 전예 재야 신분으로 주인공 선택 가능");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"삼의 삼국지 : 환씨 조앙군으로 이적");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"네 군주 마지막 전쟁 : 반동탁 연합 해산, 네군주 우호도 0");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(160.0f * scale);

      if (ImGui::Checkbox(u8"모든 미발견 무장 재야로 변경", &bUndiscoveredToRonin)) {
        DX11Base::SetUndiscoveredToRonin(bUndiscoveredToRonin);
        NotifyFeatureToggle(u8"모든 미발견 무장 재야로 변경", bUndiscoveredToRonin);
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"<주의사항>");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"체크시 저장된 게임 불러올시에도 적용이 됩니다.");
        ImGui::EndTooltip();
      }

      if (ImGui::Checkbox(u8"모든 세력 기술 초기화", &bTechZero)) {
        DX11Base::SetTechZero(bTechZero);
        NotifyFeatureToggle(u8"모든 세력 기술 초기화", bTechZero);
        SaveConfig();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           u8"체크한 상태로 새로운 시나리오 시작시 모든 세력의 기술이 초기화 됩니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(160.0f * scale);

      if (ImGui::Checkbox(u8"교지 <-> 건녕 도로 차단", &bRoadBlock)) {
        DX11Base::SetRoadBlock(bRoadBlock);
        NotifyFeatureToggle(u8"교지 <-> 건녕 도로 차단", bRoadBlock);
        SaveConfig();
      }

      if (ImGui::Checkbox(u8"재야 장수 등장 알림", &bMonitorRonin)) {
        NotifyFeatureToggle(u8"재야 장수 등장 알림", bMonitorRonin);
        SaveConfig();
      }

      ImGui::SameLine(160.0f * scale);

      if (ImGui::Checkbox(u8"교지 <-> 회계 도로 차단", &bRoadBlock2)) {
        DX11Base::SetRoadBlock2(bRoadBlock2);
        NotifyFeatureToggle(u8"교지 <-> 회계 도로 차단", bRoadBlock2);
        SaveConfig();
      }

      // [신규] 데모플레이 제어 버튼
      float demoBtnWidth = 140.0f * scale;
      if (ImGui::Button(u8"데모플레이 중지", ImVec2(demoBtnWidth, 26.0f * scale))) {
        uintptr_t gBase = DX11Base::GetGameBase();
        uintptr_t p1_ptr = gBase + 0xE0;
        if (DX11Base::g_savedHeroAddr > 0x10000 && DX11Base::IsValidPtr(p1_ptr, 8)) {
          DWORD oldP;
          if (VirtualProtect((LPVOID)p1_ptr, 8, PAGE_READWRITE, &oldP)) {
            *(uintptr_t *)p1_ptr = DX11Base::g_savedHeroAddr;
            VirtualProtect((LPVOID)p1_ptr, 8, oldP, &oldP);
            DX11Base::AddLog(u8"[데모] 데모 플레이 중지 (주인공 주소 복원 완료: %p)",
                             (void *)DX11Base::g_savedHeroAddr);
          }
        } else if (DX11Base::g_savedHeroAddr <= 0x10000) {
          DX11Base::AddLog(u8"[데모] 복원할 백업 주소가 없습니다.");
        }
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"[사용 방법]");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"데모플레이 중 중지 버튼을 눌러 데모플레이를 중지합니다.");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"저장을 한뒤에 불러오기를 하면 정상적으로 플레이가 가능합니다.");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), u8"[ 주의 사항 ]");
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                           u8"마우스 우측키를 눌러 일시 정지후에 중지 버튼을 누르면 까만화면으로 바뀝니다.");
        ImGui::EndTooltip();
      }

      EndSection(); // 시나리오
    }

    void DrawOfficerDetailSection(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale) {
      BeginSection();

      float btnHeight = 26.0f * scale;

      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"[ 정보 ]");

      // 1행: 명품 / 주인공 / 모든 무장 - 3열 균등 배치
      if (ImGui::BeginTable("InfoTopRow", 3,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (p1 != 0) {
          if (ImGui::Button(u8"명품", ImVec2(-FLT_MIN, btnHeight))) {
            bShowSpecialtyInfoWin = !bShowSpecialtyInfoWin;
          }
        }

        ImGui::TableSetColumnIndex(1);
        if (p1 != 0) {
          if (ImGui::Button(u8"주인공", ImVec2(-FLT_MIN, btnHeight))) {
            bShowOfficerDetail = !bShowOfficerDetail;
          }
        }

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(u8"모든 무장", ImVec2(-FLT_MIN, btnHeight))) {
          DX11Base::bShowOfficerListWin = !DX11Base::bShowOfficerListWin;
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      // 2행: 기재 3슬롯 / 기재 이름 편집 - 2열 균등 배치
      if (ImGui::BeginTable("InfoTraitRow", 2,
                            ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox(u8"기재 3슬롯 활성화", &DX11Base::bTraitViewer)) {
          const bool requested = DX11Base::bTraitViewer;
          if (!DX11Base::SetTraitViewerFeature(requested))
            DX11Base::bTraitViewer = DX11Base::IsTraitViewerFeatureApplied();
          NotifyFeatureToggle(u8"기재 3슬롯 활성화", DX11Base::bTraitViewer);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"기재 슬롯을 2개에서 3개로 확장합니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),u8"편집 메뉴에서 3번째 기재를 부여할 수 있습니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(u8"기재 이름 편집", ImVec2(-FLT_MIN, btnHeight))) {
          DX11Base::OpenTraitTextEditorWindow();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"기본 기재 이름및 설명을 편집할수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"적용후 저장까지 하면 게임실행시 자동으로 적용이 됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }

      ImGui::Spacing();

      // 3행: 전체 폭
      if (ImGui::Button(u8"모든 무장 일괄 랜덤기재 부여", ImVec2(-FLT_MIN, 28.0f * scale))) {
        DX11Base::OpenBatchRandomTraitAssignmentWindow();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"모든 유효 무장의 기존 기재는 유지하고 빈 슬롯만 랜덤으로 채웁니다.");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"실행 전 황금/녹색/적색 등급을 선택할 수 있습니다.");
        ImGui::EndTooltip();
      }

      EndSection(); // 무장 정보
      DX11Base::TickTraitTextEditorAutoApply();
      DX11Base::DrawTraitTextEditorWindow(scale);
      DX11Base::DrawBatchRandomTraitAssignmentWindow(scale);

      BeginSection();
      ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), u8"[ 위젯 ]");
      if (ImGui::Checkbox(u8"전기취소##WIDGET", &DX11Base::bShowWidgetTengi)) {
        NotifyFeatureToggle(u8"위젯: 전기취소", DX11Base::bShowWidgetTengi);
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"주인공##WIDGET", &DX11Base::bShowWidgetHero)) {
        NotifyFeatureToggle(u8"위젯: 주인공", DX11Base::bShowWidgetHero);
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"모든무장##WIDGET", &DX11Base::bShowWidgetAllOfficers)) {
        NotifyFeatureToggle(u8"위젯: 모든 무장", DX11Base::bShowWidgetAllOfficers);
        SaveConfig();
      }

      ImGui::SameLine();
      if (ImGui::Checkbox(u8"알림확인##WIDGET", &DX11Base::bShowWidgetNotif)) {
        NotifyFeatureToggle(u8"위젯: 알림확인", DX11Base::bShowWidgetNotif);
        SaveConfig();
      }
      EndSection(); // 위젯

      BeginSection();
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), u8"[ 알림 설정 ]");

      ImGui::SetNextItemWidth(150.0f * scale);
      if (ImGui::SliderFloat(u8"알림 속도", &DX11Base::g_notificationSpeed, 20.0f, 500.0f, "%.0f px/s")) {
        SaveConfig();
      }

      ImGui::SameLine(0, 20.0f * scale);
      if (ImGui::Button(u8"알림 비우기", ImVec2(100.0f * scale, 0))) {
        g_notifications.clear();
        AddLog(u8"[알림] 모든 내역을 초기화했습니다.");
      }
      EndSection(); // 알림
    }
  } // namespace MenuSections
} // namespace DX11Base