#include <mutex>

// [컴파일 타임 로그 관리]
// 로그를 끄고 싶을 때는 아래줄을 주석 처리(//) 하세요.
// #define ENABLE_DEBUG_LOG

namespace DX11Base {
  extern std::mutex g_logMutex;
  void AddLog(const char *fmt, ...);
  void showLoveLogs();
  std::string GetFullLogs();
  void SaveMemoryLog(uintptr_t p1);
} // namespace DX11Base
