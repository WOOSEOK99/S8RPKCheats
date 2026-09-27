#include "StartSetting.h"
#include "../../Cheats.h"
#include "../../NotificationManager.h"
#include <atomic>
#include <string>

namespace DX11Base {
  // SelectOfficercapture.cpp의 UI-thread refresh bridge.
  // worker에서는 캐시/vector를 직접 만지지 않고 atomic 요청만 올립니다.
  void RequestOfficerListRefresh();

  static void StartSettingAddNotificationProxy(const std::string &message) {
    AddNotification(message);

    // 미발견→재야 일괄 보정이 실제로 끝난 시점에만 모든 무장 목록 캐시를 무효화합니다.
    // 시작 알림("변경중")에는 refresh를 요청하지 않아 불필요한 재스캔을 피합니다.
    if (message.find(u8"명의 미발견 무장이 재야로 변경되었습니다.") != std::string::npos) {
      RequestOfficerListRefresh();
    }
  }

  // 기존 StartSetting worker의 메뉴→0/0→실제연도 추적 경로는 더 이상
  // 미발견→재야 기능에 사용하지 않습니다. 시나리오 수정 자체의 worker는 그대로 유지합니다.
  bool bUndiscoveredToRoninLegacyGuard = false;
}

#define AddNotification StartSettingAddNotificationProxy
#define bUndiscoveredToRonin bUndiscoveredToRoninLegacyGuard
#define SetUndiscoveredToRonin LegacySetUndiscoveredToRonin
#include "StartSetting_impl.inc"
#undef SetUndiscoveredToRonin
#undef bUndiscoveredToRonin
#undef AddNotification

namespace DX11Base {
  static std::atomic<uint64_t> s_undiscoveredRequestVersion{0};
  static std::atomic<uint64_t> s_undiscoveredProcessedVersion{0};

  void RequestUndiscoveredToRoninScan(const char *reason) {
    if (!bUndiscoveredToRonin)
      return;

    const uint64_t version =
        s_undiscoveredRequestVersion.fetch_add(1, std::memory_order_acq_rel) + 1;
    AddLog(u8"[미발견보정] 1회 검사 요청 #%llu (%s)",
           static_cast<unsigned long long>(version),
           reason ? reason : "unknown");
  }

  void ProcessPendingUndiscoveredToRoninScan(uintptr_t p1) {
    if (!bUndiscoveredToRonin)
      return;

    const uint64_t requested =
        s_undiscoveredRequestVersion.load(std::memory_order_acquire);
    if (requested == 0 ||
        requested == s_undiscoveredProcessedVersion.load(std::memory_order_acquire)) {
      return;
    }

    // P1이 새 세이브의 실제 무장 데이터로 유효해질 때까지 요청을 소비하지 않습니다.
    if (p1 <= 0x10000 || !IsValidPtr(p1, 0x100))
      return;

    uintptr_t root = ResolveRoot();
    if (!root)
      return;

    uintptr_t baseAddr = root + 0x1d4560;
    if (!IsValidPtr(baseAddr, 0x100))
      return;

    // 나이/등장연도 보정은 기존 동작을 그대로 유지하므로 현재 시나리오 연도가 필요합니다.
    unsigned short currentYear = 0;
    uint8_t currentMonth = 0;
    if (!ReadScenarioDate(&currentYear, &currentMonth))
      return;
    if (currentYear < 100 || currentYear >= 400)
      return;

    AddLog(u8"[미발견보정] 요청 #%llu 처리 시작: p1=%p year=%u month=%u",
           static_cast<unsigned long long>(requested),
           (void *)p1, (unsigned)currentYear, (unsigned)currentMonth);

    const int targetYear = static_cast<int>(currentYear);
    const int stride = 0x3D0;
    const int maxOfficers = 1000;
    int countModified = 0;
    int countRonin = 0;

    for (int i = 0; i < maxOfficers; ++i) {
      // 사용자가 OFF했거나 처리 도중 더 최신 로드 요청이 들어오면 오래된 검사를 중단합니다.
      if (!bUndiscoveredToRonin)
        return;
      if (s_undiscoveredRequestVersion.load(std::memory_order_acquire) != requested)
        return;

      uintptr_t offPtr = baseAddr + (i * stride);
      if (!IsValidPtr(offPtr, 0x40))
        break;

      const uint16_t id = *(uint16_t *)(offPtr + 0x08);
      if (id == 0 || id > 2000)
        continue;

      uint8_t *pStatus = (uint8_t *)(offPtr + 0x10);
      const uint8_t status = *pStatus;

      bool modified = false;
      if (status == 0x68 || status == 0x78) {
        WriteByte((uintptr_t)pStatus, 0x58);
        modified = true;
        ++countRonin;
      }

      uint16_t *pAppearYear = (uint16_t *)(offPtr + 0x32);
      uint16_t *pBirthYear = (uint16_t *)(offPtr + 0x34);
      uint16_t *pDeathYear = (uint16_t *)(offPtr + 0x36);

      if (IsValidPtr((uintptr_t)pAppearYear, 6)) {
        if (*pAppearYear > targetYear && *pAppearYear < 400) {
          *pAppearYear = (uint16_t)targetYear;
          modified = true;
        }
        if (*pBirthYear >= (targetYear - 15) && *pBirthYear < 400) {
          *pBirthYear = (uint16_t)(targetYear - 20);
          modified = true;
        }
        if (*pDeathYear <= targetYear && *pDeathYear != 0) {
          *pDeathYear = (uint16_t)(targetYear + 50);
          modified = true;
        }
      }

      if (modified)
        ++countModified;
    }

    if (!bUndiscoveredToRonin ||
        s_undiscoveredRequestVersion.load(std::memory_order_acquire) != requested) {
      return;
    }

    s_undiscoveredProcessedVersion.store(requested, std::memory_order_release);

    AddLog(u8"[미발견보정] 요청 #%llu 완료: 재야 변경 %d명 / 전체 보정 %d명",
           static_cast<unsigned long long>(requested), countRonin, countModified);

    if (countModified > 0) {
      AddNotification(std::to_string(countModified) +
                      u8"명의 미발견 무장이 재야로 변경되었습니다.");
    } else {
      AddLog(u8"[미발견보정] 현재 세이브에는 보정할 미발견 무장이 없습니다.");
    }
  }

  void SetUndiscoveredToRonin(bool enable) {
    if (enable) {
      AddLog(u8"[미발견보정] 기능 활성화 - 현재/다음 로드 데이터 1회 검사 예약");
      RequestUndiscoveredToRoninScan("checkbox/config ON");
    } else {
      // 아직 처리되지 않은 요청을 즉시 무효화합니다.
      const uint64_t requested =
          s_undiscoveredRequestVersion.load(std::memory_order_acquire);
      s_undiscoveredProcessedVersion.store(requested, std::memory_order_release);
      AddLog(u8"[미발견보정] 기능 비활성화 - 대기 요청 취소");
    }
  }
}
