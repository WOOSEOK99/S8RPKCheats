#include "Cheats.h"
#include "CityData.h"
#include "MenuState.h"
#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include "SelectOfficercapture.h"
#include "debug.h"
#include "pch.h"
#include "showlog.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

// 한블럭의 크기 0x40(64byte)
// 명품 시작주소 0x1F1B30DC968

namespace DX11Base {
  extern HMODULE g_hModule;
  void AddLog(const char *fmt, ...);

  namespace {
    static bool s_specialityDefsLoaded = false;
    static std::unordered_map<int, std::string> s_specialityNameById;
    static std::unordered_map<int, std::string> s_specialityDescById;
    static uintptr_t s_selectedSpecialtyObj = 0;
    static uintptr_t s_selectedSpecialtySlotAddr = 0;
    static std::string s_selectedSpecialtyName;
    static std::string s_selectedSpecialtyDesc;
    static int s_giveOfficerSelectedId = 0;
    static bool s_showGiveOfficerListWindow = false;

    static bool Read8(uintptr_t addr, uint8_t *out) {
      __try {
        *out = *(uint8_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool Read16(uintptr_t addr, uint16_t *out) {
      __try {
        *out = *(uint16_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool Read32(uintptr_t addr, uint32_t *out) {
      __try {
        *out = *(uint32_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool ReadPtr(uintptr_t addr, uintptr_t *out) {
      __try {
        *out = *(uintptr_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static uintptr_t Ptr48(uintptr_t v) { return (uintptr_t)(v & 0x0000FFFFFFFFFFFFULL); }

    static bool WritePtrSafe(uintptr_t addr, uintptr_t value) {
      DWORD old = 0;
      if (!VirtualProtect((LPVOID)addr, sizeof(uintptr_t), PAGE_READWRITE, &old))
        return false;
      bool ok = false;
      __try {
        *(uintptr_t *)addr = value;
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }
      DWORD dummy = 0;
      VirtualProtect((LPVOID)addr, sizeof(uintptr_t), old, &dummy);
      return ok;
    }
    static bool Write32Safe(uintptr_t addr, uint32_t value) {
      DWORD old = 0;
      if (!VirtualProtect((LPVOID)addr, sizeof(uint32_t), PAGE_READWRITE, &old))
        return false;
      bool ok = false;
      __try {
        *(uint32_t *)addr = value;
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }
      DWORD dummy = 0;
      VirtualProtect((LPVOID)addr, sizeof(uint32_t), old, &dummy);
      return ok;
    }

    static uintptr_t ResolveCityArrayBase() {
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      if (exeBase <= 0x10000)
        return 0;
      uintptr_t p1 = 0, p2 = 0, cityArrayBase = 0;
      if (!ReadPtr(exeBase + 0x34C8630, &p1) || p1 <= 0x10000)
        return 0;
      if (!ReadPtr(p1, &p2) || p2 <= 0x10000)
        return 0;
      if (!ReadPtr(p2, &cityArrayBase) || cityArrayBase <= 0x10000)
        return 0;
      return cityArrayBase;
    }

    static void LoadSpecialityDefinitionsIfNeeded() {
      if (s_specialityDefsLoaded)
        return;
      s_specialityDefsLoaded = true;
      s_specialityNameById.clear();
      s_specialityDescById.clear();

      char path[MAX_PATH] = {};
      if (!GetModuleFileNameA(g_hModule, path, MAX_PATH))
        return;
      std::filesystem::path modDir = std::filesystem::path(path).parent_path();
      std::vector<std::filesystem::path> candidates = {
          modDir / "speciality_definitions.json", modDir / "specialty_definitions.json",
          modDir / "release" / "speciality_definitions.json", modDir / "release" / "specialty_definitions.json"};

      std::ifstream file;
      std::filesystem::path loadedPath;
      for (const auto &p : candidates) {
        file.open(p.string(), std::ios::in);
        if (file.is_open()) {
          loadedPath = p;
          break;
        }
      }
      if (!file.is_open()) {
        AddLog(u8"[명품] 정의 파일을 찾지 못했습니다. speciality_definitions.json");
        return;
      }

      std::string line;
      int currentId = -1;
      std::string currentName;
      std::string currentDesc;
      while (std::getline(file, line)) {
        if (line.find("{") != std::string::npos) {
          currentId = -1;
          currentName.clear();
          currentDesc.clear();
        }
        size_t idPos = line.find("\"id\"");
        if (idPos != std::string::npos) {
          size_t colon = line.find(":", idPos);
          if (colon != std::string::npos) {
            try {
              currentId = std::stoi(line.substr(colon + 1));
            } catch (...) {
              currentId = -1;
            }
          }
        }
        size_t namePos = line.find("\"name\"");
        if (namePos != std::string::npos) {
          size_t colon = line.find(":", namePos);
          if (colon != std::string::npos) {
            size_t q1 = line.find("\"", colon);
            if (q1 != std::string::npos) {
              size_t q2 = line.find("\"", q1 + 1);
              if (q2 != std::string::npos)
                currentName = line.substr(q1 + 1, q2 - q1 - 1);
            }
          }
        }
        size_t descPos = line.find("\"description\"");
        if (descPos != std::string::npos) {
          size_t colon = line.find(":", descPos);
          if (colon != std::string::npos) {
            size_t q1 = line.find("\"", colon);
            if (q1 != std::string::npos) {
              size_t q2 = line.rfind("\"");
              if (q2 != std::string::npos && q2 > q1)
                currentDesc = line.substr(q1 + 1, q2 - q1 - 1);
            }
          }
        }
        if (line.find("}") != std::string::npos && currentId > 0 && !currentName.empty()) {
          s_specialityNameById[currentId] = currentName;
          s_specialityDescById[currentId] = currentDesc;
        }
      }
      file.close();
      AddLog(u8"[명품] 정의 로드 완료: %s (%d개)", loadedPath.string().c_str(), (int)s_specialityNameById.size());
    }

    static bool ResolveSpecialityNameAndNo(uintptr_t objPtr, std::string &outName, uint16_t *outNo) {
      outName.clear();
      if (outNo)
        *outNo = 0;
      objPtr = Ptr48(objPtr);
      if (objPtr <= 0x10000)
        return false;
      uint16_t no = 0;
      if (!Read16(objPtr + 0x08, &no))
        return false;
      if (outNo)
        *outNo = no;
      auto it = s_specialityNameById.find((int)no);
      if (it == s_specialityNameById.end())
        return false;
      outName = it->second;
      return true;
    }

    static std::string ResolveSpecialityDescByNo(uint16_t no) {
      auto it = s_specialityDescById.find((int)no);
      if (it == s_specialityDescById.end())
        return std::string();
      return it->second;
    }

    static bool ResolveOwnerNameFromObj(uintptr_t objPtr, std::string &outName) {
      outName.clear();
      objPtr = Ptr48(objPtr);
      if (objPtr <= 0x10000)
        return false;
      uintptr_t ownerPtr = 0;
      if (!ReadPtr(objPtr + 0x30, &ownerPtr))
        return false;
      ownerPtr = Ptr48(ownerPtr);
      if (ownerPtr <= 0x10000)
        return false;
      uint16_t ownerId = 0;
      if (!Read16(ownerPtr + 0x08, &ownerId) || ownerId < 1 || ownerId > 5102)
        return false;
      auto it = g_officerNames.find((int)ownerId);
      if (it != g_officerNames.end())
        outName = it->second;
      else {
        char buf[24];
        snprintf(buf, sizeof(buf), u8"ID:%u", (unsigned)ownerId);
        outName = buf;
      }
      return true;
    }

    static uintptr_t ResolveOfficerBaseById(uint16_t officerId) {
      if (officerId < 1 || officerId > 5102)
        return 0;
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      uintptr_t rosterBase = 0;
      if (!TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) || rosterBase <= 0x10000)
        return 0;

      uintptr_t fastBase = rosterBase + (uintptr_t)(officerId - 1) * 0x3D0;
      uint16_t checkId = 0;
      if (Read16(fastBase + 0x08, &checkId) && checkId == officerId)
        return fastBase;

      // fallback: ID 인덱스가 다를 때 선형 탐색
      for (int i = 0; i < 5102; i++) {
        uintptr_t base = rosterBase + (uintptr_t)i * 0x3D0;
        if (Read16(base + 0x08, &checkId) && checkId == officerId)
          return base;
      }
      return 0;
    }

    static std::string GetSpecialtyAttributesString(uintptr_t objPtr) {
      if (objPtr <= 0x10000)
        return "";

      uint8_t type = 0, skill = 0, skillLv = 0, ability = 0, abilityVal = 0, effect = 0, effectVal = 0, value = 0;
      Read8(objPtr + 0x0E, &type);
      Read8(objPtr + 0x10, &skill); // 말그림
      Read8(objPtr + 0x20, &skillLv);
      Read8(objPtr + 0x22, &ability);
      Read8(objPtr + 0x23, &abilityVal);
      Read8(objPtr + 0x24, &effect); //??
      Read8(objPtr + 0x25, &effectVal);
      Read8(objPtr + 0x26, &value);

      static const std::unordered_map<uint8_t, const char *> typeMap = {
          {1, u8"명마"},  {2, u8"검"},     {3, u8"도"},    {4, u8"대도"},  {5, u8"창"},    {6, u8"극"},
          {7, u8"도끼"},  {8, u8"채찍"},   {9, u8"둔기"},  {10, u8"암기"}, {11, u8"활"},   {12, u8"병서"},
          {13, u8"정서"}, {14, u8"사서"},  {15, u8"경서"}, {16, u8"논문"}, {17, u8"의서"}, {18, u8"기서"},
          {19, u8"지도"}, {20, u8"장식"},  {21, u8"보물"}, {22, u8"약"},   {23, u8"술"},   {24, u8"옥새"},
          {25, u8"기타"}, {26, u8"기증품"}};

      static const std::unordered_map<uint8_t, const char *> skillMap = {
          {1, u8"과감"}, {18, u8"과감"}, {40, u8"과감"}, {22, u8"여력"}, {29, u8"여력"}, {35, u8"여력"}};

      static const std::unordered_map<uint8_t, const char *> abilityMap = {
          {1, u8"통솔"}, {2, u8"무력"}, {3, u8"지력"}, {4, u8"정치"}, {5, u8"매력"}};

      static const std::unordered_map<uint8_t, const char *> effectMap = {
          {1, u8"퇴각확실"}, {2, u8"수명연장"}, {3, u8"능력효과"}};

      std::string result;
      char buf[256];

      // 종류
      auto itType = typeMap.find(type);
      snprintf(buf, sizeof(buf), u8"종류 : %s", itType != typeMap.end() ? itType->second : u8"--");
      result += buf;

      // 특기
      auto itSkill = skillMap.find(skill);
      if (itSkill != skillMap.end()) {
        snprintf(buf, sizeof(buf), u8" , 특기 : %s +%u", itSkill->second, (unsigned)skillLv);
      } else {
        snprintf(buf, sizeof(buf), u8" , 특기 : --");
      }
      result += buf;

      // 능력
      auto itAbility = abilityMap.find(ability);
      if (itAbility != abilityMap.end()) {
        snprintf(buf, sizeof(buf), u8" , 능력 : %s +%u", itAbility->second, (unsigned)abilityVal);
      } else {
        snprintf(buf, sizeof(buf), u8" , 능력 : --");
      }
      result += buf;

      // 효과
      auto itEffect = effectMap.find(effect);
      if (itEffect != effectMap.end()) {
        snprintf(buf, sizeof(buf), u8" , 효과 : %s", itEffect->second);
      } else {
        snprintf(buf, sizeof(buf), u8" , 효과 : --");
      }
      result += buf;

      // 가치
      snprintf(buf, sizeof(buf), u8" , 가치 : %u", (unsigned)value);
      result += buf;

      return result;
    }
  } // namespace

  void DrawSpecialtyInfoWindow(float scale) {
    if (!bShowSpecialtyInfoWin)
      return;
    LoadSpecialityDefinitionsIfNeeded();

    ImGui::SetNextWindowSize(ImVec2(600 * scale, 760 * scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"명품 정보###SpecialtyInfoWin", &bShowSpecialtyInfoWin)) {
      ImGui::End();
      return;
    }

    // ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), u8"[ 도시별 명품 보유 현황 ]");
#if 0
    ImGui::SameLine();
    static std::string s_testResultString;
    if (ImGui::Button(u8"테스트 (최초 명품 찾기)")) {
      s_testResultString.clear();
      uintptr_t cityArrayBase = ResolveCityArrayBase();
      if (cityArrayBase > 0x10000) {
        bool found = false;
        const uintptr_t kStride = 0x2A0;
        const uintptr_t slots[3] = {0x248, 0x260, 0x278};
        for (int c = 0; c < g_CityCount && !found; c++) {
          uintptr_t cityAddr = cityArrayBase + c * kStride;
          for (int s = 0; s < 3 && !found; s++) {
            uintptr_t slotPtr = 0;
            if (ReadPtr(cityAddr + slots[s], &slotPtr)) {
              slotPtr = Ptr48(slotPtr);
              if (slotPtr > 0x10000) {
                uint16_t spId = 0;
                if (Read16(slotPtr + 0x08, &spId) && spId > 0) {
                  uintptr_t baseAddr = slotPtr - ((uintptr_t)(spId - 1) * 0x40);
                  char buf[256];
                  snprintf(buf, sizeof(buf), u8"[1번 명품 주소: %p] 역산 성공! (사용된 단서 - ID:%d, 객체:%p)", 
                           (void*)baseAddr, spId, (void*)slotPtr);
                  s_testResultString = buf;
                  AddLog(u8"[테스트] %s", buf);
                  found = true;
                }
              }
            }
          }
        }
        if (!found) {
          s_testResultString = u8"모든 도시를 탐색했지만 유효한 명품을 발견하지 못했습니다.";
          AddLog(u8"[테스트] 모든 도시를 탐색했지만 유효한 명품을 발견하지 못했습니다.");
        }
      } else {
        s_testResultString = u8"도시 배열 베이스 주소를 찾지 못함 (오류)";
        AddLog(u8"[테스트] 도시 배열 베이스 주소를 찾지 못함");
      }
    }
    if (!s_testResultString.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", s_testResultString.c_str());
    }
#endif
    // ImGui::Separator();

    // 다른 창과 비슷한 주황 계열 섹션 스타일
    ImGui::BeginChild("SpecialtyActionSection", ImVec2(0, 140 * scale), true);
    if (s_selectedSpecialtyObj > 0x10000) {
      ImGui::Text(u8"선택 명품: %s",
                  s_selectedSpecialtyName.empty() ? u8"(이름 미확인)" : s_selectedSpecialtyName.c_str());
      if (bShowDebug) {
        char addrHex[32];
        snprintf(addrHex, sizeof(addrHex), "%p", (void *)s_selectedSpecialtyObj);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(객체 %s)", addrHex);
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"복사##SpecialtyObjAddr")) {
          ImGui::SetClipboardText(addrHex);
          AddLog(u8"[복사] 명품 객체 주소: %s", addrHex);
        }
      }
      if (!s_selectedSpecialtyDesc.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        ImGui::TextWrapped(u8"설명: %s", s_selectedSpecialtyDesc.c_str());
        ImGui::PopStyleColor();
      }

      std::string attrStr = GetSpecialtyAttributesString(s_selectedSpecialtyObj);
      if (!attrStr.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", attrStr.c_str());
      }

      ImGui::Separator();
      ImGui::Spacing();

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.22f, 0.05f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.48f, 0.30f, 0.08f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.36f, 0.10f, 1.0f));
      if (ImGui::Button(u8"선택 장수에게 수여", ImVec2(170 * scale, 0)))
        s_showGiveOfficerListWindow = true;
      ImGui::SameLine();
      if (ImGui::Button(u8"주인공에게 소유", ImVec2(150 * scale, 0))) {
        uintptr_t gameBase = GetGameBase();
        uintptr_t heroBase = 0;
        if (gameBase > 0x10000)
          ReadPtr(gameBase + 0xE0, &heroBase);
        heroBase = Ptr48(heroBase);
        if (heroBase > 0x10000) {
          if (WritePtrSafe(s_selectedSpecialtyObj + 0x30, heroBase) &&
              (s_selectedSpecialtySlotAddr <= 0x10000 || Write32Safe(s_selectedSpecialtySlotAddr + 0x08, 1))) {
            AddLog(u8"[명품] 주인공에게 소유 완료");
          } else {
            AddLog(u8"[명품] 주인공 소유 실패");
          }
        } else {
          AddLog(u8"[명품] 주인공 주소를 찾지 못했습니다.");
        }
      }
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
      ImGui::TextWrapped(u8"* 명품 수여 또는 소유시 반드시 저장후 재 로딩 해야 적용됩니다.");
      ImGui::PopStyleColor(4);
    } else {
      ImGui::TextDisabled(u8"표에서 명품을 클릭하면 수여 기능을 사용할 수 있습니다.");
      s_showGiveOfficerListWindow = false;
    }
    ImGui::EndChild();
    ImGui::Spacing();

    uintptr_t cityArrayBase = ResolveCityArrayBase();
    if (cityArrayBase <= 0x10000) {
      ImGui::TextDisabled(u8"도시 데이터 해석 실패");
      ImGui::End();
      return;
    }

    constexpr uintptr_t kCityStride = 0x2A0;
    const uintptr_t slotOffsets[3] = {0x248, 0x260, 0x278};
    if (ImGui::BeginTabBar("SpecialtyTabs")) {
      if (ImGui::BeginTabItem(u8"도시별 명품 보유 현황")) {
        ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("CitySpecialtyOverview", 6, flags, ImVec2(0, 620 * scale))) {
          ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed, 90 * scale);
          ImGui::TableSetupColumn(u8"슬롯1", ImGuiTableColumnFlags_WidthStretch, 1.2f);
          ImGui::TableSetupColumn(u8"슬롯2", ImGuiTableColumnFlags_WidthStretch, 1.2f);
          ImGui::TableSetupColumn(u8"슬롯3", ImGuiTableColumnFlags_WidthStretch, 1.2f);
          ImGui::TableSetupColumn(u8"구매가능", ImGuiTableColumnFlags_WidthFixed, 85 * scale);
          ImGui::TableSetupColumn(u8"구매완료", ImGuiTableColumnFlags_WidthStretch, 1.5f);
          ImGui::TableHeadersRow();

          for (int cityIdx = 0; cityIdx < g_CityCount; cityIdx++) {
            uintptr_t cityAddr = cityArrayBase + (uintptr_t)cityIdx * kCityStride;
            bool hasBuyable = false;
            bool hasBought = false;
            std::vector<std::string> boughtOwners;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", g_CityList[cityIdx].cityname);

            for (int slot = 0; slot < 3; slot++) {
              uintptr_t slotAddr = cityAddr + slotOffsets[slot];
              uint32_t enabled = 0, bought = 0;
              uintptr_t slotObjPtr = 0;
              bool okEnabled = Read32(slotAddr - 0x8, &enabled);
              bool okBought = Read32(slotAddr + 0x8, &bought);
              bool okSlotPtr = ReadPtr(slotAddr, &slotObjPtr);
              if (okSlotPtr)
                slotObjPtr = Ptr48(slotObjPtr);

              bool hasData = okSlotPtr && slotObjPtr > 0x10000;
              bool isEnabled = okEnabled && enabled != 0;
              bool isBought = okBought && bought != 0;
              if (hasData && isEnabled && !isBought)
                hasBuyable = true;
              if (isBought)
                hasBought = true;

              ImGui::TableSetColumnIndex(slot + 1);
              if (!hasData) {
                ImGui::TextUnformatted(u8"-");
                continue;
              }

              std::string specialityName;
              uint16_t specialityNo = 0;
              ResolveSpecialityNameAndNo(slotObjPtr, specialityName, &specialityNo);
              char label[256];
              if (!specialityName.empty())
                snprintf(label, sizeof(label), "%s##sp_%d_%d", specialityName.c_str(), cityIdx, slot);
              else if (specialityNo > 0)
                snprintf(label, sizeof(label), "No.%u##sp_%d_%d", (unsigned)specialityNo, cityIdx, slot);
              else
                snprintf(label, sizeof(label), u8"판매중##sp_%d_%d", cityIdx, slot);
              if (ImGui::Selectable(label, false, 0)) {
                s_selectedSpecialtyObj = slotObjPtr;
                s_selectedSpecialtySlotAddr = slotAddr;
                s_selectedSpecialtyName = specialityName.empty()
                                              ? std::string(label).substr(0, std::string(label).find("##"))
                                              : specialityName;
                s_selectedSpecialtyDesc = ResolveSpecialityDescByNo(specialityNo);
              }

              if (isBought) {
                std::string ownerText;
                if (ResolveOwnerNameFromObj(slotObjPtr, ownerText) && !ownerText.empty()) {
                  bool exists = false;
                  for (const auto &n : boughtOwners) {
                    if (n == ownerText) {
                      exists = true;
                      break;
                    }
                  }
                  if (!exists)
                    boughtOwners.push_back(ownerText);
                }
              }
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(hasBuyable ? u8"구매가능" : u8"-");
            ImGui::TableSetColumnIndex(5);
            if (hasBought) {
              std::string ownersJoined;
              for (size_t i = 0; i < boughtOwners.size(); i++) {
                if (i > 0)
                  ownersJoined += ", ";
                ownersJoined += boughtOwners[i];
              }
              if (!ownersJoined.empty())
                ImGui::Text(u8"소유자:%s", ownersJoined.c_str());
              else
                ImGui::TextUnformatted(u8"구매완료");
            } else {
              ImGui::TextUnformatted(u8"-");
            }
          }
          ImGui::EndTable();
        }
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem(u8"명품별 소유 현황")) {
        static uintptr_t s_spBaseCache = 0;
        if (s_spBaseCache <= 0x10000 && cityArrayBase > 0x10000) {
          bool found = false;
          for (int c = 0; c < g_CityCount && !found; c++) {
            uintptr_t cityAddr = cityArrayBase + c * kCityStride;
            for (int s = 0; s < 3 && !found; s++) {
              uintptr_t slotPtr = 0;
              if (ReadPtr(cityAddr + slotOffsets[s], &slotPtr)) {
                slotPtr = Ptr48(slotPtr);
                if (slotPtr > 0x10000) {
                  uint16_t spId = 0;
                  if (Read16(slotPtr + 0x08, &spId) && spId > 0) {
                    s_spBaseCache = slotPtr - ((uintptr_t)(spId - 1) * 0x40);
                    found = true;
                  }
                }
              }
            }
          }
        }

        if (s_spBaseCache > 0x10000) {
          ImGuiTableFlags flags2 =
              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
          if (ImGui::BeginTable("SpecialtyAllList", 3, flags2, ImVec2(0, 620 * scale))) {
            ImGui::TableSetupColumn(u8"ID", ImGuiTableColumnFlags_WidthFixed, 40 * scale);
            ImGui::TableSetupColumn(u8"명품 이름", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(u8"위치/소유자", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableHeadersRow();

            std::vector<int> sortedIds;
            for (const auto &kv : s_specialityNameById)
              sortedIds.push_back(kv.first);
            std::sort(sortedIds.begin(), sortedIds.end());

            for (int spId : sortedIds) {
              uintptr_t objPtr = s_spBaseCache + ((uintptr_t)(spId - 1) * 0x40);
              uint16_t readId = 0;
              if (!Read16(objPtr + 0x08, &readId) || readId != spId)
                continue;

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::Text("%d", spId);

              ImGui::TableSetColumnIndex(1);
              std::string name = s_specialityNameById[spId];
              char label[256];
              snprintf(label, sizeof(label), "%s##allsp_%d", name.c_str(), spId);
              if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns)) {
                s_selectedSpecialtyObj = objPtr;
                s_selectedSpecialtySlotAddr = 0;
                s_selectedSpecialtyName = name;
                s_selectedSpecialtyDesc = ResolveSpecialityDescByNo(spId);
              }

              ImGui::TableSetColumnIndex(2);
              std::string ownerStr = u8"없음";
              uintptr_t ownerPtr = 0;
              if (ReadPtr(objPtr + 0x30, &ownerPtr)) {
                ownerPtr = Ptr48(ownerPtr);
                if (ownerPtr > 0x10000) {
                  bool isCity = false;
                  for (int c = 0; c < g_CityCount; c++) {
                    if (ownerPtr == cityArrayBase + c * kCityStride) {
                      ownerStr = std::string(g_CityList[c].cityname) + u8" (도시)";
                      isCity = true;
                      break;
                    }
                  }
                  if (!isCity) {
                    uint16_t ownerId = 0;
                    if (Read16(ownerPtr + 0x08, &ownerId) && ownerId >= 1 && ownerId <= 5102) {
                      if (g_officerNames.count((int)ownerId)) {
                        ownerStr = g_officerNames[(int)ownerId];
                      } else {
                        ownerStr = u8"장수:" + std::to_string(ownerId);
                      }
                    } else {
                      ownerStr = u8"알수없음";
                    }
                  }
                }
              }
              ImGui::TextUnformatted(ownerStr.c_str());

              // 설명 컬럼 제거됨
            }
            ImGui::EndTable();
          }
        } else {
          ImGui::TextDisabled(u8"명품 데이터 베이스를 동적으로 계산하지 못했습니다.");
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();

    if (s_showGiveOfficerListWindow && s_selectedSpecialtyObj > 0x10000) {
      static char officerSearch[64] = {};
      ImGui::SetNextWindowSize(ImVec2(520 * scale, 420 * scale), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(u8"수여 대상 장수 선택###GiveOfficerListWin", &s_showGiveOfficerListWindow)) {
        ImGui::TextUnformatted(u8"수여할 장수를 선택하세요");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##officerSearchWindow", u8"이름/ID 검색", officerSearch, sizeof(officerSearch));
        ImGui::Separator();

        std::vector<std::pair<int, std::string>> officers;
        officers.reserve(g_officerNames.size());
        for (const auto &kv : g_officerNames)
          officers.push_back(kv);
        std::sort(officers.begin(), officers.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

        if (ImGui::BeginChild("OfficerSelectListWindow", ImVec2(0, 290 * scale), true)) {
          for (const auto &kv : officers) {
            const int id = kv.first;
            const std::string &name = kv.second;
            if (officerSearch[0] != '\0') {
              std::string idStr = std::to_string(id);
              if (idStr.find(officerSearch) == std::string::npos && name.find(officerSearch) == std::string::npos)
                continue;
            }
            char label[320];
            snprintf(label, sizeof(label), "%d - %s", id, name.c_str());
            if (ImGui::Selectable(label, s_giveOfficerSelectedId == id))
              s_giveOfficerSelectedId = id;
          }
        }
        ImGui::EndChild();

        if (ImGui::Button(u8"선택 장수에게 수여 실행", ImVec2(190 * scale, 0))) {
          uintptr_t targetBase = ResolveOfficerBaseById((uint16_t)s_giveOfficerSelectedId);
          if (targetBase > 0x10000) {
            if (WritePtrSafe(s_selectedSpecialtyObj + 0x30, targetBase) &&
                (s_selectedSpecialtySlotAddr <= 0x10000 || Write32Safe(s_selectedSpecialtySlotAddr + 0x08, 1))) {
              AddLog(u8"[명품] %s(ID:%d)에게 수여 완료", g_officerNames[s_giveOfficerSelectedId].c_str(),
                     s_giveOfficerSelectedId);
              s_showGiveOfficerListWindow = false;
            } else {
              AddLog(u8"[명품] 장수 수여 실패");
            }
          } else {
            AddLog(u8"[명품] 대상 장수 주소를 찾지 못했습니다. (ID:%d)", s_giveOfficerSelectedId);
          }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"닫기", ImVec2(90 * scale, 0)))
          s_showGiveOfficerListWindow = false;
      }
      ImGui::End();
    }
  }
} // namespace DX11Base
