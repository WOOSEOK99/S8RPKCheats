#pragma once
#include <mutex>
#include <cstdint>

namespace DX11Base {
  extern std::mutex g_logMutex;
  void AddLog(const char *fmt, ...);
  // MainThread ownership only; neither lifecycle function belongs in DllMain.
  void StartFileLogWriter();
  void ShutdownFileLogWriter();
  uint64_t GetFileLogDropCount();
  void showLoveLogs();
  std::string GetFullLogs();
  void ClearLogs();
  void SaveMemoryLog(uintptr_t p1);
} // namespace DX11Base