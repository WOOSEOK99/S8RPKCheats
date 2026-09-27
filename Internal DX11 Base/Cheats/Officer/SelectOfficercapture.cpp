#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
  std::atomic<bool> g_externalOfficerListRefreshRequested{false};
}

// 기존 대형 구현은 그대로 보존합니다. 전체 프로세스 배우자 스캐너와
// 프레임 종속 일괄 기재 창만 legacy 이름으로 격리하고 아래 T05 경로로 대체합니다.
#define StartSpouseScannerAsync StartSpouseScannerAsyncLegacy
#define DrawSpouseListWindow DrawSpouseListWindowLegacy
#define DrawOfficerListWindow DrawOfficerListWindowImpl
#define DrawBatchRandomTraitAssignmentWindow DrawBatchRandomTraitAssignmentWindowLegacy
#include "SelectOfficercapture_impl.inc"
#undef DrawBatchRandomTraitAssignmentWindow
#undef DrawOfficerListWindow
#undef DrawSpouseListWindow
#undef StartSpouseScannerAsync

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

  constexpr uintptr_t kT05CurrentRelationshipPtrOffset = 0x57AE38;
  constexpr uintptr_t kT05LegacyRelationshipPtrOffset = 0x462BA8;
  constexpr int kT05RelationshipSlotCount = 3000;
  constexpr uintptr_t kT05RelationshipStride = 0x40;

  bool T05IsRosterOfficerPtr(uintptr_t ptr, uintptr_t rosterBase) {
    if (ptr < rosterBase)
      return false;
    const uintptr_t delta = ptr - rosterBase;
    if (delta >= static_cast<uintptr_t>(5102) * 0x3D0)
      return false;
    return (delta % 0x3D0) == 0;
  }

  int T05ScoreRelationshipTable(uintptr_t base, uintptr_t rosterBase) {
    if (base <= 0x10000 || rosterBase <= 0x10000)
      return -1;

    int valid = 0;
    int invalid = 0;
    for (int i = 0; i < 128; ++i) {
      const uintptr_t slot = base + static_cast<uintptr_t>(i) * kT05RelationshipStride;
      uint8_t relation = 0;
      if (!DX11Base::UnsafeRead8(slot + 0x08, &relation))
        return -1;
      if (relation == 0)
        continue;
      if (relation < 1 || relation > 4) {
        if (++invalid > 3)
          return -1;
        continue;
      }

      uintptr_t first = 0;
      uintptr_t second = 0;
      if (!DX11Base::UnsafeReadPtr(slot + 0x10, &first) ||
          !DX11Base::UnsafeReadPtr(slot + 0x18, &second) ||
          !T05IsRosterOfficerPtr(first, rosterBase) ||
          !T05IsRosterOfficerPtr(second, rosterBase)) {
        if (++invalid > 3)
          return -1;
        continue;
      }
      ++valid;
    }
    return valid;
  }

  bool T05ResolveRelationshipTable(uintptr_t gameBase, uintptr_t rosterBase, uintptr_t& outBase) {
    outBase = 0;
    const uintptr_t offsets[] = {
        kT05CurrentRelationshipPtrOffset,
        kT05LegacyRelationshipPtrOffset
    };

    for (uintptr_t off : offsets) {
      uintptr_t ptr = 0;
      if (!DX11Base::UnsafeReadPtr(gameBase + off, &ptr) || ptr <= 0x10080)
        continue;

      const uintptr_t biases[] = {0x80, 0xC0};
      for (uintptr_t bias : biases) {
        const uintptr_t candidate = ptr - bias;
        if (T05ScoreRelationshipTable(candidate, rosterBase) >= 1) {
          outBase = candidate;
          return true;
        }
      }
    }
    return false;
  }

  bool T05BuildSpouseListDirect(uintptr_t heroAddr, std::vector<DX11Base::SpouseEntry>& outEntries) {
    outEntries.clear();
    if (heroAddr <= 0x10000)
      return false;

    unsigned short heroId = 0;
    if (!DX11Base::UnsafeRead16(heroAddr + 0x08, &heroId) || heroId < 1 || heroId > 5102)
      return false;

    const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    const uintptr_t gameBase = DX11Base::GetGameBase();
    uintptr_t rosterBase = 0;
    if (!exe || gameBase <= 0x10000 ||
        !DX11Base::TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
        rosterBase <= 0x10000) {
      return false;
    }

    const uintptr_t heroRoster =
        rosterBase + static_cast<uintptr_t>(heroId - 1) * 0x3D0;
    unsigned short verifyHeroId = 0;
    if (!DX11Base::UnsafeRead16(heroRoster + 0x08, &verifyHeroId) || verifyHeroId != heroId)
      return false;

    uintptr_t relationBase = 0;
    if (!T05ResolveRelationshipTable(gameBase, rosterBase, relationBase))
      return false;

    std::unordered_set<uintptr_t> seenSpouses;
    for (int i = 0; i < kT05RelationshipSlotCount; ++i) {
      const uintptr_t slot =
          relationBase + static_cast<uintptr_t>(i) * kT05RelationshipStride;

      uint8_t relation = 0;
      if (!DX11Base::UnsafeRead8(slot + 0x08, &relation))
        break;
      if (relation != 2)
        continue;

      uintptr_t first = 0;
      uintptr_t spousePtr = 0;
      if (!DX11Base::UnsafeReadPtr(slot + 0x10, &first) || first != heroRoster ||
          !DX11Base::UnsafeReadPtr(slot + 0x18, &spousePtr) ||
          !T05IsRosterOfficerPtr(spousePtr, rosterBase)) {
        continue;
      }

      unsigned short spouseId = 0;
      uint8_t gender = 0;
      if (!DX11Base::UnsafeRead16(spousePtr + 0x08, &spouseId) ||
          spouseId < 1 || spouseId > 5102 ||
          !DX11Base::UnsafeRead8(spousePtr + 0x30, &gender) || gender != 1 ||
          !seenSpouses.insert(spousePtr).second) {
        continue;
      }

      DX11Base::SpouseEntry entry{};
      entry.pointerAddr = slot + 0x10;
      entry.targetBase = spousePtr;
      entry.selected = false;
      memset(entry.context, 0, sizeof(entry.context));
      DX11Base::UnsafeReadMem(entry.pointerAddr - 24, entry.context, sizeof(entry.context));
      outEntries.push_back(entry);
    }

    return true;
  }

  uintptr_t g_t05BatchGameBase = 0;
  uintptr_t g_t05BatchP1 = 0;
  uintptr_t g_t05BatchRosterBase = 0;
  size_t g_t05BatchCollectCursor = 0;
  bool g_t05BatchSeenIds[DX11Base::kBatchRandomOfficerCount + 1] = {};
  ULONGLONG g_t05BatchLastTickMs = 0;
  ULONGLONG g_t05BatchLastScanMs = 0;

  void ResetT05BatchRuntime() {
    g_t05BatchGameBase = 0;
    g_t05BatchP1 = 0;
    g_t05BatchRosterBase = 0;
    g_t05BatchCollectCursor = 0;
    memset(g_t05BatchSeenIds, 0, sizeof(g_t05BatchSeenIds));
    g_t05BatchLastTickMs = 0;
    g_t05BatchLastScanMs = 0;
  }

  bool ResolveT05BatchSession() {
    const uintptr_t gameBase = DX11Base::GetGameBaseFast();
    if (gameBase <= 0x10000)
      return false;

    uintptr_t p1 = 0;
    if (!SafeTraitReadPtr(gameBase + 0xE0, p1) || p1 <= 0x10000)
      return false;

    const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    uintptr_t rosterBase = 0;
    if (!exe || !DX11Base::TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
        rosterBase <= 0x10000)
      return false;

    g_t05BatchGameBase = gameBase;
    g_t05BatchP1 = p1;
    g_t05BatchRosterBase = rosterBase;
    return true;
  }

  bool IsT05BatchSessionCurrent() {
    if (g_t05BatchGameBase <= 0x10000 || g_t05BatchP1 <= 0x10000)
      return false;
    if (DX11Base::GetGameBaseFast() != g_t05BatchGameBase)
      return false;

    uintptr_t p1 = 0;
    if (!SafeTraitReadPtr(g_t05BatchGameBase + 0xE0, p1))
      return false;
    return p1 == g_t05BatchP1;
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

  void TickBatchRandomTraitJobT05() {
    if (!s_batchRandomJob.running)
      return;

    const ULONGLONG nowMs = GetTickCount64();
    if (g_t05BatchLastTickMs != 0 && nowMs - g_t05BatchLastTickMs < 50)
      return;
    g_t05BatchLastTickMs = nowMs;

    if (!IsT05BatchSessionCurrent()) {
      s_batchRandomJob.running = false;
      s_batchRandomJob.collectingOfficers = false;
      s_batchRandomJob.scanningTraits = false;
      s_batchRandomStatus = u8"세이브/주인공 변경 감지: 일괄 기재 작업을 취소했습니다.";
      ResetT05BatchRuntime();
      return;
    }

    using Clock = std::chrono::steady_clock;

    if (s_batchRandomJob.collectingOfficers) {
      const auto deadline = Clock::now() + std::chrono::milliseconds(1);
      size_t processedThisTick = 0;
      constexpr size_t kMaxSlotsPerTick = 256;

      while (g_t05BatchCollectCursor < static_cast<size_t>(kBatchRandomOfficerCount) &&
             processedThisTick < kMaxSlotsPerTick && Clock::now() < deadline) {
        const uintptr_t base =
            g_t05BatchRosterBase + g_t05BatchCollectCursor * 0x3D0;
        ++g_t05BatchCollectCursor;
        ++processedThisTick;

        const RosterStats stats = SafeReadRosterStats(base);
        if (!stats.valid || stats.id_08 < 1 || stats.id_08 > kBatchRandomOfficerCount)
          continue;
        if (g_t05BatchSeenIds[stats.id_08])
          continue;
        if (!IsValidPtr(base + 0x10, 1))
          continue;

        g_t05BatchSeenIds[stats.id_08] = true;
        s_batchRandomJob.officers.push_back(base);
      }

      if (g_t05BatchCollectCursor < static_cast<size_t>(kBatchRandomOfficerCount))
        return;

      s_batchRandomJob.collectingOfficers = false;
      if (s_batchRandomJob.officers.empty()) {
        s_batchRandomJob.running = false;
        s_batchRandomStatus = u8"1~5102 무장 범위에서 유효 무장을 찾지 못했습니다.";
        ResetT05BatchRuntime();
        return;
      }

      s_batchRandomStatus =
          std::string(u8"유효 무장 수집 완료: ") +
          std::to_string(s_batchRandomJob.officers.size()) +
          u8"명 / 기재 객체 준비 중";
      return;
    }

    if (s_batchRandomJob.scanningTraits) {
      if (g_pendingTraitChange.active)
        return;
      if (g_t05BatchLastScanMs != 0 && nowMs - g_t05BatchLastScanMs < 200)
        return;
      g_t05BatchLastScanMs = nowMs;

      bool scanFinished = false;
      constexpr size_t kReadableBytesPerTick = 2 * 1024 * 1024;
      ScanTraitObjectsForBatchStep(
          s_batchRandomJob.requestedPool,
          s_batchRandomJob.traitObjects,
          s_batchRandomJob.traitVtable,
          s_batchRandomJob.scanAddress,
          kReadableBytesPerTick,
          scanFinished);

      if (!scanFinished)
        return;

      s_batchRandomJob.pool.clear();
      s_batchRandomJob.pool.reserve(s_batchRandomJob.requestedPool.size());
      for (uint16_t id : s_batchRandomJob.requestedPool) {
        if (s_batchRandomJob.traitObjects.count(id) != 0)
          s_batchRandomJob.pool.push_back(id);
      }

      if (s_batchRandomJob.pool.empty()) {
        s_batchRandomJob.running = false;
        s_batchRandomJob.scanningTraits = false;
        s_batchRandomStatus = u8"기재 객체 검색은 완료했지만 사용할 수 있는 기재 객체를 찾지 못했습니다.";
        ResetT05BatchRuntime();
        return;
      }

      s_batchRandomJob.scanningTraits = false;
      s_batchRandomJob.cursor = 0;
      s_batchRandomStatus =
          std::string(u8"기재 객체 검색 완료: ") +
          std::to_string(s_batchRandomJob.pool.size()) + u8"개 / 무장 적용 시작";
      return;
    }

    static std::mt19937 rng(
        static_cast<unsigned int>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    const auto deadline = Clock::now() + std::chrono::milliseconds(2);
    size_t processedThisTick = 0;
    constexpr size_t kHardOfficerCapPerTick = 64;

    while (s_batchRandomJob.cursor < s_batchRandomJob.officers.size() &&
           processedThisTick < kHardOfficerCapPerTick && Clock::now() < deadline) {
      const uintptr_t base = s_batchRandomJob.officers[s_batchRandomJob.cursor++];
      ++processedThisTick;

      const int result = AssignRandomTraitsToOneOfficerBatchFast(
          base,
          s_batchRandomJob.pool,
          s_batchRandomJob.traitObjects,
          rng,
          s_batchRandomJob.filledSlots);

      if (result < 0)
        ++s_batchRandomJob.alreadyFull;
      else if (result > 0)
        ++s_batchRandomJob.changedOfficers;
      else
        ++s_batchRandomJob.failedOfficers;
    }

    if (s_batchRandomJob.cursor >= s_batchRandomJob.officers.size()) {
      const size_t targetCount = s_batchRandomJob.officers.size();
      s_batchRandomJob.running = false;
      s_batchRandomStatus =
          std::string(u8"완료: ") +
          std::to_string(s_batchRandomJob.changedOfficers) +
          u8"명 변경 / " +
          std::to_string(s_batchRandomJob.filledSlots) +
          u8"개 슬롯 부여 / 이미 3개 보유 " +
          std::to_string(s_batchRandomJob.alreadyFull) +
          u8"명 / 미변경 " +
          std::to_string(s_batchRandomJob.failedOfficers) + u8"명";

      AddLog(u8"[랜덤기재/T05] 대상 %d명, 변경 %d명, 부여 슬롯 %d개, 이미 3개 %d명, 미변경 %d명",
             static_cast<int>(targetCount),
             s_batchRandomJob.changedOfficers,
             s_batchRandomJob.filledSlots,
             s_batchRandomJob.alreadyFull,
             s_batchRandomJob.failedOfficers);
      ResetT05BatchRuntime();
    }
  }

  void DrawOfficerListWindow(uintptr_t p1, float scale) {
    if (g_externalOfficerListRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
      s_requestOfficerListRefresh = true;
    }

    DrawOfficerListWindowImpl(p1, scale);
  }

  void StartSpouseScannerAsync() {
    if (s_isSpouseScanning.exchange(true))
      return;

    s_spouseScanProgress.store(0.0f);

    std::vector<SpouseEntry> found;
    const uintptr_t heroAddr = g_savedHeroAddr;
    const bool resolved = T05BuildSpouseListDirect(heroAddr, found);

    {
      std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
      s_spouseList = std::move(found);
    }

    s_spouseScanProgress.store(1.0f);
    s_isSpouseScanning.store(false);

    if (!resolved) {
      AddLog(u8"[배우자 검색/T05] 관계 테이블 직접 조회 실패. 전체 프로세스 스캔은 자동 실행하지 않습니다.");
      return;
    }

    AddLog(u8"[배우자 검색/T05] 관계 테이블 직접 조회 완료: %d명",
           static_cast<int>(s_spouseList.size()));
  }

  void DrawSpouseListWindow(float scale) {
    static bool s_wasSpouseListWinOpen = false;

    if (!bShowSpouseListWin) {
      if (s_wasSpouseListWinOpen) {
        s_wasSpouseListWinOpen = false;
        std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
        s_spouseList.clear();
        AddLog(u8"[배우자 검색/T05] 배우자 창 종료: 목록 캐시 정리");
      }
      return;
    }

    if (!s_wasSpouseListWinOpen) {
      s_wasSpouseListWinOpen = true;
      StartSpouseScannerAsync();
    }

    ImGui::SetNextWindowSize(ImVec2(400 * scale, 500 * scale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"주인공 배우자 목록", &bShowSpouseListWin)) {
      if (s_isSpouseScanning.load()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"배우자 관계 테이블을 확인 중입니다...");
        ImGui::ProgressBar(s_spouseScanProgress.load(), ImVec2(-1, 0));
      } else {
        std::lock_guard<std::recursive_mutex> lock(s_spouseMutex);
        if (s_spouseList.empty()) {
          ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), u8"조회된 배우자가 없습니다.");
          ImGui::TextDisabled(u8"주인공이 미혼이거나 관계 테이블을 확인하지 못했습니다.");
        } else {
          ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), u8"총 %d 명의 배우자를 찾았습니다.",
                             static_cast<int>(s_spouseList.size()));
          ImGui::Separator();
          ImGui::TextDisabled(u8"주소/ID 클릭 시 복사, 컬럼 경계 드래그로 너비 조절");

          if (ImGui::BeginTable("SpouseTable", 5,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                    ImGuiTableFlags_Resizable,
                                ImVec2(0, 300 * scale))) {
            ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 30 * scale);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150 * scale);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50 * scale);
            ImGui::TableSetupColumn(u8"슬롯", ImGuiTableColumnFlags_WidthFixed, 50 * scale);
            ImGui::TableSetupColumn(u8"이름", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < s_spouseList.size(); ++i) {
              auto &entry = s_spouseList[i];
              const uintptr_t relationAddr = entry.pointerAddr;
              const uintptr_t spouseAddr = entry.targetBase;
              unsigned short officerID = 0;
              uint32_t slotNumber = 0;
              const bool hasSlotNumber = UnsafeRead32(relationAddr + 0x28, &slotNumber);
              if (!UnsafeRead16(spouseAddr + 0x08, &officerID))
                continue;

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::PushID(static_cast<int>(i));
              ImGui::Checkbox("##sel", &entry.selected);
              ImGui::PopID();

              ImGui::TableSetColumnIndex(1);
              char addrText[32];
              snprintf(addrText, sizeof(addrText), "%p", reinterpret_cast<void *>(relationAddr));
              char addrLabel[48];
              snprintf(addrLabel, sizeof(addrLabel), "%s##addr_%d", addrText, static_cast<int>(i));
              if (ImGui::Selectable(addrLabel, false)) {
                ImGui::SetClipboardText(addrText);
                AddLog(u8"[복사] 관계 포인터 주소 복사: %s", addrText);
              }

              ImGui::TableSetColumnIndex(2);
              char idText[16];
              snprintf(idText, sizeof(idText), "%d", officerID);
              char idLabel[32];
              snprintf(idLabel, sizeof(idLabel), "%s##id_%d", idText, static_cast<int>(i));
              if (ImGui::Selectable(idLabel, false)) {
                ImGui::SetClipboardText(idText);
                AddLog(u8"[복사] 배우자 ID 복사: %s", idText);
              }

              ImGui::TableSetColumnIndex(3);
              if (hasSlotNumber)
                ImGui::Text("%u", slotNumber);
              else
                ImGui::TextUnformatted("-");

              ImGui::TableSetColumnIndex(4);
              std::string name = u8"알 수 없음";
              if (g_officerNames.count(officerID))
                name = g_officerNames[officerID];

              char label[128];
              snprintf(label, sizeof(label), "%s##%p", name.c_str(), reinterpret_cast<void *>(spouseAddr));
              if (ImGui::Selectable(label)) {
                bShowSelectedOfficerWin = true;
                g_capturedOfficerBase = spouseAddr;
                bForceCenterSelectedOfficer = true;
              }

              ImGui::PushID(static_cast<int>(i + 1000));
              if (ImGui::CollapsingHeader(u8"메모리 분석 (Hex View)")) {
                ImGui::BeginChild("HexChild", ImVec2(0, 150 * scale), true);
                ImGui::Text(u8"기준 주소(관계 포인터): %p", reinterpret_cast<void *>(relationAddr));
                ImGui::Text(u8"배우자 주소: %p", reinterpret_cast<void *>(spouseAddr));
                ImGui::Separator();

                for (int row = 0; row < 4; ++row) {
                  const int offset = (row * 16) - 24;
                  ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Offset %s%02X: ",
                                     (offset >= 0 ? "+" : "-"), abs(offset));
                  ImGui::SameLine();

                  for (int col = 0; col < 16; ++col) {
                    const int idx = row * 16 + col;
                    const uint8_t val = entry.context[idx];
                    ImVec4 color = ImVec4(1, 1, 1, 1);
                    if (idx >= 24 && idx < 32)
                      color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                    else if (idx >= 32 && idx < 40)
                      color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
                    else if (idx >= 16 && idx < 24)
                      color = ImVec4(0.4f, 0.4f, 1.0f, 1.0f);

                    ImGui::TextColored(color, "%02X", val);
                    if (col < 15)
                      ImGui::SameLine();
                  }
                }
                ImGui::EndChild();
              }
              ImGui::PopID();
            }
            ImGui::EndTable();
          }

          ImGui::Spacing();
          int selectedCount = 0;
          std::vector<int> selIndices;
          for (size_t i = 0; i < s_spouseList.size(); ++i) {
            if (s_spouseList[i].selected) {
              ++selectedCount;
              selIndices.push_back(static_cast<int>(i));
            }
          }

          if (selectedCount == 2) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
            if (ImGui::Button(u8"체크된 배우자 둘 맞바꾸기", ImVec2(-1, 30 * scale))) {
              auto &e1 = s_spouseList[selIndices[0]];
              auto &e2 = s_spouseList[selIndices[1]];

              DWORD old1 = 0;
              DWORD old2 = 0;
              const bool s1 = VirtualProtect(reinterpret_cast<LPVOID>(e1.pointerAddr), 8, PAGE_READWRITE, &old1) != FALSE;
              const bool s2 = VirtualProtect(reinterpret_cast<LPVOID>(e2.pointerAddr), 8, PAGE_READWRITE, &old2) != FALSE;

              if (s1 && s2) {
                *reinterpret_cast<uintptr_t *>(e1.pointerAddr) = e2.targetBase;
                *reinterpret_cast<uintptr_t *>(e2.pointerAddr) = e1.targetBase;

                DWORD tmp = 0;
                VirtualProtect(reinterpret_cast<LPVOID>(e1.pointerAddr), 8, old1, &tmp);
                VirtualProtect(reinterpret_cast<LPVOID>(e2.pointerAddr), 8, old2, &tmp);

                AddLog(u8"[배우자 검색/T05] 두 배우자 순서를 교환했습니다.");
                StartSpouseScannerAsync();
              } else {
                if (s1) {
                  DWORD tmp = 0;
                  VirtualProtect(reinterpret_cast<LPVOID>(e1.pointerAddr), 8, old1, &tmp);
                }
                if (s2) {
                  DWORD tmp = 0;
                  VirtualProtect(reinterpret_cast<LPVOID>(e2.pointerAddr), 8, old2, &tmp);
                }
                AddLog(u8"[ERROR] 배우자 순서 교환 실패 (메모리 접근 오류).");
              }
            }
            ImGui::PopStyleColor();
          } else {
            ImGui::BeginDisabled();
            ImGui::Button(u8"체크된 배우자 둘 맞바꾸기 (2명 선택 필요)", ImVec2(-1, 30 * scale));
            ImGui::EndDisabled();
          }
        }

        ImGui::Spacing();
        if (ImGui::Button(u8"다시 조회", ImVec2(-1, 30 * scale)))
          StartSpouseScannerAsync();
      }
    }
    ImGui::End();
  }

  void DrawBatchRandomTraitAssignmentWindow(float scale) {
    if (!s_showBatchRandomTraitWindow && !s_batchRandomJob.running)
      return;

    if (!s_showBatchRandomTraitWindow)
      return;

    ImGui::SetNextWindowSize(ImVec2(470.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"모든 무장 일괄 랜덤기재 부여###BatchRandomTraits",
                      &s_showBatchRandomTraitWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::End();
      return;
    }

    const std::vector<uint16_t> allPool = BuildBatchRandomTraitPool();
    const bool hasGreen = BatchPoolHasGrade(allPool, 2);
    const bool hasRed = BatchPoolHasGrade(allPool, 3);

    int goldCount = 0, greenCount = 0, redCount = 0;
    for (uint16_t id : allPool) {
      const int grade = GetBatchTraitGrade(id);
      if (grade == 1) ++goldCount;
      else if (grade == 2) ++greenCount;
      else if (grade == 3) ++redCount;
    }

    ImGui::TextWrapped(u8"모든 유효 무장의 기존 기재는 유지하고, 비어 있는 기재 슬롯만 중복 없이 랜덤으로 채웁니다.");
    ImGui::Spacing();
    ImGui::Text(u8"황금 후보: %d개", goldCount);
    ImGui::SameLine();
    ImGui::Text(u8"녹색: %d개", greenCount);
    ImGui::SameLine();
    ImGui::Text(u8"적색: %d개", redCount);
    ImGui::Separator();

    if (s_batchRandomJob.running)
      ImGui::BeginDisabled();

    ImGui::Checkbox(u8"황금##BatchRandomGold", &s_batchRandomGold);
    ImGui::SameLine();

    if (!hasGreen)
      ImGui::BeginDisabled();
    ImGui::Checkbox(u8"녹색##BatchRandomGreen", &s_batchRandomGreen);
    if (!hasGreen) {
      ImGui::EndDisabled();
      s_batchRandomGreen = false;
    }

    ImGui::SameLine();
    if (!hasRed)
      ImGui::BeginDisabled();
    ImGui::Checkbox(u8"적색##BatchRandomRed", &s_batchRandomRed);
    if (!hasRed) {
      ImGui::EndDisabled();
      s_batchRandomRed = false;
    }

    if (s_batchRandomJob.running)
      ImGui::EndDisabled();

    if (!HasCustomTraitConfigFile())
      ImGui::TextDisabled(u8"※ san8r_traits_config.json 없음: 기본 황금 기재 72개만 사용");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                       u8"※ 기존 기재는 유지하고 빈 슬롯만 변경합니다.");

    if (s_batchRandomJob.running) {
      if (s_batchRandomJob.collectingOfficers) {
        const float progress = static_cast<float>(g_t05BatchCollectCursor) /
                               static_cast<float>(kBatchRandomOfficerCount);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), u8"1~5102 무장 분할 수집 중...");
      } else if (s_batchRandomJob.scanningTraits) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                           u8"기재 객체 bounded scan 중... %zu / %zu개 발견",
                           s_batchRandomJob.traitObjects.size(),
                           s_batchRandomJob.requestedPool.size());
      } else {
        const float progress = s_batchRandomJob.officers.empty()
            ? 0.0f
            : static_cast<float>(s_batchRandomJob.cursor) /
              static_cast<float>(s_batchRandomJob.officers.size());

        char progressText[128];
        snprintf(progressText, sizeof(progressText),
                 u8"%zu / %zu명", s_batchRandomJob.cursor,
                 s_batchRandomJob.officers.size());
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressText);
      }

      if (ImGui::Button(u8"작업 취소", ImVec2(-1.0f, 28.0f * scale))) {
        s_batchRandomJob.running = false;
        s_batchRandomJob.collectingOfficers = false;
        s_batchRandomJob.scanningTraits = false;
        s_batchRandomStatus = u8"사용자가 일괄 기재 작업을 취소했습니다.";
        ResetT05BatchRuntime();
      }
    } else {
      const bool noGradeSelected =
          !s_batchRandomGold && !s_batchRandomGreen && !s_batchRandomRed;

      if (noGradeSelected)
        ImGui::BeginDisabled();

      if (ImGui::Button(u8"모든 무장 일괄 랜덤기재 부여 실행",
                        ImVec2(-1.0f, 34.0f * scale))) {
        std::vector<uint16_t> enabledPool;
        enabledPool.reserve(allPool.size());
        for (uint16_t id : allPool) {
          const int grade = GetBatchTraitGrade(id);
          if ((grade == 1 && s_batchRandomGold) ||
              (grade == 2 && s_batchRandomGreen) ||
              (grade == 3 && s_batchRandomRed)) {
            enabledPool.push_back(id);
          }
        }

        if (enabledPool.empty()) {
          s_batchRandomStatus = u8"선택한 등급에 사용 가능한 기재가 없습니다.";
        } else {
          s_batchRandomJob = {};
          ResetT05BatchRuntime();

          if (!ResolveT05BatchSession()) {
            s_batchRandomStatus = u8"현재 세이브/무장 배열을 확인하지 못했습니다.";
          } else {
            s_batchRandomJob.running = true;
            s_batchRandomJob.collectingOfficers = true;
            s_batchRandomJob.requestedPool = std::move(enabledPool);
            s_batchRandomJob.officers.reserve(kBatchRandomOfficerCount);

            if (!SeedTraitObjectsForBatch(
                    s_batchRandomJob.requestedPool,
                    s_batchRandomJob.traitObjects,
                    s_batchRandomJob.traitVtable)) {
              s_batchRandomJob.running = false;
              s_batchRandomJob.collectingOfficers = false;
              s_batchRandomStatus =
                  u8"기재 객체 형식(vtable)을 확인할 기준 기재를 찾지 못했습니다.";
              ResetT05BatchRuntime();
            } else {
              s_batchRandomJob.scanningTraits =
                  s_batchRandomJob.traitObjects.size() <
                  s_batchRandomJob.requestedPool.size();

              if (!s_batchRandomJob.scanningTraits)
                s_batchRandomJob.pool = s_batchRandomJob.requestedPool;
              else
                s_batchRandomJob.scanAddress = 0;

              s_batchRandomStatus = u8"1~5102 전체 무장 공간 분할 수집 시작";
            }
          }
        }
      }

      if (noGradeSelected)
        ImGui::EndDisabled();
    }

    if (!s_batchRandomStatus.empty()) {
      ImGui::Spacing();
      ImGui::TextWrapped("%s", s_batchRandomStatus.c_str());
    }

    ImGui::End();
  }

} // namespace DX11Base
