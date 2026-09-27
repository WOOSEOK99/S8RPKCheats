#define NOMINMAX
#include "pch.h"
#include "debug.h"
#include "Cheats.h"
#include "Cheats\Social\InstantLoveCave.h"
#include "Cheats\System\MonthCapture.h"
#include "Cheats\Officer\OfficerDetail.h"
#include "Cheats\Officer\SelectOfficercapture.h"
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
}

} // namespace DX11Base
