#pragma once
#include <mutex>

namespace DX11Base {
  extern std::mutex g_logMutex;
  void AddLog(const char *fmt, ...);
  void showLoveLogs();
  std::string GetFullLogs();
  void ClearLogs();
  void SaveMemoryLog(uintptr_t p1);
} // namespace DX11Base