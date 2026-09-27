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
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_set>
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

// 커스텀 주인공/배우자 임신 구조 확인용 read-only 진단.
// 이번 테스트 세이브: Hero ID 3001 / spouse ID 3012 / 자녀 없음 / 비임신.
constexpr uint16_t kPregDiagHeroId = 3001;
constexpr uint16_t kPregDiagSpouseId = 3012;
std::atomic<bool> g_pregDiagStarted{false};

struct PregDiagSlot {
  bool readOk = false;
  uintptr_t addr = 0;
  uintptr_t q00 = 0;
  uintptr_t q08 = 0;
  uintptr_t child = 0;
  uint16_t id00 = 0;
  uint16_t id08 = 0;
  uint16_t childId = 0;
  uint8_t cooldown = 0;
  uint8_t flag = 0;
  uint8_t months = 0;
};

uintptr_t PregDiagNormalizePtr(uintptr_t p) {
  return p & 0x0000FFFFFFFFFFFFULL;
}

bool PregDiagRead(uintptr_t addr, void* out, size_t size) {
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
bool PregDiagReadValue(uintptr_t addr, T* out) {
  return out && PregDiagRead(addr, out, sizeof(T));
}

uint16_t PregDiagReadOfficerId(uintptr_t rawPtr) {
  const uintptr_t ptr = PregDiagNormalizePtr(rawPtr);
  if (ptr <= 0x10000)
    return 0;

  uint16_t id = 0;
  if (!PregDiagReadValue(ptr + 0x08, &id) || id < 1 || id > 5102)
    return 0;
  return id;
}

bool PregDiagReadSlot(uintptr_t addr, PregDiagSlot* out) {
  if (!out)
    return false;

  PregDiagSlot slot;
  slot.addr = addr;
  if (!PregDiagReadValue(addr + 0x00, &slot.q00) ||
      !PregDiagReadValue(addr + 0x08, &slot.q08) ||
      !PregDiagReadValue(addr + 0x08, &slot.cooldown) ||
      !PregDiagReadValue(addr + 0x09, &slot.flag) ||
      !PregDiagReadValue(addr + 0x0A, &slot.months) ||
      !PregDiagReadValue(addr + 0x10, &slot.child)) {
    *out = slot;
    return false;
  }

  slot.id00 = PregDiagReadOfficerId(slot.q00);
  slot.id08 = PregDiagReadOfficerId(slot.q08);
  slot.childId = PregDiagReadOfficerId(slot.child);
  slot.readOk = true;
  *out = slot;
  return true;
}

void PregDiagLogSlot(const char* tag, int index, const PregDiagSlot& s) {
  DX11Base::AddLog(
      u8"[임신구조DBG] %s slot%d addr=%p +00=%p(ID:%u) +08=%p(ID:%u / cd:%u flag:%u mon:%u) +10=%p(childID:%u)",
      tag,
      index + 1,
      (void*)s.addr,
      (void*)PregDiagNormalizePtr(s.q00),
      (unsigned)s.id00,
      (void*)PregDiagNormalizePtr(s.q08),
      (unsigned)s.id08,
      (unsigned)s.cooldown,
      (unsigned)s.flag,
      (unsigned)s.months,
      (void*)PregDiagNormalizePtr(s.child),
      (unsigned)s.childId);
}

bool PregDiagValidateTable(
    uintptr_t base,
    PregDiagSlot (&slots)[3],
    char* reason,
    size_t reasonSize) {
  if (!reason || reasonSize == 0)
    return false;
  reason[0] = '\0';

  std::unordered_set<uint16_t> uniqueSpouses;

  for (int i = 0; i < 3; ++i) {
    if (!PregDiagReadSlot(base + (uintptr_t)i * 0x28, &slots[i])) {
      snprintf(reason, reasonSize, "slot%d read-fail", i + 1);
      return false;
    }

    const PregDiagSlot& s = slots[i];
    if (s.cooldown > 100) {
      snprintf(reason, reasonSize, "slot%d cooldown=%u >100", i + 1, (unsigned)s.cooldown);
      return false;
    }
    if (s.flag > 1) {
      snprintf(reason, reasonSize, "slot%d flag=%u >1", i + 1, (unsigned)s.flag);
      return false;
    }
    if (s.months > 12) {
      snprintf(reason, reasonSize, "slot%d months=%u >12", i + 1, (unsigned)s.months);
      return false;
    }

    const uintptr_t spousePtr = PregDiagNormalizePtr(s.q00);
    if (spousePtr == 0) {
      if (s.id00 != 0) {
        snprintf(reason, reasonSize, "slot%d NULL spouse but id=%u", i + 1, (unsigned)s.id00);
        return false;
      }
    } else {
      if (s.id00 == 0) {
        snprintf(reason, reasonSize, "slot%d +00 nonnull but officer-id invalid", i + 1);
        return false;
      }
      if (!uniqueSpouses.insert(s.id00).second) {
        snprintf(reason, reasonSize, "slot%d duplicate spouse id=%u", i + 1, (unsigned)s.id00);
        return false;
      }
    }

    if (s.child != 0 && s.childId == 0) {
      snprintf(reason, reasonSize, "slot%d child nonnull but officer-id invalid", i + 1);
      return false;
    }
  }

  snprintf(reason, reasonSize, "VALID");
  return true;
}

void PregDiagLogTable(const char* tag, uintptr_t base) {
  PregDiagSlot slots[3]{};
  char reason[160] = {};
  const bool valid = PregDiagValidateTable(base, slots, reason, sizeof(reason));

  DX11Base::AddLog(
      u8"[임신구조DBG] %s table=%p result=%s reason=%s",
      tag,
      (void*)base,
      valid ? "VALID" : "REJECT",
      reason);

  for (int i = 0; i < 3; ++i)
    PregDiagLogSlot(tag, i, slots[i]);
}

bool PregDiagReadableRegion(const MEMORY_BASIC_INFORMATION& mbi) {
  if (mbi.State != MEM_COMMIT ||
      (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS)) {
    return false;
  }

  const DWORD p = mbi.Protect & 0xFF;
  return p == PAGE_READONLY ||
         p == PAGE_READWRITE ||
         p == PAGE_WRITECOPY ||
         p == PAGE_EXECUTE_READ ||
         p == PAGE_EXECUTE_READWRITE ||
         p == PAGE_EXECUTE_WRITECOPY;
}

void PregDiagLogHitContext(uintptr_t hitAddr) {
  uintptr_t values[9]{};
  bool ok[9]{};
  for (int i = 0; i < 9; ++i) {
    const intptr_t delta = (intptr_t)(i - 4) * 8;
    const intptr_t addr = (intptr_t)hitAddr + delta;
    if (addr > 0x10000)
      ok[i] = PregDiagReadValue((uintptr_t)addr, &values[i]);
  }

  DX11Base::AddLog(
      u8"[임신구조DBG] ptr-hit ctx hit=%p rel[-20..+20]=%p %p %p %p [%p] %p %p %p %p",
      (void*)hitAddr,
      (void*)(ok[0] ? PregDiagNormalizePtr(values[0]) : 0),
      (void*)(ok[1] ? PregDiagNormalizePtr(values[1]) : 0),
      (void*)(ok[2] ? PregDiagNormalizePtr(values[2]) : 0),
      (void*)(ok[3] ? PregDiagNormalizePtr(values[3]) : 0),
      (void*)(ok[4] ? PregDiagNormalizePtr(values[4]) : 0),
      (void*)(ok[5] ? PregDiagNormalizePtr(values[5]) : 0),
      (void*)(ok[6] ? PregDiagNormalizePtr(values[6]) : 0),
      (void*)(ok[7] ? PregDiagNormalizePtr(values[7]) : 0),
      (void*)(ok[8] ? PregDiagNormalizePtr(values[8]) : 0));
}

void RunPregnancyLayoutDiagnostic(uintptr_t gameBase) {
  const uintptr_t exeBase = (uintptr_t)GetModuleHandle(nullptr);
  uintptr_t rosterBase = 0;
  if (gameBase <= 0x10000 || !exeBase ||
      !DX11Base::TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) ||
      rosterBase <= 0x10000) {
    DX11Base::AddLog(u8"[임신구조DBG] 시작 실패: game/roster base 확인 불가");
    return;
  }

  uintptr_t heroAddr = 0;
  uintptr_t spouseAddr = 0;
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t candidate = rosterBase + (uintptr_t)i * 0x3D0;
    uint16_t id = 0;
    if (!PregDiagReadValue(candidate + 0x08, &id))
      continue;
    if (id == kPregDiagHeroId)
      heroAddr = candidate;
    else if (id == kPregDiagSpouseId)
      spouseAddr = candidate;
    if (heroAddr && spouseAddr)
      break;
  }

  if (!heroAddr || !spouseAddr) {
    DX11Base::AddLog(
        u8"[임신구조DBG] 시작 실패: roster에서 hero=%p spouse=%p",
        (void*)heroAddr, (void*)spouseAddr);
    return;
  }

  DX11Base::AddLog(
      u8"[임신구조DBG] 시작: Hero %u=%p / spouse %u=%p / gameBase=%p / roster=%p",
      (unsigned)kPregDiagHeroId,
      (void*)heroAddr,
      (unsigned)kPregDiagSpouseId,
      (void*)spouseAddr,
      (void*)gameBase,
      (void*)rosterBase);

  // 현재 코드가 고정 우선 경로로 사용하는 canonical 후보를 그대로 덤프합니다.
  PregDiagLogTable("fixed+5B40", gameBase + 0x5B40);

  constexpr uintptr_t kScanSize = 0x400000;
  constexpr size_t kChunkSize = 0x10000;
  const uintptr_t scanStart = gameBase;
  const uintptr_t scanEnd = gameBase + kScanSize;
  const uintptr_t spouseNorm = PregDiagNormalizePtr(spouseAddr);

  std::vector<unsigned char> buffer(kChunkSize);
  std::unordered_set<uintptr_t> testedBases;
  size_t ptrHits = 0;
  size_t candidateCount = 0;
  size_t loggedCandidates = 0;
  const ULONGLONG started = GetTickCount64();

  uintptr_t cursor = scanStart;
  while (cursor < scanEnd) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) != sizeof(mbi))
      break;

    const uintptr_t regionStart =
        (std::max)(cursor, (uintptr_t)mbi.BaseAddress);
    const uintptr_t regionEnd =
        (std::min)(scanEnd, (uintptr_t)mbi.BaseAddress + mbi.RegionSize);

    if (PregDiagReadableRegion(mbi) && regionEnd > regionStart) {
      for (uintptr_t curr = regionStart; curr < regionEnd;) {
        const size_t remaining = (size_t)(regionEnd - curr);
        const size_t toRead = (std::min)(remaining, kChunkSize);
        SIZE_T bytesRead = 0;

        if (ReadProcessMemory(
                GetCurrentProcess(), (LPCVOID)curr,
                buffer.data(), toRead, &bytesRead) != FALSE &&
            bytesRead >= sizeof(uintptr_t)) {
          size_t i = (size_t)((8 - (curr & 7)) & 7);
          for (; i + sizeof(uintptr_t) <= bytesRead; i += 8) {
            uintptr_t raw = 0;
            memcpy(&raw, buffer.data() + i, sizeof(raw));
            if (PregDiagNormalizePtr(raw) != spouseNorm)
              continue;

            ++ptrHits;
            const uintptr_t hitAddr = curr + i;
            if (ptrHits <= 8) {
              DX11Base::AddLog(
                  u8"[임신구조DBG] spouse3012 ptr-hit #%zu addr=%p relative=+0x%llX",
                  ptrHits,
                  (void*)hitAddr,
                  (unsigned long long)(hitAddr - gameBase));
              PregDiagLogHitContext(hitAddr);
            }

            // 현재 구현처럼 이 포인터가 slot0/1/2의 +00이라고 가정했을 때
            // 각 후보가 정확히 왜 탈락하는지 기록합니다.
            for (int slot = 0; slot < 3; ++slot) {
              const uintptr_t back = (uintptr_t)slot * 0x28;
              if (hitAddr < scanStart + back)
                continue;

              const uintptr_t candidateBase = hitAddr - back;
              if (candidateBase < scanStart ||
                  candidateBase + 0x28 * 3 > scanEnd ||
                  !testedBases.insert(candidateBase).second) {
                continue;
              }

              ++candidateCount;
              if (loggedCandidates < 18) {
                char tag[80] = {};
                snprintf(
                    tag, sizeof(tag),
                    "hit%zu-as-slot%d", ptrHits, slot + 1);
                PregDiagLogTable(tag, candidateBase);
                ++loggedCandidates;
              }
            }
          }
        }

        curr += toRead;
      }
    }

    if (regionEnd <= cursor)
      break;
    cursor = regionEnd;
  }

  DX11Base::AddLog(
      u8"[임신구조DBG] 종료: spouse3012 ptrHits=%zu candidateBases=%zu logged=%zu elapsed=%llums",
      ptrHits,
      candidateCount,
      loggedCandidates,
      (unsigned long long)(GetTickCount64() - started));
}

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

} // namespace

namespace DX11Base {
  bool T05PrepareDebugWorkerStart();
  void T05RegisterDebugWorker(
      std::thread&& worker,
      std::shared_ptr<std::atomic<bool>> done);
}

namespace {

void TryStartPregnancyLayoutDiagnostic(uintptr_t gameBase) {
  if (gameBase <= 0x10000 ||
      g_pregDiagStarted.load(std::memory_order_acquire)) {
    return;
  }

  uintptr_t heroLive = 0;
  uint16_t heroId = 0;
  if (!PregDiagReadValue(gameBase + 0xE0, &heroLive) ||
      heroLive <= 0x10000 ||
      !PregDiagReadValue(PregDiagNormalizePtr(heroLive) + 0x08, &heroId) ||
      heroId != kPregDiagHeroId) {
    return;
  }

  bool expected = false;
  if (!g_pregDiagStarted.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return;
  }

  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread worker([gameBase, done]() {
    RunPregnancyLayoutDiagnostic(gameBase);
    done->store(true, std::memory_order_release);
  });
  DX11Base::T05RegisterDebugWorker(std::move(worker), done);
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
  TryStartPregnancyLayoutDiagnostic(gameBase);
}

} // namespace DX11Base
