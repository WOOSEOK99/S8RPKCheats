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
#include "Cheats/Officer/SpecialAbilityAutoAssign.h"
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
#include "Cheats/War/PrisonerCaptureManagement.h"
#include "Cheats/War/Roadblock.h"
#include "Cheats/War/ReinforcementArrivalAction.h"
#include "Cheats/War/ReinforcementDefenderPlacement.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/War/Spell5HealProbe.h"
#include "Cheats/War/StratagemGaugeMax.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Cheats/War/ShortBattleCooldown.h"
#include "Cheats/War/TotalWarCycleShortening.h"
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

    // GrowthM이 참조하는 게임 능력 성장 설정.
    // EXE + 0x3BA9850 구조체의 +0x38 한 바이트에
    // 플레이어 세력/타 세력 성장 속도가 각각 2bit로 저장되어 있다.
    // Step 1에서는 읽기 전용 진단만 수행한다.
    struct OfficerGrowthGameSettings {
      bool valid = false;
      uint8_t packed = 0;
      int playerForce = 0; // bits 0..1
      int otherForces = 0; // bits 2..3
    };

    static OfficerGrowthGameSettings ReadOfficerGrowthGameSettings() {
      OfficerGrowthGameSettings result{};
      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return result;

      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr =
          exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;

      if (!DX11Base::IsValidPtr(addr, 1))
        return result;

      result.packed = *(const uint8_t *)addr;
      result.playerForce = (int)(result.packed & 0x03);
      result.otherForces = (int)((result.packed >> 2) & 0x03);
      result.valid = true;
      return result;
    }

    static const char *OfficerGrowthSettingName(int raw) {
      switch (raw) {
      case 0: return u8"없음";
      case 1: return u8"느림";
      case 2: return u8"보통";
      case 3: return u8"빠름";
      default: return u8"알 수 없음";
      }
    }

    static bool WriteOfficerGrowthGameSettingsRaw(int playerForce, int otherForces) {
      if (playerForce < 0 || playerForce > 3 ||
          otherForces < 0 || otherForces > 3)
        return false;

      const uintptr_t exe = (uintptr_t)GetModuleHandle(nullptr);
      if (!exe)
        return false;

      constexpr uintptr_t kGrowthSettingObjectOffset = 0x3BA9850;
      constexpr uintptr_t kGrowthSettingPackedOffset = 0x38;
      const uintptr_t addr =
          exe + kGrowthSettingObjectOffset + kGrowthSettingPackedOffset;

      if (!DX11Base::IsValidPtr(addr, 1))
        return false;

      const uint8_t oldValue = *(const uint8_t *)addr;
      const uint8_t newValue =
          (uint8_t)((oldValue & 0xF0) |
                    (playerForce & 0x03) |
                    ((otherForces & 0x03) << 2));

      DWORD oldProtect = 0;
      if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProtect))
        return false;

      *(uint8_t *)addr = newValue;

      DWORD dummy = 0;
      VirtualProtect((LPVOID)addr, 1, oldProtect, &dummy);
      return *(const uint8_t *)addr == newValue;
    }

    static bool WriteOfficerGrowthGameSettings(int speed) {
      if (speed < 1 || speed > 3)
        return false;

      return WriteOfficerGrowthGameSettingsRaw(speed, speed);

    }

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

        if (ImGui::Checkbox(u8"행동력 무제한", &bInfiniteAP)) {
          NotifyFeatureToggle(u8"행동력 무제한", bInfiniteAP);
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

        if (ImGui::Checkbox(u8"청부 무제한 (주점)", &bInfiniteTavernRequests)) {
          NotifyFeatureToggle(u8"청부 무제한 (주점)", bInfiniteTavernRequests);
          SaveConfig();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Checkbox(u8"명품 자동 배분 (평정 끝날 때)", &bAutoFillSpecialties)) {
          NotifyFeatureToggle(u8"명품 자동 배분 (평정 끝날 때)", bAutoFillSpecialties);
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

        if (ImGui::Checkbox(u8"매 평정 새로운 전기 발생", &bInfTengi)) {
          NotifyFeatureToggle(u8"매 평정 새로운 전기 발생", bInfTengi);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"매 평정 마다 새로운 전기가 발생합니다.");
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

        if (ImGui::Checkbox(u8"결전 발생 주기 단축", &bTotalWarCycleShortening)) {
          const bool requested = bTotalWarCycleShortening;
          if (!DX11Base::SetTotalWarCycleShortening(requested))
            bTotalWarCycleShortening = DX11Base::IsTotalWarCycleShorteningApplied();
          NotifyFeatureToggle(u8"결전 발생 주기 단축", bTotalWarCycleShortening);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1),
                             u8"결전 발생 후 다음 결전의 재발생 대기 주기를 1년으로 단축합니다.");
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                             u8"다른 결전 발생 조건은 그대로 유지됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::SameLine(160.0f * scale);

        if (ImGui::Button(u8"전기발생 즉시 취소", ImVec2(120, 26))) {
          // 일회용 버튼: 현재 캡처된 주소가 있으면 값과 무관하게 취소(플래그 0으로 처리)
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[수동] 전기 취소 (플래그 적용)");
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"현재 발생된 전기를 즉시 취소합니다.");
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
        ImGui::SameLine(160 * scale);
        if (ImGui::Checkbox(u8"보주 교체 무제한", &bFastJewel)) {
          NotifyFeatureToggle(u8"보주 교체 무제한", bFastJewel);
          SaveConfig();
        }

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
        ImGui::SameLine(160 * scale);
        if (ImGui::Checkbox(u8"보조 보주 모두 사용", &allSecondaryJewels)) {
          if (DX11Base::SetAllSecondaryJewelsEnabled(allSecondaryJewels)) {
            DX11Base::AddNotification(allSecondaryJewels ? u8"보조 보주 모두 사용 ON"
                                                         : u8"보조 보주 모두 사용 OFF");
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
              u8"상성과 흥미·중시의 일치 정도를 반영하며 최대 친밀도는 100입니다.");
          ImGui::TextUnformatted(
              u8"친밀도만 가속하며 부부·의형제·상생 관계를 직접 생성하지 않습니다.");
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
                             u8"AI 세력의 공격 판단, 공백지 점령, 항복권고 관련 문제를 함께 보정합니다.");
          ImGui::TextUnformatted(u8"- 공격 후보를 넓혀 기존 목표세력 외의 국가도 공격 대상으로 평가");
          ImGui::TextUnformatted(u8"- 주인공만 지나치게 공격하는 행동을 줄임");
          ImGui::TextUnformatted(u8"- 주인공이 군주가 아닐 때 소속 도시의 별도 공격 대기 조건을 제거");
          ImGui::TextUnformatted(u8"- 주인공 소속 도시도 일반 AI처럼 게임의 호전도 설정에 맞는 공격 기준을 사용");
          ImGui::TextUnformatted(u8"- 일부 군주가 빈 도시를 점령하지 않는 현상을 완화");
          ImGui::TextUnformatted(u8"- 의리가 높은 AI 군주는 항복권고를 더 잘 거부하도록 보정");
          ImGui::TextUnformatted(u8"- 항복권고 실패 후 3개월 동안 같은 대상 재권고를 막고 공격 우선도를 크게 상승");
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                             u8"※ 관련 패치 또는 hook이 예상한 원본 상태와 다르면 전체 적용을 보류합니다.");
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

        if (ImGui::Checkbox(u8"포로 및 처형 개선", &bPrisonerCaptureManagement)) {
          const bool requested = bPrisonerCaptureManagement;
          if (!DX11Base::SetPrisonerCaptureManagement(requested))
            bPrisonerCaptureManagement =
                DX11Base::IsPrisonerCaptureManagementApplied();
          NotifyFeatureToggle(u8"포로 및 처형 개선", bPrisonerCaptureManagement);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(u8"- 총대장이 포로가 되면 같은 부대의 부장도 함께 포로 처리");
          ImGui::TextUnformatted(u8"- 고립된 도시가 함락되면 그 도시에 남은 수비측 장수 전부 포로 처리");
          ImGui::TextUnformatted(u8"- AI의 포로 석방/처형 조건에 상성·의리·관계 등을 반영");
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

        // 4행: 병력수 공방 반영 / 사망장수 및 등용장수 알림
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
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(u8"사망장수 및 등용장수 알림", &bOfficerChangeNotify)) {
          NotifyFeatureToggle(u8"사망장수 및 등용장수 알림", bOfficerChangeNotify);
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"평정이 끝나고 내정으로 넘어갈 때 장수 변동을 기존 알림창으로 보여줍니다.");
          ImGui::TextUnformatted(u8"- 새로 사망 상태가 된 장수는 직전 소속 세력/도시를 함께 표시합니다.");
          ImGui::TextUnformatted(u8"- 소속 세력이 새로 생기거나 다른 세력으로 바뀐 장수는 현재 세력/도시를 표시합니다.");
          ImGui::TextUnformatted(u8"- 사망 원인이나 세력 변경 원인까지는 구분하지 않고 최종 상태를 기준으로 판정합니다.");
          ImGui::TextUnformatted(u8"- 체크 상태는 설정 파일에 저장됩니다.");
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

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"클릭하여 날씨, 일자, 지형, 여울(공성전) 등의 상세 설정을 엽니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine();
      if (ImGui::Button(u8"모든 무장 일괄 편집", ImVec2(-1, 30 * scale))) {
        bShowBatchOfficerEditWin = !bShowBatchOfficerEditWin;
      }

      if (ImGui::Button(u8"세력별 기술력 편집", ImVec2(-1, 30 * scale))) {
        bShowFactionTechEditor = !bShowFactionTechEditor;
      }

      ImGui::Spacing();

      if (ImGui::Button(u8"모든 무장 일괄 랜덤기재 부여", ImVec2(-1, 30 * scale))) {
        DX11Base::OpenBatchRandomTraitAssignmentWindow();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"모든 유효 무장의 기존 기재는 유지하고 빈 슬롯만 랜덤으로 채웁니다.");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"실행 전 황금/녹색/적색 등급을 선택할 수 있습니다.");
        ImGui::EndTooltip();
      }

      // AI 무장 자동성장: 사용자에게는 하나의 옵션만 노출하고
      // 내부적으로 게임 원본 능력성장 설정(플레이어/타 세력)도 같은 속도로 맞춘다.
      {
        static bool s_growthSettingApplied = false;

        if (bAIOfficerAutoGrowth && !s_growthSettingApplied) {
          if (iAIOfficerGrowthSpeed < 1 || iAIOfficerGrowthSpeed > 3)
            iAIOfficerGrowthSpeed = 2;
          if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
            s_growthSettingApplied = true;
          }
        }
        if (!bAIOfficerAutoGrowth)
          s_growthSettingApplied = false;

        if (ImGui::Checkbox(u8"AI 무장 자동성장", &bAIOfficerAutoGrowth)) {
          if (bAIOfficerAutoGrowth) {
            const OfficerGrowthGameSettings current =
                ReadOfficerGrowthGameSettings();

            // 게임이 이미 느림/보통/빠름이면 그 값을 초기 속도로 존중한다.
            // 둘 다 없음이면 보통으로 시작한다.
            if (current.valid) {
              bAIOfficerGrowthRestoreNone =
                  (current.playerForce == 0 && current.otherForces == 0);

              if (current.otherForces >= 1 && current.otherForces <= 3)
                iAIOfficerGrowthSpeed = current.otherForces;
              else if (current.playerForce >= 1 && current.playerForce <= 3)
                iAIOfficerGrowthSpeed = current.playerForce;
              else
                iAIOfficerGrowthSpeed = 2;
            } else {
              bAIOfficerGrowthRestoreNone = false;
              iAIOfficerGrowthSpeed = 2;
            }

            if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
              s_growthSettingApplied = true;
              AddLog(u8"[AI성장] 자동성장 ON / 게임 능력성장=%s",
                     OfficerGrowthSettingName(iAIOfficerGrowthSpeed));
            } else {
              AddLog(u8"[AI성장] 게임 능력성장 설정 적용 실패");
            }
          } else {
            s_growthSettingApplied = false;

            if (bAnnualSpecialAbilityAutoAssign) {
              bAnnualSpecialAbilityAutoAssign = false;
              AddLog(u8"[AI성장] 자동성장 OFF -> 자동 특수능력 부여도 OFF");
            }

            if (bAIOfficerGrowthRestoreNone) {
              if (WriteOfficerGrowthGameSettingsRaw(0, 0)) {
                AddLog(u8"[AI성장] 자동성장 OFF / 원래 설정이 '없음'이어서 게임 능력성장도 '없음'으로 복귀");
              } else {
                AddLog(u8"[AI성장] 자동성장 OFF / 게임 능력성장 '없음' 복귀 실패");
              }
              bAIOfficerGrowthRestoreNone = false;
            } else {
              AddLog(u8"[AI성장] 자동성장 OFF / 게임 능력성장 설정은 유지");
            }
          }
          SaveConfig();
        }
        const bool growthToggleHovered = ImGui::IsItemHovered();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bAIOfficerAutoGrowth);
        ImGui::SetNextItemWidth(90.0f * scale);

        const char *growthSpeedItems[] = {u8"느림", u8"보통", u8"빠름"};
        int speedIndex = iAIOfficerGrowthSpeed - 1;
        if (speedIndex < 0 || speedIndex > 2)
          speedIndex = 1;

        if (ImGui::Combo(u8"##AIOfficerGrowthSpeed",
                         &speedIndex,
                         growthSpeedItems,
                         IM_ARRAYSIZE(growthSpeedItems))) {
          iAIOfficerGrowthSpeed = speedIndex + 1;
          if (WriteOfficerGrowthGameSettings(iAIOfficerGrowthSpeed)) {
            s_growthSettingApplied = true;
            AddLog(u8"[AI성장] 성장 속도 변경: %s",
                   OfficerGrowthSettingName(iAIOfficerGrowthSpeed));
          } else {
            AddLog(u8"[AI성장] 성장 속도 적용 실패");
          }
          SaveConfig();
        }
        const bool growthComboHovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();

        const char *growthSpeedGuide =
            speedIndex == 0
                ? u8"장기 시나리오용"
                : (speedIndex == 2
                       ? u8"단기 시나리오 / 성장 체감 강조용"
                       : u8"추천 기본값");
        ImGui::SameLine();
        ImGui::TextDisabled(u8"- %s", growthSpeedGuide);
        const bool growthGuideHovered = ImGui::IsItemHovered();

        if (growthToggleHovered || growthComboHovered || growthGuideHovered) {
          ImGui::BeginTooltip();
          ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
          ImGui::TextUnformatted(
              u8"AI 자동성장과 게임 원본 능력성장 속도를 한 번에 제어합니다.");
          ImGui::Separator();
          ImGui::TextColored(
              ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
              u8"느림 : 장기 시나리오용");
          ImGui::TextColored(
              ImVec4(0.65f, 1.0f, 0.65f, 1.0f),
              u8"보통 : 추천 기본값");
          ImGui::TextColored(
              ImVec4(1.0f, 0.82f, 0.45f, 1.0f),
              u8"빠름 : 단기 시나리오 / 성장 체감 강조용");
          ImGui::Separator();
          ImGui::TextUnformatted(
              u8"- 게임 설정이 '없음'이면 기능을 켤 때 자동으로 '보통'으로 변경합니다.");
          ImGui::TextUnformatted(
              u8"- 원래 게임 설정이 '없음'이었다면 기능을 끌 때 다시 '없음'으로 복귀합니다.");
          ImGui::TextUnformatted(
              u8"- 원래 느림/보통/빠름이었다면 기능을 꺼도 마지막 선택 속도를 유지합니다.");
          ImGui::PopTextWrapPos();
          ImGui::EndTooltip();
        }
      }

      if (!bAIOfficerAutoGrowth)
        ImGui::BeginDisabled();

      if (ImGui::Checkbox(u8"자동 특수능력 부여", &bAnnualSpecialAbilityAutoAssign)) {
        NotifyFeatureToggle(u8"자동 특수능력 부여", bAnnualSpecialAbilityAutoAssign);
        SaveConfig();
      }
      const bool annualSpecialHovered = ImGui::IsItemHovered();

      if (!bAIOfficerAutoGrowth)
        ImGui::EndDisabled();

      if (annualSpecialHovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
        if (!bAIOfficerAutoGrowth) {
          ImGui::TextColored(
              ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
              u8"AI 무장 자동성장을 먼저 켜야 사용할 수 있습니다.");
          ImGui::Separator();
        }
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"매년 1월 평정에서 AI 자동성장이 끝난 뒤 특수 능력을 자동 판정합니다.");
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
            u8"[ 자동 기능 조합 ]");
        ImGui::TextUnformatted(
            u8"AI 성장 OFF / 특수능력 OFF : 두 기능 모두 미사용");
        ImGui::TextUnformatted(
            u8"AI 성장 ON  / 특수능력 OFF : AI 자동성장만 적용");
        ImGui::TextUnformatted(
            u8"AI 성장 ON  / 특수능력 ON  : AI 성장 후 특수능력 자동 판정");
        ImGui::TextDisabled(
            u8"AI 성장 OFF / 특수능력 ON  : 사용 불가 (특수능력 자동은 비활성화)");
        ImGui::Separator();
        ImGui::TextUnformatted(u8"- AI 자동성장 결과로 상승한 전법/능력치를 반영한 뒤 판정합니다.");
        ImGui::TextUnformatted(u8"- 이미 특수 능력을 하나라도 보유한 무장은 자동 판정에서 제외합니다.");
        ImGui::TextUnformatted(u8"- 새로 부여된 무장이 있을 때만 상단 알림과 알림 내역에 표시합니다.");
        ImGui::TextDisabled(u8"※ 수동 '모든 무장 특수 능력 자동 부여' 버튼과는 별도로 작동합니다.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }

      const bool specialAutoRunning = DX11Base::IsSpecialAbilityAutoAssignRunning();
      if (specialAutoRunning)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"모든 무장 특수 능력 자동 부여", ImVec2(-1, 30 * scale))) {
        if (DX11Base::AutoAssignSpecialAbilities())
          ImGui::OpenPopup(u8"특수 능력 자동 부여 진행###SpecialAbilityAutoAssignPopup");
      }
      if (specialAutoRunning)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 42.0f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                           u8"전체 무장 공간 1~5102의 실제 전법/특기/능력치를 분석하여 특수 능력을 자동으로 추가합니다.");
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           u8"기존에 수동으로 부여한 특수 능력은 삭제하지 않습니다.");
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), u8"[ 자동 부여 조건 ]");
        ImGui::TextUnformatted(u8"군악대 : 분기/고무 중 하나가 Lv2 이상 + 두 전법 레벨 합계 4 이상");
        ImGui::TextUnformatted(u8"무쌍 보병 : 무력 80 이상 + 강격/맹돌 중 하나 Lv2 이상 + 보병 전법 합계 + 보장×2가 9 이상");
        ImGui::TextUnformatted(u8"불꽃 기병 : 무력 80 이상 + 연격/기사 중 하나 Lv2 이상 + 기병 전법 합계 + 기장×2가 9 이상");
        ImGui::TextUnformatted(u8"원격 궁병 : 무력 75 이상 + 궁병 전법 합계 + 궁장×2가 9 이상 + 원사/시람 Lv2 이상 또는 궁병 전법 합계 8 이상");
        ImGui::TextUnformatted(u8"총사령관 : 통솔 90 이상 + 보병/기병/궁병 중 2계통 이상 전법합 6 이상 + 3병종 전법 합계 16 이상 + 군사 특기 합계 6 이상");
        ImGui::TextUnformatted(u8"기습부대 : 교란/급습/요격 중 2종 이상 보유 + 세 전법 레벨 합계 5 이상");
        ImGui::TextUnformatted(u8"대군사 : 지력 85 이상 + 열화/격류/낙석/요격 합계 7 이상 + 군사 특기 합계 5 이상 또는 신산 Lv2 이상");
        ImGui::TextUnformatted(u8"무신 : 무력 95 이상 + 보병/기병/궁병 전법 합계가 각각 5 이상 + 세 병종 총합 18 이상 + 각 병종에 Lv2 이상 전법 1개 이상");
        ImGui::TextUnformatted(u8"함선 병기화 : 함선 전법 레벨 합계 7 이상 + 수군 Lv2 이상");
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                           u8"등갑군 : 현재 자동 판정 제외 (수동 지정만 가능)");

        ImGui::Separator();
        ImGui::TextDisabled(u8"※ 전법/특기의 '합계'는 해당 항목들의 레벨 합계입니다.");
        ImGui::TextDisabled(u8"※ 기재 이름/ID는 자동 판정에 사용하지 않습니다.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }

      ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 480.0f * scale), ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal(
              u8"특수 능력 자동 부여 진행###SpecialAbilityAutoAssignPopup",
              nullptr,
              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const bool popupRunning = DX11Base::IsSpecialAbilityAutoAssignRunning();
        const char *autoStatus = DX11Base::GetSpecialAbilityAutoAssignStatus();

        ImGui::TextColored(
            ImVec4(1.0f, 0.84f, 0.0f, 1.0f),
            popupRunning ? u8"[ 특수 능력 자동 부여 진행 중 ]"
                         : u8"[ 특수 능력 자동 부여 결과 ]");
        ImGui::Separator();

        ImGui::ProgressBar(
            DX11Base::GetSpecialAbilityAutoAssignProgress(),
            ImVec2(-1.0f, 0.0f),
            autoStatus && autoStatus[0] != '\0' ? autoStatus : nullptr);

        if (popupRunning) {
          ImGui::Spacing();
          ImGui::TextDisabled(
              u8"전체 무장 공간을 분석하고 있습니다.");
          ImGui::TextDisabled(
              u8"완료 전에는 실제 특수 능력 설정을 변경하지 않습니다.");

          ImGui::Spacing();
          if (ImGui::Button(
                  u8"작업 취소",
                  ImVec2(-1.0f, 30.0f * scale))) {
            DX11Base::CancelSpecialAbilityAutoAssign();
          }
        } else {
          ImGui::Spacing();
          ImGui::TextColored(
              ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
              u8"이번 실행에서 새로 특수 능력을 부여받은 무장");
          ImGui::Separator();

          const std::size_t resultCount =
              DX11Base::GetSpecialAbilityAutoAssignResultCount();

          ImGui::BeginChild(
              "##SpecialAbilityAutoAssignResults",
              ImVec2(0.0f, 300.0f * scale),
              true,
              ImGuiWindowFlags_AlwaysVerticalScrollbar);

          if (resultCount == 0) {
            ImGui::TextDisabled(
                u8"새로 부여된 특수 능력이 없습니다.");
            ImGui::TextDisabled(
                u8"조건을 만족한 무장이 이미 해당 능력을 보유했거나 작업이 취소된 경우입니다.");
          } else {
            for (std::size_t i = 0; i < resultCount; ++i) {
              const char *line =
                  DX11Base::GetSpecialAbilityAutoAssignResultLine(i);
              if (line && line[0] != '\0')
                ImGui::TextWrapped("%s", line);
            }
          }
          ImGui::EndChild();

          ImGui::Spacing();
          if (ImGui::Button(
                  u8"닫기",
                  ImVec2(-1.0f, 32.0f * scale))) {
            ImGui::CloseCurrentPopup();
          }
        }

        ImGui::EndPopup();
      }
      ::DX11Base::DrawBatchOfficerEditWindow(scale);
      ::DX11Base::DrawFactionTechEditor(scale);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 전법 및 건물 ]");
      ImGui::SameLine();
      if (ImGui::Button(u8"수정", ImVec2(100.0f * scale, 25.0f * scale))) {
        bShowTacticsEditWin = !bShowTacticsEditWin;
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.0f, 1.0f), u8"[ 책략 ]");

      if (ImGui::Checkbox(u8"공격측 책략 게이지 시작 최대", &bMaxAttackStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxAttackStratagemGauge = !bMaxAttackStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"공격측 책략 게이지 최대", bMaxAttackStratagemGauge);
          SaveConfig();
        }
      }

      ImGui::SameLine();

      if (ImGui::Checkbox(u8"수비측 책략 게이지 시작 최대", &bMaxDefenseStratagemGauge)) {
        const bool wantHook = bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge;
        if (!DX11Base::SetStratagemGaugeCapture(wantHook)) {
          bMaxDefenseStratagemGauge = !bMaxDefenseStratagemGauge;
          AddNotification(u8"책략 게이지 기능 적용 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"수비측 책략 게이지 최대", bMaxDefenseStratagemGauge);
          SaveConfig();
        }
      }

      static bool s_stratagemFiveEnabled = true;
      if (ImGui::Checkbox(u8"5번 책략 활성화", &s_stratagemFiveEnabled)) {
        const bool enable = s_stratagemFiveEnabled;
        const bool ok = DX11Base::SetStratagemFiveFeature(enable);
        if (!ok) {
          s_stratagemFiveEnabled = !enable;
          AddNotification(enable ? u8"5번 책략 활성화 실패 - 로그 확인"
                                 : u8"5번 책략 해제 실패 - 로그 확인");
        } else {
          NotifyFeatureToggle(u8"5번 책략 활성화", s_stratagemFiveEnabled);
        }
      }

      static DX11Base::Spell5CustomSettings s_stratagem5Edit{};
      ImGui::SameLine();
      if (ImGui::Button(u8"5번 책략 설정")) {
        s_stratagem5Edit = DX11Base::GetSpell5CustomSettings();
        ImGui::OpenPopup(u8"5번 책략 설정###Stratagem5SettingsPopup");
      }

      ImGui::SetNextWindowSize(ImVec2(590.0f * scale, 570.0f * scale),
                               ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal(
              u8"5번 책략 설정###Stratagem5SettingsPopup",
              nullptr,
              ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f),
                           u8"[ 5번 책략 사용자 설정 ]");
        ImGui::Separator();
        ImGui::TextDisabled(u8"책략 ID 5/5/5와 사용횟수 1회는 고정합니다.");

        const char *targetNames[] = {u8"아군", u8"적군", u8"피아불문"};
        int targetIndex = s_stratagem5Edit.target - 1;
        if (targetIndex < 0 || targetIndex > 2)
          targetIndex = 0;
        ImGui::SetNextItemWidth(180.0f * scale);
        if (ImGui::Combo(u8"대상", &targetIndex,
                         targetNames, IM_ARRAYSIZE(targetNames))) {
          s_stratagem5Edit.target = targetIndex + 1;
        }

        static const int effectCodes[] = {0, 3, 10, 11, 12};
        const char *effectNames[] = {
            u8"없음", u8"상태이상", u8"사기 증감", u8"직접 데미지", u8"화계"};

        auto effectIndexFromCode = [&](int code) {
          for (int i = 0; i < IM_ARRAYSIZE(effectCodes); ++i)
            if (effectCodes[i] == code)
              return i;
          return 0;
        };

        auto drawEffectEditor = [&](const char *title, const char *idPrefix,
                                    int &effect, int &power, int &duration) {
          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f), "%s", title);

          int effectIndex = effectIndexFromCode(effect);
          char comboId[64] = {};
          sprintf_s(comboId, "%s_effect", idPrefix);
          ImGui::SetNextItemWidth(180.0f * scale);
          if (ImGui::Combo(comboId, &effectIndex,
                           effectNames, IM_ARRAYSIZE(effectNames))) {
            effect = effectCodes[effectIndex];
            if (effect == 0) {
              power = 0;
              duration = 0;
            } else if (effect == 3) {
              if (power < 1 || power > 3)
                power = 1;
              if (duration < 0)
                duration = 0;
            } else if (effect == 12) {
              if (power < 0 || power > 100)
                power = 100;
              duration = 0;
            } else {
              duration = 0;
            }
          }

          if (effect == 3) {
            const char *statusNames[] = {u8"저지", u8"혼란", u8"공황"};
            int statusIndex = power - 1;
            if (statusIndex < 0 || statusIndex > 2)
              statusIndex = 0;

            char statusId[64] = {};
            sprintf_s(statusId, "%s_status", idPrefix);
            ImGui::SetNextItemWidth(180.0f * scale);
            if (ImGui::Combo(statusId, &statusIndex,
                             statusNames, IM_ARRAYSIZE(statusNames))) {
              power = statusIndex + 1;
            }

            char durationId[64] = {};
            sprintf_s(durationId, "%s_duration", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(durationId, &duration, 1, 3)) {
              if (duration < 0) duration = 0;
              if (duration > 30) duration = 30;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"지속일");
          } else if (effect == 10) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_morale", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 5, 10)) {
              if (power < -100) power = -100;
              if (power > 100) power = 100;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"사기 증감");
          } else if (effect == 11) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_damage", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 50, 100)) {
              if (power < 0) power = 0;
              if (power > 10000) power = 10000;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"위력");
          } else if (effect == 12) {
            char powerId[64] = {};
            sprintf_s(powerId, "%s_fire", idPrefix);
            ImGui::SetNextItemWidth(120.0f * scale);
            if (ImGui::InputInt(powerId, &power, 5, 10)) {
              if (power < 0) power = 0;
              if (power > 100) power = 100;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(u8"발동 확률(%)");
          } else {
            ImGui::TextDisabled(u8"추가 설정 없음");
          }
        };

        drawEffectEditor(u8"효과 1", "##S5E1",
                         s_stratagem5Edit.effect1,
                         s_stratagem5Edit.power1,
                         s_stratagem5Edit.duration1);

        drawEffectEditor(u8"효과 2", "##S5E2",
                         s_stratagem5Edit.effect2,
                         s_stratagem5Edit.power2,
                         s_stratagem5Edit.duration2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(120.0f * scale);
        if (ImGui::InputInt(u8"범위##S5Range",
                            &s_stratagem5Edit.range, 1, 1)) {
          if (s_stratagem5Edit.range < 1) s_stratagem5Edit.range = 1;
          if (s_stratagem5Edit.range > 100) s_stratagem5Edit.range = 100;
        }

        ImGui::SetNextItemWidth(140.0f * scale);
        if (ImGui::InputInt(u8"추가 병력 회복##S5Heal",
                            &s_stratagem5Edit.healAmount, 100, 500)) {
          if (s_stratagem5Edit.healAmount < 0)
            s_stratagem5Edit.healAmount = 0;
          if (s_stratagem5Edit.healAmount > 65535)
            s_stratagem5Edit.healAmount = 65535;
        }
        ImGui::TextDisabled(
            u8"※ 추가 병력 회복은 우리가 별도로 넣은 기능이며, 효과1이 '사기 증가'일 때만 적용됩니다.");
        ImGui::TextDisabled(
            u8"※ 첫 번째 효과가 직접 데미지/화계 계통이면 원본 게임의 지형 판정 영향을 받을 수 있습니다.");

        ImGui::Spacing();
        if (ImGui::Button(u8"현재 기본값 복원",
                          ImVec2(150.0f * scale, 30.0f * scale))) {
          s_stratagem5Edit = DX11Base::Spell5CustomSettings{};
        }

        ImGui::Separator();

        if (ImGui::Button(u8"적용 및 저장",
                          ImVec2(180.0f * scale, 34.0f * scale))) {
          DX11Base::SetSpell5CustomSettings(s_stratagem5Edit);
          s_stratagem5Edit = DX11Base::GetSpell5CustomSettings();
          SaveConfig();
          AddNotification(u8"5번 책략 설정 적용 완료");
          ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(u8"취소",
                          ImVec2(100.0f * scale, 34.0f * scale))) {
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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

    }
  } // namespace MenuSections
} // namespace DX11Base