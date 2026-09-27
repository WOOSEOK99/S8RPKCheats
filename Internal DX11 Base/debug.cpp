#define NOMINMAX
#include "pch.h"
#include "debug.h"
#include "Cheats.h"
#include "Cheats\Social\InstantLoveCave.h"
#include "Cheats\System\MonthCapture.h"
#include "Cheats\Officer\OfficerDetail.h"
#include "Cheats\Officer\SelectOfficercapture.h"
#include "Cheats\Officer\OfficerRosterResolve.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuState.h"
#include "showcal.h"
#include "showlog.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct T05DebugWorker {
  std::thread thread;
  std::shared_ptr<std::atomic<bool>> done;
};

std::mutex g_t05DebugWorkersMutex;
// 정상 FreeLibrary 경로에서는 join+delete하고, 프로세스 종료에서는 loader lock
// 아래 joinable thread destructor를 피하기 위해 raw pointer만 정적 보관합니다.
std::vector<T05DebugWorker*> g_t05DebugWorkers;

// 커스텀 배우자 관계 진단용 임시 타깃.
// 이번 테스트 케이스: 주인공 3001 / 게임 UI상 배우자 3012.
constexpr uint16_t kT05CustomSpouseDiagHeroId = 3001;
constexpr uint16_t kT05CustomSpouseDiagTargetId = 3012;
std::atomic<uint16_t> g_t05CustomSpouseDiagCompletedHeroId{0};
std::atomic<bool> g_t05CustomSpouseDiagRunning{false};

struct T05DebugBytePool {
  unsigned char* data = nullptr;
  size_t capacity = 0;

  ~T05DebugBytePool() {
    ::operator delete(data);
  }
};

thread_local T05DebugBytePool g_t05DebugBytePool;

unsigned char* AcquireT05DebugBytes(size_t count) {
  if (g_t05DebugBytePool.data && g_t05DebugBytePool.capacity >= count) {
    unsigned char* p = g_t05DebugBytePool.data;
    g_t05DebugBytePool.data = nullptr;
    g_t05DebugBytePool.capacity = 0;
    return p;
  }
  return static_cast<unsigned char*>(::operator new(count));
}

void ReleaseT05DebugBytes(unsigned char* data, size_t count) noexcept {
  if (!data)
    return;

  if (!g_t05DebugBytePool.data || count > g_t05DebugBytePool.capacity) {
    ::operator delete(g_t05DebugBytePool.data);
    g_t05DebugBytePool.data = data;
    g_t05DebugBytePool.capacity = count;
    return;
  }

  ::operator delete(data);
}

template <class T>
struct T05DebugAllocator {
  using value_type = T;

  T05DebugAllocator() noexcept = default;
  template <class U>
  T05DebugAllocator(const T05DebugAllocator<U>&) noexcept {}

  T* allocate(size_t count) {
    if constexpr (std::is_same_v<T, unsigned char>)
      return reinterpret_cast<T*>(AcquireT05DebugBytes(count));
    return std::allocator<T>{}.allocate(count);
  }

  void deallocate(T* p, size_t count) noexcept {
    if constexpr (std::is_same_v<T, unsigned char>)
      ReleaseT05DebugBytes(reinterpret_cast<unsigned char*>(p), count);
    else
      std::allocator<T>{}.deallocate(p, count);
  }

  template <class U>
  bool operator==(const T05DebugAllocator<U>&) const noexcept { return true; }
  template <class U>
  bool operator!=(const T05DebugAllocator<U>&) const noexcept { return false; }
};

constexpr uintptr_t kT05PointerMask = 0x0000FFFFFFFFFFFFULL;

uintptr_t T05NormalizePtr(uintptr_t p) {
  return p & kT05PointerMask;
}

bool T05ReadProcessBytes(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;

  SIZE_T read = 0;
  return ReadProcessMemory(
             GetCurrentProcess(),
             reinterpret_cast<LPCVOID>(addr),
             out, size, &read) != FALSE &&
         read == size;
}

template <class T>
bool T05ReadProcessValue(uintptr_t addr, T* out) {
  return out && T05ReadProcessBytes(addr, out, sizeof(T));
}

uint16_t T05ReadOfficerIdFromAnyPtr(uintptr_t rawPtr) {
  const uintptr_t p = T05NormalizePtr(rawPtr);
  if (p <= 0x10000)
    return 0;

  uint16_t id = 0;
  if (!T05ReadProcessValue(p + 0x08, &id) || id < 1 || id > 5102)
    return 0;
  return id;
}

bool T05IsReadableCommittedRegion(const MEMORY_BASIC_INFORMATION& mbi) {
  if (mbi.State != MEM_COMMIT)
    return false;
  if (mbi.Protect & PAGE_GUARD)
    return false;
  if (mbi.Protect & PAGE_NOACCESS)
    return false;

  const DWORD baseProtect = mbi.Protect & 0xFF;
  return baseProtect == PAGE_READONLY ||
         baseProtect == PAGE_READWRITE ||
         baseProtect == PAGE_WRITECOPY ||
         baseProtect == PAGE_EXECUTE_READ ||
         baseProtect == PAGE_EXECUTE_READWRITE ||
         baseProtect == PAGE_EXECUTE_WRITECOPY;
}

void T05LogKnownRelationshipCandidates(
    uintptr_t gameBase,
    uint16_t heroId,
    uint16_t targetId) {
  constexpr uintptr_t kOffsets[] = {
      0x57AE38,
      0x462BA8,
  };
  constexpr uintptr_t kBiases[] = {
      0x80,
      0xC0,
  };
  constexpr int kSlotCount = 3000;
  constexpr uintptr_t kStride = 0x40;

  for (uintptr_t off : kOffsets) {
    uintptr_t root = 0;
    if (!T05ReadProcessValue(gameBase + off, &root) || root <= 0x10000) {
      DX11Base::AddLog(
          u8"[커스텀배우자DBG] relation ptr +0x%llX 읽기 실패/NULL",
          (unsigned long long)off);
      continue;
    }

    DX11Base::AddLog(
        u8"[커스텀배우자DBG] relation ptr +0x%llX = %p",
        (unsigned long long)off,
        (void*)root);

    for (uintptr_t bias : kBiases) {
      if (root <= bias)
        continue;

      const uintptr_t base = root - bias;
      size_t heroSlots = 0;
      size_t targetSlots = 0;
      size_t pairSlots = 0;
      size_t logged = 0;

      for (int i = 0; i < kSlotCount; ++i) {
        unsigned char slot[0x40] = {};
        if (!T05ReadProcessBytes(
                base + (uintptr_t)i * kStride,
                slot, sizeof(slot))) {
          continue;
        }

        const uint8_t relation = slot[0x08];
        if (relation < 1 || relation > 4)
          continue;

        uint16_t ids[5] = {};
        bool hasHero = false;
        bool hasTarget = false;
        for (int j = 0; j < 5; ++j) {
          uintptr_t memberPtr = 0;
          memcpy(
              &memberPtr,
              slot + 0x10 + (size_t)j * sizeof(uintptr_t),
              sizeof(memberPtr));
          ids[j] = T05ReadOfficerIdFromAnyPtr(memberPtr);
          hasHero = hasHero || ids[j] == heroId;
          hasTarget = hasTarget || ids[j] == targetId;
        }

        if (!hasHero && !hasTarget)
          continue;

        if (hasHero)
          ++heroSlots;
        if (hasTarget)
          ++targetSlots;
        if (hasHero && hasTarget)
          ++pairSlots;

        if (logged < 24) {
          DX11Base::AddLog(
              u8"[커스텀배우자DBG] relation 후보 base=%p bias=0x%llX slot=%d addr=%p type=%u members=%u,%u,%u,%u,%u%s",
              (void*)base,
              (unsigned long long)bias,
              i,
              (void*)(base + (uintptr_t)i * kStride),
              (unsigned)relation,
              (unsigned)ids[0],
              (unsigned)ids[1],
              (unsigned)ids[2],
              (unsigned)ids[3],
              (unsigned)ids[4],
              (hasHero && hasTarget) ? u8"  <== 3001/3012 동시" : "");
          ++logged;
        }
      }

      DX11Base::AddLog(
          u8"[커스텀배우자DBG] relation 요약 base=%p bias=0x%llX / heroSlots=%zu targetSlots=%zu pairSlots=%zu",
          (void*)base,
          (unsigned long long)bias,
          heroSlots,
          targetSlots,
          pairSlots);
    }
  }
}

void T05ScanProcessForCustomSpousePair(
    uintptr_t heroMaster,
    uintptr_t heroLive,
    uint16_t targetId) {
  SYSTEM_INFO si{};
  GetSystemInfo(&si);

  const uintptr_t minAddr =
      reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
  const uintptr_t maxAddr =
      reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
  const uintptr_t heroMasterNorm = T05NormalizePtr(heroMaster);
  const uintptr_t heroLiveNorm = T05NormalizePtr(heroLive);

  constexpr size_t kChunkSize = 0x10000;
  std::vector<unsigned char> buffer(kChunkSize);

  uintptr_t cursor = minAddr;
  size_t scannedBytes = 0;
  size_t heroPointerHits = 0;
  size_t pairHits = 0;
  size_t loggedPairHits = 0;
  const ULONGLONG started = GetTickCount64();

  while (cursor < maxAddr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(
            reinterpret_cast<LPCVOID>(cursor),
            &mbi, sizeof(mbi)) != sizeof(mbi)) {
      break;
    }

    const uintptr_t regionStart =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd =
        regionStart + mbi.RegionSize;
    if (regionEnd <= cursor)
      break;

    if (T05IsReadableCommittedRegion(mbi)) {
      uintptr_t curr = regionStart;
      while (curr < regionEnd) {
        const size_t remaining = (size_t)(regionEnd - curr);
        const size_t toRead = (std::min)(remaining, kChunkSize);
        SIZE_T bytesRead = 0;

        if (ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<LPCVOID>(curr),
                buffer.data(), toRead, &bytesRead) != FALSE &&
            bytesRead >= sizeof(uintptr_t)) {
          scannedBytes += bytesRead;

          size_t i = (size_t)((8 - (curr & 7)) & 7);
          for (; i + sizeof(uintptr_t) <= bytesRead;
               i += sizeof(uintptr_t)) {
            uintptr_t raw = 0;
            memcpy(&raw, buffer.data() + i, sizeof(raw));
            const uintptr_t norm = T05NormalizePtr(raw);
            if (norm != heroMasterNorm &&
                (heroLiveNorm == 0 || norm != heroLiveNorm)) {
              continue;
            }

            ++heroPointerHits;
            const uintptr_t hitAddr = curr + i;

            for (int q = -6; q <= 6; ++q) {
              if (q == 0)
                continue;

              const intptr_t signedNeighbor =
                  (intptr_t)hitAddr +
                  (intptr_t)q * (intptr_t)sizeof(uintptr_t);
              if (signedNeighbor <= 0x10000)
                continue;

              uintptr_t neighborRaw = 0;
              if (!T05ReadProcessValue(
                      (uintptr_t)signedNeighbor,
                      &neighborRaw)) {
                continue;
              }

              if (T05ReadOfficerIdFromAnyPtr(neighborRaw) != targetId)
                continue;

              ++pairHits;

              uint8_t m8 = 0xFF;
              uint8_t m16 = 0xFF;
              uint8_t m24 = 0xFF;
              if (hitAddr >= 8)
                T05ReadProcessValue(hitAddr - 8, &m8);
              if (hitAddr >= 16)
                T05ReadProcessValue(hitAddr - 16, &m16);
              if (hitAddr >= 24)
                T05ReadProcessValue(hitAddr - 24, &m24);

              if (loggedPairHits < 64) {
                DX11Base::AddLog(
                    u8"[커스텀배우자DBG] RAW PAIR hit=%p heroPtr=%p(%s) spousePtr=%p(ID:%u) qDelta=%d / preBytes[-8,-16,-24]=%u,%u,%u%s",
                    (void*)hitAddr,
                    (void*)norm,
                    norm == heroMasterNorm ? u8"master" : u8"live",
                    (void*)T05NormalizePtr(neighborRaw),
                    (unsigned)targetId,
                    q,
                    (unsigned)m8,
                    (unsigned)m16,
                    (unsigned)m24,
                    (q == 1 && m8 == 2)
                        ? u8"  <== legacy 관계형(type=2, hero, spouse)"
                        : "");
                ++loggedPairHits;
              }
              break;
            }
          }
        }

        curr += toRead;
      }
    }

    cursor = regionEnd;
  }

  DX11Base::AddLog(
      u8"[커스텀배우자DBG] 전체 메모리 read-only 검색 완료: scanned=%zuMB heroPtrHits=%zu pairHits=%zu logged=%zu / %llums",
      scannedBytes / (1024 * 1024),
      heroPointerHits,
      pairHits,
      loggedPairHits,
      (unsigned long long)(GetTickCount64() - started));
}

} // namespace

namespace DX11Base {
  bool T05PrepareDebugWorkerStart();
  void T05RegisterDebugWorker(
      std::thread&& worker,
      std::shared_ptr<std::atomic<bool>> done);
}

namespace {

void T05TryStartCustomSpouseDiagnostic(uintptr_t gameBase) {
  if (gameBase <= 0x10000 ||
      g_t05CustomSpouseDiagRunning.load(std::memory_order_acquire)) {
    return;
  }

  uintptr_t heroLive = 0;
  uint16_t heroId = 0;
  if (!T05ReadProcessValue(gameBase + 0xE0, &heroLive) ||
      heroLive <= 0x10000 ||
      !T05ReadProcessValue(T05NormalizePtr(heroLive) + 0x08, &heroId) ||
      heroId != kT05CustomSpouseDiagHeroId) {
    return;
  }

  if (g_t05CustomSpouseDiagCompletedHeroId.load(
          std::memory_order_acquire) == heroId) {
    return;
  }

  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
  uintptr_t rosterBase = 0;
  if (!exeBase ||
      !DX11Base::TryResolveOfficerRosterArrayBase(
          exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    return;
  }

  const uintptr_t heroMaster =
      rosterBase +
      (uintptr_t)(heroId - 1) * 0x3D0;
  const uintptr_t targetMaster =
      rosterBase +
      (uintptr_t)(kT05CustomSpouseDiagTargetId - 1) * 0x3D0;

  uint16_t verifyHero = 0;
  uint16_t verifyTarget = 0;
  if (!T05ReadProcessValue(heroMaster + 0x08, &verifyHero) ||
      verifyHero != heroId ||
      !T05ReadProcessValue(targetMaster + 0x08, &verifyTarget) ||
      verifyTarget != kT05CustomSpouseDiagTargetId) {
    return;
  }

  bool expected = false;
  if (!g_t05CustomSpouseDiagRunning.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return;
  }

  g_t05CustomSpouseDiagCompletedHeroId.store(
      heroId, std::memory_order_release);

  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread worker(
      [gameBase,
       rosterBase,
       heroMaster,
       heroLive,
       targetMaster,
       heroId,
       done]() {
        DX11Base::AddLog(
            u8"[커스텀배우자DBG] 시작: Hero ID %u master=%p live=%p / Target spouse ID %u master=%p / roster=%p",
            (unsigned)heroId,
            (void*)heroMaster,
            (void*)T05NormalizePtr(heroLive),
            (unsigned)kT05CustomSpouseDiagTargetId,
            (void*)targetMaster,
            (void*)rosterBase);

        // 1) 현재 코드가 사용하는 두 relationship root와 0x80/0xC0 후보를
        //    3000슬롯 끝까지 직접 읽어 3001/3012가 실제로 들어있는지 확인합니다.
        T05LogKnownRelationshipCandidates(
            gameBase,
            heroId,
            kT05CustomSpouseDiagTargetId);

        // 2) 위 테이블에서 못 잡는 경우를 위해 프로세스의 읽기 가능한 메모리에서
        //    hero 포인터(master/live) 주변에 ID 3012 무장 포인터가 같이 있는 구조만 찾습니다.
        //    쓰기/VirtualProtect/hook은 전혀 하지 않는 read-only 진단입니다.
        T05ScanProcessForCustomSpousePair(
            heroMaster,
            T05NormalizePtr(heroLive),
            kT05CustomSpouseDiagTargetId);

        DX11Base::AddLog(
            u8"[커스텀배우자DBG] 종료: 3001/3012 관계 진단 완료");

        g_t05CustomSpouseDiagRunning.store(
            false, std::memory_order_release);
        done->store(true, std::memory_order_release);
      });

  DX11Base::T05RegisterDebugWorker(
      std::move(worker), done);
}

} // namespace

// 기존 구현의 std::thread(...).detach()를 관리형 worker로 바꾸고,
// 내부 vector<unsigned char> 버퍼는 thread-local allocator로 재사용합니다.
namespace std {
template <class T>
using T05DebugVector = vector<T, ::T05DebugAllocator<T>>;

class T05ManagedThread {
public:
  template <class F, class... Args>
  explicit T05ManagedThread(F&& f, Args&&... args) {
    auto fn = std::decay_t<F>(std::forward<F>(f));
    auto params = std::make_tuple(std::forward<Args>(args)...);
    task_ = [fn = std::move(fn), params = std::move(params)]() mutable {
      std::apply(fn, params);
    };
  }

  T05ManagedThread(const T05ManagedThread&) = delete;
  T05ManagedThread& operator=(const T05ManagedThread&) = delete;

  void detach() {
    if (!DX11Base::T05PrepareDebugWorkerStart())
      return;

    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([task = std::move(task_), done]() mutable {
      task();
      done->store(true, std::memory_order_release);
    });
    DX11Base::T05RegisterDebugWorker(std::move(worker), done);
  }

private:
  std::function<void()> task_;
};
} // namespace std

#define vector T05DebugVector
#define thread T05ManagedThread
#define debuging debugingLegacy
#include "debug_impl.inc"
#undef debuging
#undef thread
#undef vector

namespace DX11Base {

bool T05PrepareDebugWorkerStart() {
  bool expected = false;
  if (!s_isScanning.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    AddLog(u8"[스캐너/T05] 이미 검색 작업이 실행 중입니다.");
    return false;
  }
  s_stopScan.store(false, std::memory_order_release);
  return true;
}

void T05RegisterDebugWorker(
    std::thread&& worker,
    std::shared_ptr<std::atomic<bool>> done) {
  auto* state = new T05DebugWorker{std::move(worker), std::move(done)};
  std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
  g_t05DebugWorkers.push_back(state);
}

static void ReapFinishedDebugWorkersT05() {
  std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
  for (auto it = g_t05DebugWorkers.begin(); it != g_t05DebugWorkers.end();) {
    T05DebugWorker* worker = *it;
    if (!worker || !worker->done ||
        !worker->done->load(std::memory_order_acquire)) {
      ++it;
      continue;
    }

    if (worker->thread.joinable())
      worker->thread.join();
    delete worker;
    it = g_t05DebugWorkers.erase(it);
  }
}

void ShutdownDebugScannerT05() {
  s_stopScan.store(true, std::memory_order_release);

  std::vector<T05DebugWorker*> workers;
  {
    std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
    workers.swap(g_t05DebugWorkers);
  }

  for (T05DebugWorker* worker : workers) {
    if (!worker)
      continue;
    if (worker->thread.joinable())
      worker->thread.join();
    delete worker;
  }

  s_isScanning.store(false, std::memory_order_release);
}

void debuging(uintptr_t gameBase, uintptr_t p1) {
  ReapFinishedDebugWorkersT05();
  debugingLegacy(gameBase, p1);
  T05TryStartCustomSpouseDiagnostic(gameBase);
}

} // namespace DX11Base