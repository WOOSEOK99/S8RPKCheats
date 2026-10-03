#include "pch.h"
#include "debug.h"
#include "Cheats.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Config.h"
#include "MenuState.h"
#include "PerformanceDiagnostics.h"
#include "showlog.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace DX11Base {

namespace {
bool s_earlyStratagemFiveEnabled = false;
bool s_startupStratagemBridgeReady = false;

bool ReadEarlyStratagemFiveSetting() {
  std::ifstream file(GetConfigPath());
  if (!file.is_open())
    return false;

  std::string line;
  while (std::getline(file, line)) {
    if (line.find("\"bStratagemFiveEnabled\"") == std::string::npos)
      continue;
    return line.find("true") != std::string::npos;
  }
  return false;
}
} // namespace

void T00LoadEarlyLogConfig() {
  LoadEarlyLogConfig();

  s_earlyStratagemFiveEnabled = ReadEarlyStratagemFiveSetting();
  bStratagemFiveEnabled = s_earlyStratagemFiveEnabled;

  static std::atomic<uint64_t> s_initGeneration{0};
  const uint64_t generation =
      s_initGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

  HMODULE selfModule = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&T00LoadEarlyLogConfig),
                     &selfModule);

  char dllPath[MAX_PATH] = {};
  if (selfModule)
    GetModuleFileNameA(selfModule, dllPath, MAX_PATH);

  const std::string dllName = dllPath[0]
                                  ? std::filesystem::path(dllPath).filename().string()
                                  : std::string("unknown");
  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

  AddLog("[Session] pid=%lu init=%llu dll=%s module=%p exeBase=%p version=%s renderer=pending",
         static_cast<unsigned long>(GetCurrentProcessId()),
         static_cast<unsigned long long>(generation), dllName.c_str(),
         selfModule, reinterpret_cast<void *>(exeBase), SAM8_CHEAT_VERSION);
  AddLog("[Stratagem5UI] early saved setting=%s",
         s_earlyStratagemFiveEnabled ? "ON" : "OFF");
}

bool T00PrepareStratagemFiveUiBridge(bool reportFailure = true) {
  if (!s_earlyStratagemFiveEnabled) {
    s_startupStratagemBridgeReady = true;
    return true;
  }

  if (!PerfDiagnosticsEnabled()) {
    const bool ready = PrepareStratagemFiveUiBridge(reportFailure);
    s_startupStratagemBridgeReady = ready;
    return ready;
  }

  const uint64_t start = PerfRealNow100ns();
  const bool ready = PrepareStratagemFiveUiBridge(reportFailure);
  const uint64_t end = PerfRealNow100ns();
  PerfRecord(PerfMetric::StartupBridgePrepare,
             end >= start ? end - start : 0, 0, ready ? 1 : 0);
  s_startupStratagemBridgeReady = ready;
  return ready;
}

bool T01SetStratagemFiveFeature(bool enable) {
  if (enable && !s_earlyStratagemFiveEnabled)
    return true;
  return SetStratagemFiveFeature(enable);
}

void T01StartupSleep(DWORD milliseconds) {
  if (milliseconds == 100 && s_startupStratagemBridgeReady)
    return;
  ::Sleep(milliseconds);
}

template <typename... Args>
void T01AddLog(const char *fmt, Args &&...args) {
  if (!s_earlyStratagemFiveEnabled && fmt) {
    if (std::strcmp(fmt,
                    "[Stratagem5UI] early bridge preparation before startup delay") == 0) {
      AddLog("[Stratagem5UI] early bridge skipped: saved setting OFF");
      return;
    }
    if (std::strcmp(fmt,
                    "[Stratagem5UI] unified ID5 experiment armed before battle UI") == 0)
      return;
  }

  AddLog(fmt, std::forward<Args>(args)...);
}

bool T00InitCheats() {
  if (!PerfDiagnosticsEnabled())
    return InitCheats();

  const uint64_t start = PerfRealNow100ns();
  const bool ready = InitCheats();
  const uint64_t end = PerfRealNow100ns();
  PerfRecord(PerfMetric::InitCheatsAttempt,
             end >= start ? end - start : 0, 0, ready ? 1 : 0);
  return ready;
}

} // namespace DX11Base

namespace {

[[noreturn]] void WINAPI T05FreeLibraryAndExitThread(HMODULE module, DWORD exitCode) {
  // 정상 unload 경로에서만 실행됩니다. DllMain 종료 경로에서는 loader lock 아래
  // join하지 않도록 기존 terminating cleanup 경로를 그대로 유지합니다.
  DX11Base::ShutdownDebugScannerT05();
  ::FreeLibraryAndExitThread(module, exitCode);
}

} // namespace

#define LoadEarlyLogConfig T00LoadEarlyLogConfig
#define PrepareStratagemFiveUiBridge T00PrepareStratagemFiveUiBridge
#define SetStratagemFiveFeature T01SetStratagemFiveFeature
#define InitCheats T00InitCheats
#define AddLog T01AddLog
#define Sleep T01StartupSleep
#define FreeLibraryAndExitThread T05FreeLibraryAndExitThread
#include "Source_impl.inc"
#undef FreeLibraryAndExitThread
#undef Sleep
#undef AddLog
#undef InitCheats
#undef SetStratagemFiveFeature
#undef PrepareStratagemFiveUiBridge
#undef LoadEarlyLogConfig