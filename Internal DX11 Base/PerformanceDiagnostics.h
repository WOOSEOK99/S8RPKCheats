#pragma once

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace DX11Base {

  void AddLog(const char *fmt, ...);
  extern bool bFileLog;

  enum class PerfMetric : uint8_t {
    MenuLoopHeartbeat = 0,
    SpeedHackUpdate,
    IsValidPtr,
    FindPattern,
    SkillCountSave,
    NotificationProduce,
    NotificationRender,
    StartupBridgePrepare,
    InitCheatsAttempt,
    MonthCaptureScan,
    AddLogCall,
    AddLogMutexWait,
    AddLogFileIo,
    Count
  };

  struct PerfMetricSlot {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> total100ns{0};
    std::atomic<uint64_t> max100ns{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> changes{0};
  };

  struct PerfDiagnosticsState {
    std::array<PerfMetricSlot, static_cast<size_t>(PerfMetric::Count)> metrics{};
    std::atomic<uint64_t> nextReport100ns{0};
    std::atomic<uint64_t> maxNotificationQueue{0};
    std::atomic<uint64_t> maxNotificationHistory{0};
    std::atomic<uint64_t> currentNotificationQueue{0};
    std::atomic<uint64_t> currentNotificationHistory{0};
    std::atomic<uint32_t> speedMultiplierMilli{1000};
    std::atomic<bool> speedHackEnabled{false};
    std::atomic<bool> environmentLogged{false};
  };

  inline PerfDiagnosticsState &GetPerfDiagnosticsState() {
    static PerfDiagnosticsState state;
    return state;
  }

  inline uint64_t PerfRealNow100ns() {
    ULONGLONG now = 0;
    if (QueryUnbiasedInterruptTime(&now))
      return static_cast<uint64_t>(now);

    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
  }

  inline bool PerfDiagnosticsEnabled() {
    if (bFileLog)
      return true;

    static const bool environmentEnabled = []() {
      char value[32] = {};
      const DWORD len = GetEnvironmentVariableA("S8RPK_PERF_DIAGNOSTICS", value, static_cast<DWORD>(sizeof(value)));
      if (len == 0 || len >= sizeof(value))
        return false;

      for (DWORD i = 0; i < len; ++i)
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));

      return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
             std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0;
    }();
    return environmentEnabled;
  }

  inline const char *PerfMetricName(PerfMetric metric) {
    switch (metric) {
    case PerfMetric::MenuLoopHeartbeat: return "MenuLoopHeartbeat";
    case PerfMetric::SpeedHackUpdate: return "SpeedHackUpdate";
    case PerfMetric::IsValidPtr: return "IsValidPtr";
    case PerfMetric::FindPattern: return "FindPattern";
    case PerfMetric::SkillCountSave: return "SkillCountSave";
    case PerfMetric::NotificationProduce: return "NotificationProduce";
    case PerfMetric::NotificationRender: return "NotificationRender";
    case PerfMetric::StartupBridgePrepare: return "StartupBridgePrepare";
    case PerfMetric::InitCheatsAttempt: return "InitCheatsAttempt";
    case PerfMetric::MonthCaptureScan: return "MonthCaptureScan";
    case PerfMetric::AddLogCall: return "AddLogCall";
    case PerfMetric::AddLogMutexWait: return "AddLogMutexWait";
    case PerfMetric::AddLogFileIo: return "AddLogFileIo";
    default: return "Unknown";
    }
  }

  inline void PerfUpdateMax(std::atomic<uint64_t> &target, uint64_t value) {
    uint64_t observed = target.load(std::memory_order_relaxed);
    while (observed < value &&
           !target.compare_exchange_weak(observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
  }

  // AddLog itself is instrumented. This raw recorder intentionally does not
  // call PerfMaybeReport(), otherwise a diagnostics report would recurse back
  // into AddLog while AddLog is trying to record its own cost.
  inline void PerfRecordNoReport(PerfMetric metric, uint64_t elapsed100ns = 0,
                                 uint64_t bytes = 0, uint64_t changes = 0) {
    if (!PerfDiagnosticsEnabled())
      return;

    auto &slot = GetPerfDiagnosticsState().metrics[static_cast<size_t>(metric)];
    slot.calls.fetch_add(1, std::memory_order_relaxed);
    if (elapsed100ns != 0) {
      slot.total100ns.fetch_add(elapsed100ns, std::memory_order_relaxed);
      PerfUpdateMax(slot.max100ns, elapsed100ns);
    }
    if (bytes != 0)
      slot.bytes.fetch_add(bytes, std::memory_order_relaxed);
    if (changes != 0)
      slot.changes.fetch_add(changes, std::memory_order_relaxed);
  }

  inline void PerfRecordNotificationSizes(size_t queueSize, size_t historySize) {
    if (!PerfDiagnosticsEnabled())
      return;

    auto &state = GetPerfDiagnosticsState();
    state.currentNotificationQueue.store(static_cast<uint64_t>(queueSize), std::memory_order_relaxed);
    state.currentNotificationHistory.store(static_cast<uint64_t>(historySize), std::memory_order_relaxed);
    PerfUpdateMax(state.maxNotificationQueue, static_cast<uint64_t>(queueSize));
    PerfUpdateMax(state.maxNotificationHistory, static_cast<uint64_t>(historySize));
  }

  inline void PerfSetSpeedState(bool enabled, float multiplier) {
    if (!PerfDiagnosticsEnabled())
      return;

    auto &state = GetPerfDiagnosticsState();
    state.speedHackEnabled.store(enabled, std::memory_order_relaxed);
    if (multiplier < 0.0f)
      multiplier = 0.0f;
    state.speedMultiplierMilli.store(static_cast<uint32_t>(multiplier * 1000.0f + 0.5f), std::memory_order_relaxed);
  }

  inline void PerfLogEnvironmentOnce() {
    if (!PerfDiagnosticsEnabled())
      return;

    auto &state = GetPerfDiagnosticsState();
    bool expected = false;
    if (!state.environmentLogged.compare_exchange_strong(expected, true, std::memory_order_relaxed))
      return;

    unsigned dinput8Count = 0;
    unsigned dxgiCount = 0;
    unsigned hidCount = 0;
    unsigned versionCount = 0;

    HMODULE modules[1024] = {};
    DWORD bytesNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytesNeeded)) {
      const size_t moduleCount = (std::min)(
          static_cast<size_t>(bytesNeeded / sizeof(HMODULE)),
          sizeof(modules) / sizeof(modules[0]));
      for (size_t i = 0; i < moduleCount; ++i) {
        char moduleName[MAX_PATH] = {};
        if (GetModuleBaseNameA(GetCurrentProcess(), modules[i], moduleName,
                               static_cast<DWORD>(sizeof(moduleName))) == 0)
          continue;

        if (_stricmp(moduleName, "dinput8.dll") == 0)
          ++dinput8Count;
        else if (_stricmp(moduleName, "dxgi.dll") == 0)
          ++dxgiCount;
        else if (_stricmp(moduleName, "hid.dll") == 0)
          ++hidCount;
        else if (_stricmp(moduleName, "version.dll") == 0)
          ++versionCount;
      }
    }

#ifdef _DEBUG
    const char *buildConfig = "Debug";
#else
    const char *buildConfig = "Release";
#endif

    AddLog("[Perf:T00] diagnostics=ON build=%s arch=%s modules(dinput8=%u dxgi=%u hid=%u version=%u)",
           buildConfig,
#ifdef _WIN64
           "x64",
#else
           "x86",
#endif
           dinput8Count, dxgiCount, hidCount, versionCount);
  }

  inline void PerfMaybeReport() {
    if (!PerfDiagnosticsEnabled())
      return;

    PerfLogEnvironmentOnce();

    auto &state = GetPerfDiagnosticsState();
    const uint64_t now = PerfRealNow100ns();
    constexpr uint64_t kReportInterval100ns = 10ull * 1000ull * 1000ull * 10ull;

    uint64_t deadline = state.nextReport100ns.load(std::memory_order_relaxed);
    if (deadline == 0) {
      const uint64_t desired = now + kReportInterval100ns;
      state.nextReport100ns.compare_exchange_strong(deadline, desired, std::memory_order_relaxed);
      return;
    }
    if (now < deadline)
      return;

    if (!state.nextReport100ns.compare_exchange_strong(
            deadline, now + kReportInterval100ns, std::memory_order_relaxed, std::memory_order_relaxed))
      return;

    const bool speedEnabled = state.speedHackEnabled.load(std::memory_order_relaxed);
    const double speed = static_cast<double>(state.speedMultiplierMilli.load(std::memory_order_relaxed)) / 1000.0;
    const uint64_t queueNow = state.currentNotificationQueue.load(std::memory_order_relaxed);
    const uint64_t queueMax = state.maxNotificationQueue.exchange(queueNow, std::memory_order_relaxed);
    const uint64_t historyNow = state.currentNotificationHistory.load(std::memory_order_relaxed);
    const uint64_t historyMax = state.maxNotificationHistory.exchange(historyNow, std::memory_order_relaxed);

    AddLog("[Perf:T00] interval=10s speed=%s/%.1fx notification(queue=%llu max=%llu history=%llu max=%llu)",
           speedEnabled ? "ON" : "OFF", speed,
           static_cast<unsigned long long>(queueNow), static_cast<unsigned long long>(queueMax),
           static_cast<unsigned long long>(historyNow), static_cast<unsigned long long>(historyMax));

    for (size_t i = 0; i < static_cast<size_t>(PerfMetric::Count); ++i) {
      auto &slot = state.metrics[i];
      const uint64_t calls = slot.calls.exchange(0, std::memory_order_relaxed);
      const uint64_t total = slot.total100ns.exchange(0, std::memory_order_relaxed);
      const uint64_t maximum = slot.max100ns.exchange(0, std::memory_order_relaxed);
      const uint64_t bytes = slot.bytes.exchange(0, std::memory_order_relaxed);
      const uint64_t changes = slot.changes.exchange(0, std::memory_order_relaxed);
      if (calls == 0 && bytes == 0 && changes == 0)
        continue;

      const double totalMs = static_cast<double>(total) / 10000.0;
      const double avgUs = calls ? (static_cast<double>(total) / static_cast<double>(calls)) / 10.0 : 0.0;
      const double maxUs = static_cast<double>(maximum) / 10.0;
      AddLog("[Perf:T00] %-20s calls=%llu total=%.3fms avg=%.2fus max=%.2fus bytes=%llu changes=%llu",
             PerfMetricName(static_cast<PerfMetric>(i)),
             static_cast<unsigned long long>(calls), totalMs, avgUs, maxUs,
             static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(changes));
    }
  }

  inline void PerfRecord(PerfMetric metric, uint64_t elapsed100ns = 0,
                         uint64_t bytes = 0, uint64_t changes = 0) {
    if (!PerfDiagnosticsEnabled())
      return;

    PerfRecordNoReport(metric, elapsed100ns, bytes, changes);
    PerfMaybeReport();
  }

  class PerfScope {
  public:
    explicit PerfScope(PerfMetric metric, uint64_t bytes = 0, uint64_t changes = 0)
        : m_metric(metric), m_bytes(bytes), m_changes(changes),
          m_enabled(PerfDiagnosticsEnabled()), m_start(m_enabled ? PerfRealNow100ns() : 0) {}

    ~PerfScope() {
      if (!m_enabled)
        return;
      const uint64_t end = PerfRealNow100ns();
      const uint64_t elapsed = (end >= m_start) ? (end - m_start) : 0;
      PerfRecord(m_metric, elapsed, m_bytes, m_changes);
    }

    PerfScope(const PerfScope &) = delete;
    PerfScope &operator=(const PerfScope &) = delete;

  private:
    PerfMetric m_metric;
    uint64_t m_bytes;
    uint64_t m_changes;
    bool m_enabled;
    uint64_t m_start;
  };

} // namespace DX11Base