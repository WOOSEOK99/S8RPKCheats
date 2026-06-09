#include "../../BattleMonitor.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../debug.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../Civilian/CityData.h"
#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include "SelectOfficercapture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <unordered_map>

// 명품(Specialty) 관련 오프셋 및 데이터 구조 정리
/*
    [명품 객체 구조 - Specialty Object (Size: 0x40)]
    +0x08 : 명품 ID (uint16_t)
    +0x0E : 종류/항목 (uint8_t) - 1:명마, 2:검, 3:도, 11:활, 12:병서, 24:옥새 등
    +0x10 : 부여 특기 ID (uint8_t)
    +0x20 : 특기 레벨 (uint8_t)
    +0x22 : 상승 능력 종류 (uint8_t) - 1:통솔, 2:무력, 3:지력, 4:정치, 5:매력
    +0x23 : 능력 상승치 (uint8_t)
    +0x24 : 특수 효과 종류 (uint8_t) - 1:퇴각확실, 2:수명연장, 3:능력효과
    +0x25 : 특수 효과 값 (uint8_t)
    +0x26 : 명품 가치 (uint8_t)
    +0x30 : 소유주 포인터 (uintptr_t) - 장수 또는 도시 객체 주소
    +0x38 : 소유주 타입 (uint32_t) - 1: 장수(Officer), 2: 도시(City), 0: 없음/미정

    [도시 명품 슬롯 구조 - City Specialty Slots]
    - 도시 객체 베이스(Stride 0x2A0) 내 오프셋
    - 슬롯 위치: 0x248, 0x260, 0x278 (총 3개 슬롯)

    * 슬롯 포인터 주소(SlotPtrAddr) 기준:
      -0x08 : 슬롯 활성화 플래그 (uint32_t) - 1: 활성, 0: 비활성
      +0x00 : 명품 객체 주소 (uintptr_t) - Specialty Object Pointer
      +0x08 : 소유/구매 플래그 (uint32_t) - 1: 구매됨/장수소유, 0: 도시보유

    [기타 관련 주소]
    - 주인공(Hero) 주소: GetGameBase() + 0xE0 (포인터)

      ---- 자세한 설명 ----
    도시 객체 기준 오프셋 (명품 객체를 가리키는 포인터)
    도시 객체 베이스(크기 0x2A0) 내에는 상점에 진열되는 총 3개의 명품 슬롯이 있으며, 각 슬롯은 특정 명품 객체의 메모리
    주소를 가리킵니다.

    슬롯 1 포인터 주소: 도시 베이스 + 0x248
    슬롯 2 포인터 주소: 도시 베이스 + 0x260
    슬롯 3 포인터 주소: 도시 베이스 + 0x278
    (추가로, 각 슬롯 포인터 주소를 기준으로 -0x08은 슬롯 활성화 플래그, +0x08은 구매 완료 플래그로 사용됩니다.)

    2. 명품 객체 구조 (실제 명품 데이터)
    위의 도시 슬롯 주소를 읽어들여(포인터 참조) 찾아간 **명품 객체 베이스(크기 0x40)**에서 오프셋을 더해야 실제 명품의
   ID 및 속성을 확인할 수 있습니다.

    +0x08 : 명품 ID (2바이트, uint16_t) 👈 여기에 명품 ID가 존재합니다.
    +0x0E : 명품 종류 (1:명마, 2:검, 3:도, 11:활 등)
    +0x10 : 부여 특기 ID
    +0x20 : 특기 레벨
    +0x22 : 상승 능력 종류 (1:통솔, 2:무력, 3:지력, 4:정치, 5:매력)
    +0x23 : 능력 상승치
    +0x24 : 특수 효과 종류
    +0x26 : 명품 가치
    +0x30 : 소유주 포인터 (해당 명품을 보유한 장수 또는 도시의 객체 주소)
    +0x38 : 소유주 타입 (1: 장수, 2: 도시, 0: 없음)
*/

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

      uint8_t type = 0;
      if (!Read8(objPtr + 0x0E, &type))
        return false;

      // 종류(Type)가 1~26 사이면 유효한 명품으로 간주 (술, 보물, 기증품 등 능력치가 없는 명품도 포함)
      return (type >= 1 && type <= 26);
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
      uint16_t checkId = 0;
      uintptr_t modBase = (uintptr_t)GetModuleHandle(NULL);
      if (modBase > 0x10000) {
        TryResolveSpecialtyArrayBase(modBase, &spBase);
      }

      // [보정] 사용자 데이터 분석 결과, 포인터 체인 결과값에 0x19AF60 오프셋을 더해야 실제 배열 시작점이 나옴
      if (spBase > 0x10000) {
        if (Read16(spBase + 0x08, &checkId) && checkId != 1) {
          spBase += 0x19AF60; // 오프셋 보정
          AddLog(u8"[명품] 포인터 체인 주소 보정 적용 (+0x19AF60)");
        }
      }

      // [안전한 주소 탐지 및 Stride 탐지]
      // 포인터 체인이 여전히 틀리거나, Stride를 확인하고 싶을 때 이미 상점에 있는 명품 주소를 대조
      uintptr_t detectedStride = 0x40; // 기본값
      {
        constexpr uintptr_t kCityStride = 0x2A0;
        const uintptr_t slotOffsets[3] = {0x248, 0x260, 0x278};
        struct FoundSample {
          uintptr_t addr;
          uint16_t id;
        };
        std::vector<FoundSample> samples;

        for (int c = 0; c < g_CityCount && samples.size() < 2; c++) {
          uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;
          for (int s = 0; s < 3 && samples.size() < 2; s++) {
            uintptr_t slotPtrAddr = cityAddr + slotOffsets[s];
            uintptr_t slotObjPtr = 0;
            if (ReadPtr(slotPtrAddr, &slotObjPtr) && Ptr48(slotObjPtr) > 0x10000) {
              uint16_t spId = 0;
              if (Read16(Ptr48(slotObjPtr) + 0x08, &spId) && spId > 0 && spId <= 1000) {
                samples.push_back({Ptr48(slotObjPtr), spId});
              }
            }
          }
        }

        if (samples.size() >= 2 && samples[0].id != samples[1].id) {
          // Stride 계산: (Addr2 - Addr1) / (ID2 - ID1)
          int idDiff = (int)samples[1].id - (int)samples[0].id;
          int64_t addrDiff = (int64_t)samples[1].addr - (int64_t)samples[0].addr;
          detectedStride = (uintptr_t)(std::abs(addrDiff) / std::abs(idDiff));
          AddLog(u8"[명품] Stride 동적 탐지 성공: 0x%X (기존 0x40)", (uint32_t)detectedStride);

          // 만약 포인터 체인 결과가 여전히 틀리다면 역산으로 보정
          if (spBase <= 0x10000 || (Read16(spBase + 0x08, &checkId) && checkId != 1)) {
            spBase = samples[0].addr - ((uintptr_t)(samples[0].id - 1) * detectedStride);
            AddLog(u8"[명품] 역산 탐지로 베이스 주소 최종 교정: %p", (void *)spBase);
          }
        } else if (samples.size() == 1 && (spBase <= 0x10000 || (Read16(spBase + 0x08, &checkId) && checkId != 1))) {
          // 샘플이 하나라도 있으면 최소한의 역산 시도
          spBase = samples[0].addr - ((uintptr_t)(samples[0].id - 1) * 0x40);
          AddLog(u8"[명품] 단일 샘플 역산 탐지 성공: %p", (void *)spBase);
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
            alreadyAssigned.insert(Ptr48(slotObjPtr));
          }
        }
      }

      // 2. 수집할 명품 풀 (ID 1 ~ 300 범위 탐색)
      std::vector<uintptr_t> pool;
      for (int i = 1; i <= 300; i++) {
        uintptr_t objPtr = spBase + ((uintptr_t)(i - 1) * detectedStride);

        // 이미 어떤 도시 슬롯에라도 배치되어 있다면 제외 (중복 배분 방지)
        if (alreadyAssigned.count(objPtr) > 0)
          continue;

        uint16_t readId = 0;
        if (Read16(objPtr + 0x08, &readId) && readId == i) {
          uintptr_t ownerPtr = 0;
          if (ReadPtr(objPtr + 0x30, &ownerPtr)) {
            ownerPtr = Ptr48(ownerPtr);
            // 소유주가 없는(0) 아이템 혹은 발견되지 않은 아이템을 배분 대상으로 함
            if (ownerPtr <= 0x10000 && HasSpecialtyAttributes(objPtr)) {
              // 추가로 정의 파일에 이름이 있는 경우에만 배분 (예: 124, 130 등 빈 이름 제외)
              auto it = s_specialityNameById.find((int)readId);
              if (it != s_specialityNameById.end() && !it->second.empty()) {
                pool.push_back(objPtr);
              }
            }
          }
        }
      }

      if (pool.empty()) {
        AddLog(u8"[명품자동배분] 배분할 수 있는 유효한 명품(주인 없는 물건)이 없습니다.");
        return;
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

          // 슬롯 포인터가 비어있거나, 활성화되지 않았거나, 품절된 경우 빈 슬롯으로 간주
          bool hasPtr = ReadPtr(slotAddr, &slotObjPtr) && Ptr48(slotObjPtr) > 0x10000;
          bool okEnabled = Read32(slotAddr - 0x08, &enabled);
          bool isEnabled = okEnabled && enabled != 0;

          uint32_t bought = 0;
          bool okBought = Read32(slotAddr + 0x08, &bought);
          bool isBought = okBought && bought != 0;

          // [개선] 품절된 슬롯(isBought)도 빈자리로 취급하여 새로 채웁니다.
          if (!hasPtr || !isEnabled || isBought) {
            emptySlots.push_back({cityAddr, slotAddr, s});
          }
        }
      }

      AddLog(u8"[명품배분] 가용 명품:%d개, 빈 슬롯:%d개", (int)pool.size(), (int)emptySlots.size());

      if (emptySlots.empty()) {
        AddLog(u8"[명품자동배분] 도시 슬롯에 남는 자리가 없습니다.");
        return;
      }

      // 4. 랜덤 셔플 및 배분
      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(pool.begin(), pool.end(), g);
      std::shuffle(emptySlots.begin(), emptySlots.end(), g);

      int assignCount = (int)(std::min)(pool.size(), emptySlots.size());
      int successCount = 0;
      for (int i = 0; i < assignCount; i++) {
        uintptr_t spObj = pool[i];
        EmptySlot &target = emptySlots[i];

        bool ok = true;
        // 도시 슬롯에 명품 주소 쓰기
        ok &= WritePtrSafe(target.slotPtrAddr, spObj);

        // [수정] 명품 객체 자체의 소유주 정보(0x30, 0x38)는 건드리지 않음
        // 상점 상업 시스템은 소유주가 없는(0) 아이템을 구매 대상으로 처리하므로,
        // 여기서 도시 주소를 직접 써버리면 구매 버튼 클릭 시 시스템 충돌(프리징)이 발생할 수 있음.

        // 슬롯 활성화 플래그 (Enabled=1, Bought=0)
        ok &= Write32Safe(target.slotPtrAddr - 0x08, 1);
        ok &= Write32Safe(target.slotPtrAddr + 0x08, 0);

        if (ok)
          successCount++;
      }

      AddLog(u8"[명품자동배분] 배분 완료: 총 %d개를 비어있던 도시 슬롯에 배치했습니다. (중복 배분 방지 적용)",
             successCount);
    }
  } // namespace

  void UpdateAutoSpecialtyDistribution(bool isCouncil) {
    if (!bAutoFillSpecialties)
      return;

    static bool s_lastCouncil = false;

    // 평정 종료 시점 감지 (Council: true -> false)
    if (!isCouncil && s_lastCouncil) {
      if (IsInBattle()) {
        AddLog(u8"[자동화] 평정 종료가 감지되었으나, 현재 전투 중이므로 명품 배분을 건너뜁니다.");
      } else {
        AddLog(u8"[자동화] 평정 종료 감지 -> 남는 명품 자동 배분 시작");
        AssignRandomSpecialtiesToEmptySlots();
      }
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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.0f));
    if (ImGui::Button(u8"명품 즉시 자동 배분 실행")) {
      AssignRandomSpecialtiesToEmptySlots();
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

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
          if (WritePtrSafe(s_selectedSpecialtyObj + 0x30, heroBase) && Write32Safe(s_selectedSpecialtyObj + 0x38, 1) &&
              (s_selectedSpecialtySlotAddr <= 0x10000 || Write32Safe(s_selectedSpecialtySlotAddr + 0x08, 1))) {
            AddLog(u8"[명품] 주인공에게 소유 완료 (타입:장수)");
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
                ImGui::TextDisabled("No.%u ?", (unsigned)specialityNo);
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
                Write32Safe(s_selectedSpecialtyObj + 0x38, 1) &&
                (s_selectedSpecialtySlotAddr <= 0x10000 || Write32Safe(s_selectedSpecialtySlotAddr + 0x08, 1))) {
              AddLog(u8"[명품] %s(ID:%d)에게 수여 완료 (타입:장수)", g_officerNames[s_giveOfficerSelectedId].c_str(),
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
