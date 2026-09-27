#include <atomic>
#include <unordered_map>
#include <vector>

namespace {
  std::atomic<bool> g_externalOfficerListRefreshRequested{false};
}

// 기존 13만 바이트 구현은 그대로 보존하고, public entry point만 wrapper로 감쌉니다.
// 내부 구현의 static 캐시는 UI 스레드에서만 접근해야 하므로 외부 worker는
// 아래 atomic 요청만 올리고 실제 refresh 플래그 전환은 DrawOfficerListWindow에서 수행합니다.
#define DrawOfficerListWindow DrawOfficerListWindowImpl
#include "SelectOfficercapture_impl.inc"
#undef DrawOfficerListWindow

namespace {

  struct PendingTraitChangeJob {
    bool active = false;
    uint64_t generation = 0;
    uintptr_t gameBase = 0;
    uintptr_t officerBase = 0;
    uint16_t officerId = 0;
    int slotIndex = -1;
    uint16_t traitId = 0;
    uintptr_t traitVtable = 0;
    uintptr_t scanAddress = 0;
    std::unordered_map<uint16_t, uintptr_t> foundObjects;
  };

  PendingTraitChangeJob g_pendingTraitChange;
  uint64_t g_traitRequestGeneration = 0;
  std::unordered_map<uint16_t, uintptr_t> g_asyncTraitObjectCache;

  uintptr_t g_lastTraitMissGameBase = 0;
  uint16_t g_lastTraitMissId = 0;
  ULONGLONG g_lastTraitMissUntilMs = 0;

  bool SafeTraitReadPtr(uintptr_t address, uintptr_t &out) {
    __try {
      out = *reinterpret_cast<uintptr_t *>(address);
      return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      out = 0;
      return false;
    }
  }

  bool SafeTraitReadU16(uintptr_t address, uint16_t &out) {
    __try {
      out = *reinterpret_cast<uint16_t *>(address);
      return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      out = 0;
      return false;
    }
  }

  uintptr_t ResolveTraitVtableForAsyncScan(uintptr_t officerBase, uintptr_t gameBase) {
    // 1) 변경 대상 무장이 이미 보유한 기재에서 클래스 vtable을 얻습니다.
    for (int slot = 0; slot < 3; ++slot) {
      uintptr_t traitObject = 0;
      uintptr_t vtable = 0;
      uint16_t traitId = 0;
      if (!SafeTraitReadPtr(officerBase + 0x88 + static_cast<uintptr_t>(slot) * 0x08, traitObject) ||
          traitObject <= 0x10000 ||
          !SafeTraitReadPtr(traitObject, vtable) || vtable <= 0x10000 ||
          !SafeTraitReadU16(traitObject + 0x08, traitId) || traitId == 0) {
        continue;
      }
      return vtable;
    }

    // 2) 대상 무장이 기재를 하나도 보유하지 않아도 런타임 기본 기재 포인터 배열에서
    // 기준 vtable만 확보할 수 있습니다. 최대 72개 포인터만 확인하므로 전체 roster 검색보다 훨씬 작습니다.
    constexpr uintptr_t kTraitPointerArrayOffset = 0x57A4D0;
    const uint16_t builtinIds[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
        31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
        51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
        61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
        201, 202};

    for (uint16_t expectedId : builtinIds) {
      uintptr_t traitObject = 0;
      uintptr_t vtable = 0;
      uint16_t actualId = 0;
      if (!SafeTraitReadPtr(
              gameBase + kTraitPointerArrayOffset +
                  static_cast<uintptr_t>(expectedId) * sizeof(uintptr_t),
              traitObject) ||
          traitObject <= 0x10000 ||
          !SafeTraitReadPtr(traitObject, vtable) || vtable <= 0x10000 ||
          !SafeTraitReadU16(traitObject + 0x08, actualId) || actualId != expectedId) {
        continue;
      }
      return vtable;
    }

    return 0;
  }

  bool IsPendingTraitTargetStillValid(const PendingTraitChangeJob &job) {
    if (!job.active || job.officerBase <= 0x10000 || job.officerId == 0)
      return false;
    if (DX11Base::GetGameBaseFast() != job.gameBase)
      return false;

    uint16_t currentOfficerId = 0;
    if (!SafeTraitReadU16(job.officerBase + 0x08, currentOfficerId))
      return false;
    return currentOfficerId == job.officerId;
  }

} // namespace

namespace DX11Base {

  void RequestOfficerListRefresh() {
    g_externalOfficerListRefreshRequested.store(true, std::memory_order_release);
  }

  bool RequestTraitChangeAsync(uintptr_t officerBase, int slotIndex, uint16_t traitID) {
    if (officerBase <= 0x10000 || slotIndex < 0 || slotIndex >= 3 || traitID == 0)
      return false;

    const uintptr_t gameBase = GetGameBaseFast();
    if (gameBase <= 0x10000)
      return false;

    uint16_t officerId = 0;
    if (!SafeTraitReadU16(officerBase + 0x08, officerId) || officerId < 1 || officerId > 5102)
      return false;

    // 이전 bounded scan에서 이미 찾은 객체라면 다시 검색하지 않고 즉시 재사용합니다.
    auto cached = g_asyncTraitObjectCache.find(traitID);
    if (cached != g_asyncTraitObjectCache.end()) {
      if (SetTraitObjectFast(officerBase, slotIndex, traitID, cached->second)) {
        AddLog(u8"[기재변경/T05] ID:%d 비동기 캐시 재사용 완료", static_cast<int>(traitID));
        return true;
      }
      g_asyncTraitObjectCache.erase(cached);
    }

    const ULONGLONG now = GetTickCount64();
    if (g_lastTraitMissGameBase == gameBase &&
        g_lastTraitMissId == traitID &&
        now < g_lastTraitMissUntilMs) {
      AddLog(u8"[기재변경/T05] ID:%d 직전 검색 미발견 - 잠시 후 재시도 가능", static_cast<int>(traitID));
      return false;
    }

    const uintptr_t traitVtable = ResolveTraitVtableForAsyncScan(officerBase, gameBase);
    if (traitVtable <= 0x10000) {
      AddLog(u8"[기재변경/T05] ID:%d 검색 기준 vtable을 확보하지 못했습니다.", static_cast<int>(traitID));
      return false;
    }

    PendingTraitChangeJob next;
    next.active = true;
    next.generation = ++g_traitRequestGeneration;
    next.gameBase = gameBase;
    next.officerBase = officerBase;
    next.officerId = officerId;
    next.slotIndex = slotIndex;
    next.traitId = traitID;
    next.traitVtable = traitVtable;
    next.scanAddress = 0;
    next.foundObjects.reserve(1);

    // latest-wins: 검색 중 같은/다른 슬롯을 다시 선택하면 과거 요청은 즉시 폐기합니다.
    g_pendingTraitChange = std::move(next);

    AddLog(u8"[기재변경/T05] ID:%d 객체 bounded scan 요청 (무장 ID:%d / 슬롯:%d)",
           static_cast<int>(traitID), static_cast<int>(officerId), slotIndex + 1);
    return true;
  }

  void TickTraitChangeAsync() {
    if (!g_pendingTraitChange.active)
      return;

    if (!IsPendingTraitTargetStillValid(g_pendingTraitChange)) {
      AddLog(u8"[기재변경/T05] 세이브/주인공/대상 변경 감지 - 이전 기재 검색 요청 폐기");
      g_pendingTraitChange = PendingTraitChangeJob{};
      return;
    }

    std::vector<uint16_t> requestedIds = {g_pendingTraitChange.traitId};
    bool finished = false;

    // 200ms maintenance에서 호출되므로 1회 최대 2MiB, 약 10MiB/s 상한으로 검색합니다.
    // 기존 8MiB/frame과 달리 FPS가 처리율을 결정하지 않습니다.
    constexpr size_t kReadableBytesPerTick = 2 * 1024 * 1024;
    ScanTraitObjectsForBatchStep(
        requestedIds,
        g_pendingTraitChange.foundObjects,
        g_pendingTraitChange.traitVtable,
        g_pendingTraitChange.scanAddress,
        kReadableBytesPerTick,
        finished);

    auto found = g_pendingTraitChange.foundObjects.find(g_pendingTraitChange.traitId);
    if (found != g_pendingTraitChange.foundObjects.end()) {
      // 검색 완료 직전에 세션/무장 ID를 다시 확인해 과거 세대 결과가 새 세이브에 적용되지 않게 합니다.
      if (!IsPendingTraitTargetStillValid(g_pendingTraitChange)) {
        AddLog(u8"[기재변경/T05] 적용 직전 대상 세대 변경 감지 - 검색 결과 폐기");
        g_pendingTraitChange = PendingTraitChangeJob{};
        return;
      }

      const uint16_t traitId = g_pendingTraitChange.traitId;
      const int slotIndex = g_pendingTraitChange.slotIndex;
      const uintptr_t officerBase = g_pendingTraitChange.officerBase;
      const uintptr_t traitObject = found->second;

      if (SetTraitObjectFast(officerBase, slotIndex, traitId, traitObject)) {
        g_asyncTraitObjectCache[traitId] = traitObject;
        AddLog(u8"[기재변경/T05] 슬롯%d → ID:%d bounded scan 완료 후 적용",
               slotIndex + 1, static_cast<int>(traitId));
        RequestOfficerListRefresh();
      } else {
        AddLog(u8"[기재변경/T05] ID:%d 검색 객체 적용 검증 실패", static_cast<int>(traitId));
      }

      g_pendingTraitChange = PendingTraitChangeJob{};
      return;
    }

    if (finished) {
      const uint16_t traitId = g_pendingTraitChange.traitId;
      g_lastTraitMissGameBase = g_pendingTraitChange.gameBase;
      g_lastTraitMissId = traitId;
      g_lastTraitMissUntilMs = GetTickCount64() + 5000;
      AddLog(u8"[기재변경/T05] ID:%d 전체 bounded scan 완료 - 객체 미발견", static_cast<int>(traitId));
      g_pendingTraitChange = PendingTraitChangeJob{};
    }
  }

  void DrawOfficerListWindow(uintptr_t p1, float scale) {
    if (g_externalOfficerListRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
      s_requestOfficerListRefresh = true;
    }

    DrawOfficerListWindowImpl(p1, scale);
  }

} // namespace DX11Base
