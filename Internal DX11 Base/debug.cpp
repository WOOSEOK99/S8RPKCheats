#define NOMINMAX
#include "pch.h"
#include "debug.h"
#include "showlog.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct T05DebugWorker {
  std::thread thread;
  std::shared_ptr<std::atomic<bool>> done;
};

std::mutex g_t05DebugWorkersMutex;
std::vector<T05DebugWorker> g_t05DebugWorkers;

} // namespace

namespace DX11Base {
  bool T05PrepareDebugWorkerStart();
  void T05RegisterDebugWorker(
      std::thread&& worker,
      std::shared_ptr<std::atomic<bool>> done);
}

// debug_impl.inc의 std::thread(...).detach() 두 경로만 관리형 worker로 치환합니다.
// 실제 검색 구현/UI는 보존합니다.
namespace std {
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

#define thread T05ManagedThread
#define debuging debugingLegacy
#include "debug_impl.inc"
#undef debuging
#undef thread

namespace DX11Base {

bool T05PrepareDebugWorkerStart() {
  // 버튼 클릭 시점에 먼저 running을 확보하여 다음 프레임 전 중복 시작 창을 닫습니다.
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
  std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
  g_t05DebugWorkers.push_back({std::move(worker), std::move(done)});
}

static void ReapFinishedDebugWorkersT05() {
  std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
  for (auto it = g_t05DebugWorkers.begin(); it != g_t05DebugWorkers.end();) {
    if (!it->done || !it->done->load(std::memory_order_acquire)) {
      ++it;
      continue;
    }

    if (it->thread.joinable())
      it->thread.join();
    it = g_t05DebugWorkers.erase(it);
  }
}

void ShutdownDebugScannerT05() {
  s_stopScan.store(true, std::memory_order_release);

  std::vector<T05DebugWorker> workers;
  {
    std::lock_guard<std::mutex> lock(g_t05DebugWorkersMutex);
    workers.swap(g_t05DebugWorkers);
  }

  for (auto& worker : workers) {
    if (worker.thread.joinable())
      worker.thread.join();
  }

  s_isScanning.store(false, std::memory_order_release);
}

void debuging(uintptr_t gameBase, uintptr_t p1) {
  ReapFinishedDebugWorkersT05();
  debugingLegacy(gameBase, p1);
}

} // namespace DX11Base
