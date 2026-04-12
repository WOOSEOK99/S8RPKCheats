#include "Cheats.h"
#include "Cheats/OfficerRosterResolve.h"
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
#include <random>
#include <set>
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
      s_specialityDefsLoaded = true;
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

    static bool ResolveOwnerNameFromObj(uintptr_t objPtr, std::string &outName, uintptr_t cityArrayBase = 0) {
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

      // 도시 배열 베이스가 있으면 도시 주소인지 먼저 확인
      if (cityArrayBase > 0x10000) {
        constexpr uintptr_t kCityStride = 0x2A0;
        for (int c = 0; c < g_CityCount; c++) {
          if (ownerPtr == cityArrayBase + (uintptr_t)c * kCityStride) {
            outName = std::string(g_CityList[c].cityname) + u8" (도시)";
            return true;
          }
        }
      }

      uint16_t ownerId = 0;
      if (Read16(ownerPtr + 0x08, &ownerId) && ownerId >= 1 && ownerId <= 5102) {
        auto it = g_officerNames.find((int)ownerId);
        if (it != g_officerNames.end()) {
          outName = it->second;
          return true;
        } else {
          char buf[24];
          snprintf(buf, sizeof(buf), u8"ID:%u", (unsigned)ownerId);
          outName = buf;
          return true;
        }
      }
      return false;
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
          {1, u8"과감"},   {10, u8"발명"},  {13, u8"과감"},  {14, u8"과감"}, {16, u8"여력"},
          {17, u8"여력"},  {24, u8"여력"},  {30, u8"여력"},  {32, u8"과감"}, {53, u8"과감"},
          {54, u8"과감"},  {66, u8"축성"},  {71, u8"경작"},  {78, u8"화술"}, {87, u8"열변"},
          {104, u8"신산"}, {105, u8"귀모"}, {107, u8"행군"}, {108, u8"행군"}};

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

    static bool HasSpecialtyAttributes(uintptr_t objPtr) {
      if (objPtr <= 0x10000)
        return false;

      uint8_t skill = 0, ability = 0, effect = 0;
      Read8(objPtr + 0x10, &skill);
      Read8(objPtr + 0x22, &ability);
      Read8(objPtr + 0x24, &effect);

      // 정의된 맵에 ID가 존재하는 경우에만 유효한 속성으로 간주 (단순 0 체크 지양)
      static const std::set<uint8_t> validSkills = {1,  10, 13, 14, 16, 17,  24,  30,  32,  53,
                                                    54, 66, 71, 78, 87, 104, 105, 107, 108, 184};
      static const std::set<uint8_t> validAbilities = {1, 2, 3, 4, 5};
      static const std::set<uint8_t> validEffects = {1, 2, 3};

      return (validSkills.count(skill) > 0 || validAbilities.count(ability) > 0 || validEffects.count(effect) > 0);
    }

    static uintptr_t s_lastResolvedSpBase = 0;

    static void AssignRandomSpecialtiesToEmptySlots() {
      uintptr_t cityArrayBase = ResolveCityArrayBase();
      if (cityArrayBase <= 0x10000) {
        AddLog(u8"[명품자동배분] 도시 데이터 베이스를 찾기 못했습니다.");
        return;
      }

      // 1. 명품 베이스 주소 확보 (포인터 체인 우선)
      uintptr_t spBase = 0;
      uintptr_t exeBase = GetGameBase();
      if (exeBase > 0x10000) {
        TryResolveSpecialtyArrayBase(exeBase, &spBase);
      }

      // 포인터 체인이 실패하면 기존 역산 로직 사용
      if (spBase <= 0x10000) {
        constexpr uintptr_t kCityStride = 0x2A0;
        const uintptr_t slotOffsets[3] = {0x248, 0x260, 0x278};
        bool foundBase = false;
        for (int c = 0; c < g_CityCount && !foundBase; c++) {
          uintptr_t cityAddr = cityArrayBase + c * kCityStride;
          for (int s = 0; s < 3 && !foundBase; s++) {
            uintptr_t slotPtr = 0;
            if (ReadPtr(cityAddr + slotOffsets[s], &slotPtr)) {
              slotPtr = Ptr48(slotPtr);
              if (slotPtr > 0x10000) {
                uint16_t spId = 0;
                if (Read16(slotPtr + 0x08, &spId) && spId > 0 && spId <= 300) {
                  spBase = slotPtr - ((uintptr_t)(spId - 1) * 0x40);
                  foundBase = true;
                }
              }
            }
          }
        }
      }

      if (spBase <= 0x10000) {
        AddLog(u8"[명품자동배분] 명품 데이터 베이스를 찾지 못했습니다. (데이터 로딩 대기 중)");
        return;
      }
      s_lastResolvedSpBase = spBase;

      // 이름 정의 로드 확인
      LoadSpecialityDefinitionsIfNeeded();

      constexpr uintptr_t kCityStride = 0x2A0;
      const uintptr_t slotOffsets[3] = {0x248, 0x260, 0x278};

      // [추가] 현재 도시 슬롯에 이미 배치된 명품 주소들을 모두 수집 (중복 배분 방지)
      std::set<uintptr_t> alreadyAssigned;
      for (int c = 0; c < g_CityCount; c++) {
        uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;
        for (int s = 0; s < 3; s++) {
          uintptr_t slotAddr = cityAddr + slotOffsets[s];
          uintptr_t slotObjPtr = 0;
          if (ReadPtr(slotAddr, &slotObjPtr) && Ptr48(slotObjPtr) > 0x10000) {
            uintptr_t p = Ptr48(slotObjPtr);
            uint16_t spId = 0;
            // 소모품(250~254)은 중복 배분 허용하므로 체크에서 제외
            if (Read16(p + 0x08, &spId) && (spId < 250 || spId > 254)) {
              alreadyAssigned.insert(p);
            }
          }
        }
      }

      // 2. 소유주가 없는 명품 수집 (ID 1 ~ 300 범위 탐색)
      std::vector<uintptr_t> unownedSpecialties;
      for (int i = 1; i <= 300; i++) {
        // 이름 정의가 없는 ID는 빈 슬롯/더미로 간주하여 제외
        if (s_specialityNameById.find(i) == s_specialityNameById.end())
          continue;

        uintptr_t objPtr = spBase + ((uintptr_t)(i - 1) * 0x40);

        // 소모품(250~254)은 중복 사용이 가능하므로 이미 배치 여부 검사를 건너뜀
        if (i < 250 || i > 254) {
          if (alreadyAssigned.count(objPtr) > 0)
            continue;
        }

        uint16_t readId = 0;
        if (Read16(objPtr + 0x08, &readId) && readId == i) {
          uintptr_t ownerPtr = 0;
          if (ReadPtr(objPtr + 0x30, &ownerPtr)) {
            ownerPtr = Ptr48(ownerPtr);
            // 소유주가 없거나(0), 소유주 포인터가 가리키는 대상이 무효할 때
            if (ownerPtr <= 0x10000) {
              unownedSpecialties.push_back(objPtr);
            }
          }
        }
      }

      // (고유 명품이 없더라도 빈 슬롯/무효 아이템 정리를 위해 계속 진행)
      if (unownedSpecialties.empty()) {
        // AddLog(u8"[명품자동배분] 배분할 새 고유 명품이 없습니다. 빈자리 정리 모드로 진행합니다.");
      }

      // 3. 비어있는 도시 슬롯 수집
      struct EmptySlot {
        uintptr_t cityAddr;
        uintptr_t slotPtrAddr;
        int slotIdx;
      };
      std::vector<EmptySlot> emptySlots;
      for (int c = 0; c < g_CityCount; c++) {
        uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;
        for (int s = 0; s < 3; s++) {
          uintptr_t slotAddr = cityAddr + slotOffsets[s];
          uintptr_t slotObjPtr = 0;
          uint32_t enabled = 0;

          // 슬롯 포인터가 비어있거나, 활성화되지 않았거나, 이름이 없는 무효 아이템인 경우 빈 슬롯으로 간주
          bool hasPtr = ReadPtr(slotAddr, &slotObjPtr) && Ptr48(slotObjPtr) > 0x10000;
          bool okEnabled = Read32(slotAddr - 0x08, &enabled);
          bool isEnabled = okEnabled && enabled != 0;

          bool hasName = false;
          if (hasPtr && isEnabled) {
            uint16_t spIdFromObj = 0;
            if (Read16(Ptr48(slotObjPtr) + 0x08, &spIdFromObj)) {
              hasName = (s_specialityNameById.find((int)spIdFromObj) != s_specialityNameById.end());
            }
          }

          // 포인터가 없거나, 활성화가 꺼져있거나, 이름이 없는 경우 모두 빈 슬롯으로 취급
          if (!hasPtr || !isEnabled || !hasName) {
            emptySlots.push_back({cityAddr, slotAddr, s});
          }
        }
      }

      if (emptySlots.empty()) {
        AddLog(u8"[명품자동배분] 도시 슬롯에 남는 자리가 없습니다.");
        return;
      }

      // 4. 랜덤 셔플 및 배분
      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(unownedSpecialties.begin(), unownedSpecialties.end(), g);
      std::shuffle(emptySlots.begin(), emptySlots.end(), g);

      int assignCount = (int)(std::min)(unownedSpecialties.size(), emptySlots.size());
      int successCount = 0;
      for (int i = 0; i < assignCount; i++) {
        uintptr_t spObj = unownedSpecialties[i];
        EmptySlot &target = emptySlots[i];

        bool ok = true;
        // 도시 슬롯에 명품 주소 쓰기
        ok &= WritePtrSafe(target.slotPtrAddr, spObj);
        // 명품 객체에 소유주(도시) 주소 쓰기
        ok &= WritePtrSafe(spObj + 0x30, target.cityAddr);

        // [수정] 소유주 타입 플래그 설정 (City=2) - 구매 시 프리징 해결 시도
        ok &= Write32Safe(spObj + 0x38, 2);

        // 슬롯 활성화 플래그 (Enabled=1, Bought=0)
        ok &= Write32Safe(target.slotPtrAddr - 0x08, 1);
        ok &= Write32Safe(target.slotPtrAddr + 0x08, 0);

        if (ok)
          successCount++;
      }

      // [추가] 남은 빈 슬롯이 있으면 소모품(251~254)으로 채우기
      if (successCount < (int)emptySlots.size()) {
        std::vector<int> consumableIds = {251, 252, 253, 254};
        std::uniform_int_distribution<int> dist(0, (int)consumableIds.size() - 1);

        for (int i = successCount; i < (int)emptySlots.size(); i++) {
          EmptySlot &target = emptySlots[i];
          int pickedId = consumableIds[dist(g)];
          uintptr_t spObj = spBase + ((uintptr_t)(pickedId - 1) * 0x40);

          WritePtrSafe(target.slotPtrAddr, spObj);
          WritePtrSafe(spObj + 0x30, target.cityAddr);
          Write32Safe(spObj + 0x38, 2);              // City Type
          Write32Safe(target.slotPtrAddr - 0x08, 1); // Enabled
          Write32Safe(target.slotPtrAddr + 0x08, 0); // Not Bought
        }
      }

      AddLog(u8"[명품자동배분] 배분 결과: 총 %d개 명품을 빈 슬롯에 배치했습니다.", successCount);
    }
  } // namespace

  void UpdateAutoSpecialtyDistribution(bool isCouncil) {
    if (!bAutoFillSpecialties)
      return;

    static bool s_lastCouncil = false;

    // 평정 종료 시점 감지 (Council: true -> false)
    if (!isCouncil && s_lastCouncil) {
      AddLog(u8"[자동화] 평정 종료 감지 -> 남는 명품 자동 배분 시작");
      AssignRandomSpecialtiesToEmptySlots();
    }
    s_lastCouncil = isCouncil;
  }

  void DrawSpecialtyInfoWindow(float scale) {
    if (!bShowSpecialtyInfoWin)
      return;
    LoadSpecialityDefinitionsIfNeeded();
    ImGui::SetNextWindowSize(ImVec2(700 * scale, 760 * scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"명품 정보###SpecialtyInfoWin", &bShowSpecialtyInfoWin)) {
      ImGui::End();
      return;
    }

    // 상단 수동 조작 영역
    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.0f));
    // if (ImGui::Button(u8"명품 강제 무작위 배분")) {
    //   AssignRandomSpecialtiesToEmptySlots();
    // }
    // ImGui::PopStyleColor();

    // ImGui::SameLine();
    // if (ImGui::Button(u8"명품 정보 캐시 초기화")) {
    //   s_lastResolvedSpBase = 0;
    //   AddLog(u8"[명품] 명품 베이스 주소 캐시를 초기화했습니다.");
    // }
    // ImGui::Separator();

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
        // 슬롯별 정보를 상세히 표시하기 위해 컬럼 구성을 단순화 (도시 + 슬롯 1,2,3)
        if (ImGui::BeginTable("CitySpecialtyOverview", 4, flags, ImVec2(0, 620 * scale))) {
          ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed, 90 * scale);
          ImGui::TableSetupColumn(u8"슬롯 1", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn(u8"슬롯 2", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn(u8"슬롯 3", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableHeadersRow();

          for (int cityIdx = 0; cityIdx < g_CityCount; cityIdx++) {
            uintptr_t cityAddr = cityArrayBase + (uintptr_t)cityIdx * kCityStride;

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

              ImGui::TableSetColumnIndex(slot + 1);
              // [중요] 게임상에서 활성화되지 않은 슬롯은 데이터가 있어도 빈 슬롯(-)으로 표시
              if (!hasData || !isEnabled) {
                ImGui::TextDisabled("-");
                continue;
              }

              std::string specialityName;
              uint16_t specialityNo = 0;
              bool nameResolved = ResolveSpecialityNameAndNo(slotObjPtr, specialityName, &specialityNo);

              // 이름이 없는 경우 (fallback No.XXX 포함) ? 로 표시
              if (!nameResolved || specialityName.empty()) {
                ImGui::TextDisabled("?");
                continue;
              }

              bool isSelected = (s_selectedSpecialtyObj == slotObjPtr);
              char label[128];
              snprintf(label, sizeof(label), "%s##sp_%d_%d", specialityName.c_str(), cityIdx, slot);

              // 1. 선택 가능한 텍스트 (셀 전체 영역)
              float startX = ImGui::GetCursorPosX();
              float columnWidth = ImGui::GetColumnWidth();

              // 등급별 색상 선정 (가치 50+ 는 분홍색, 속성보유는 노란색)
              uint8_t spValue = 0;
              Read8(slotObjPtr + 0x26, &spValue);
              bool hasAttr = HasSpecialtyAttributes(slotObjPtr);

              if (spValue >= 50) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.9f, 1.0f)); // 분홍색
              } else if (hasAttr) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f)); // 황금색
              }

              ImGui::BeginGroup();
              if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_AllowItemOverlap)) {
                s_selectedSpecialtyObj = slotObjPtr;
                s_selectedSpecialtySlotAddr = slotAddr;
                s_selectedSpecialtyName = specialityName;
                s_selectedSpecialtyDesc = ResolveSpecialityDescByNo(specialityNo);
              }

              if (spValue >= 50 || hasAttr) {
                ImGui::PopStyleColor();
              }

              // 2. 우측 상태 태그 (Selectable 위에 겹쳐서 출력)
              ImGui::SameLine();

              std::string statusText;
              ImVec4 statusColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
              bool isNormalStatus = true;

              if (isBought) {
                std::string ownerName;
                if (ResolveOwnerNameFromObj(slotObjPtr, ownerName, cityArrayBase)) {
                  statusText = u8"[소유:" + ownerName + u8"]";
                  statusColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
                } else {
                  statusText = u8"[구매완료]";
                  statusColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                }
              } else if (isEnabled) {
                statusText = u8"[구매가능]";
                statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
              } else {
                statusText = u8"[-]";
                isNormalStatus = false;
              }

              if (isNormalStatus) {
                float textWidth = ImGui::CalcTextSize(statusText.c_str()).x;
                ImGui::SetCursorPosX(startX + columnWidth - textWidth - 5.0f * scale);
                ImGui::TextColored(statusColor, "%s", statusText.c_str());
              }
              ImGui::EndGroup();
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
            ImGui::TableSetupColumn(u8"위치/소유자", ImGuiTableColumnFlags_WidthStretch, 1.2f);
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

              // 등급별 색상 선정
              uint8_t spValue = 0;
              Read8(objPtr + 0x26, &spValue);
              bool hasAttr = HasSpecialtyAttributes(objPtr);

              if (spValue >= 50) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.9f, 1.0f)); // 분홍색
              } else if (hasAttr) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f)); // 황금색
              }

              if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns)) {
                s_selectedSpecialtyObj = objPtr;
                s_selectedSpecialtySlotAddr = 0;
                s_selectedSpecialtyName = name;
                s_selectedSpecialtyDesc = ResolveSpecialityDescByNo(spId);
              }

              if (spValue >= 50 || hasAttr) {
                ImGui::PopStyleColor();
              }

              ImGui::TableSetColumnIndex(2);
              std::string ownerStr = u8"없음";
              if (!ResolveOwnerNameFromObj(objPtr, ownerStr, cityArrayBase)) {
                ownerStr = u8"없음";
              }
              ImGui::TextUnformatted(ownerStr.c_str());
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
