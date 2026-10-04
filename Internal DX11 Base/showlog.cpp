#include "showlog.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include "PerformanceDiagnostics.h"
#include "debug.h"
#include <Windows.h>
#include <atomic>
#include <exception>
#include <utility>
#include <cstdarg>
#include <condition_variable>
#include <deque>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace DX11Base {
  extern bool bShowDebug;

  std::vector<std::string> g_loveLogs;
  std::mutex g_logMutex;
  // 이전 데이터를 저장할 버퍼 (구조체 크기 0x3D0 만큼)
  static unsigned char g_OldData[0x3D0] = {0};
  static bool g_FirstRun = true;

  // Owned by the initialization/MainThread unload path, never by DllMain.
  // The module reference prevents an unexpected FreeLibrary from unmapping
  // worker code. Process termination lets Windows stop threads without a join.
  static std::mutex g_fileLogMutex;
  static std::condition_variable g_fileLogWake;
  static std::deque<std::string> g_fileLogQueue;
  static size_t g_fileLogQueueBytes = 0;
  static constexpr size_t kFileLogMaxEntries = 4096;
  static constexpr size_t kFileLogMaxBytes = 4 * 1024 * 1024;
  static std::atomic<uint64_t> g_fileLogDropped{0};
  static HANDLE g_fileLogThread = nullptr;
  static HMODULE g_fileLogModule = nullptr;
  static bool g_fileLogStopping = false;

  static unsigned __stdcall FileLogWriter(void *) {
    std::ofstream file;
    for (;;) {
      std::deque<std::string> batch;
      bool stopping;
      {
        std::unique_lock<std::mutex> lock(g_fileLogMutex);
        g_fileLogWake.wait(lock, [] {
          return g_fileLogStopping || !g_fileLogQueue.empty();
        });
        batch.swap(g_fileLogQueue);
        g_fileLogQueueBytes = 0;
        stopping = g_fileLogStopping;
      }
      // No queue/UI mutex is held during any disk operation. At most one
      // bounded batch is in flight in addition to the bounded producer queue.
      const bool hasIo = !batch.empty() || (stopping && file.is_open());
      const bool perfEnabled = hasIo && PerfDiagnosticsEnabled();
      const uint64_t start = perfEnabled ? PerfRealNow100ns() : 0;
      uint64_t bytesWritten = 0;
      try {
        if (!batch.empty() && !file.is_open()) {
          file.clear();
          file.open("S8RPK_cheat.log", std::ios::app | std::ios::binary |
                                         std::ios::ate);
          if (file.is_open() && file.tellp() == std::streampos(0)) {
            const char bom[] = {char(0xEF), char(0xBB), char(0xBF)};
            file.write(bom, sizeof(bom));
            if (file)
              bytesWritten += sizeof(bom);
          }
        }
        if (!batch.empty()) {
          for (const auto &entry : batch) {
            file.write(entry.data(), static_cast<std::streamsize>(entry.size()));
            if (file)
              bytesWritten += static_cast<uint64_t>(entry.size());
          }
          file.flush();
          if (!file) {
            g_fileLogDropped.fetch_add(batch.size(), std::memory_order_relaxed);
            if (file.is_open())
              file.close();
          }
        }
        if (stopping && file.is_open())
          file.close();
      } catch (...) {
        // Never report an I/O failure through AddLog (which would recurse).
        g_fileLogDropped.fetch_add(batch.size(), std::memory_order_relaxed);
        if (file.is_open())
          file.close();
      }
      if (perfEnabled) {
        const uint64_t end = PerfRealNow100ns();
        PerfRecordNoReport(PerfMetric::AddLogFileIo,
                           end >= start ? end - start : 0, bytesWritten);
      }
      if (stopping)
        return 0;
    }
  }

  void StartFileLogWriter() {
    // Called once before the first startup log, outside loader lock, even
    // when file logging is OFF so a later UI toggle can enqueue immediately.
    std::lock_guard<std::mutex> lock(g_fileLogMutex);
    if (g_fileLogThread || g_fileLogStopping)
      return;
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCSTR>(&StartFileLogWriter),
                           &module))
      return;
    const uintptr_t thread = _beginthreadex(nullptr, 0, FileLogWriter,
                                           nullptr, 0, nullptr);
    if (!thread) {
      FreeLibrary(module);
      return;
    }
    g_fileLogModule = module;
    g_fileLogThread = reinterpret_cast<HANDLE>(thread);
  }

  void ShutdownFileLogWriter() {
    // Only the owning MainThread calls this, after other logging workers stop.
    // DllMain/Shutdown(true) must never call it: the wait needs loader progress.
    HANDLE thread;
    {
      std::lock_guard<std::mutex> lock(g_fileLogMutex);
      g_fileLogStopping = true; // Reject late producers; never restart.
      thread = g_fileLogThread;
    }
    g_fileLogWake.notify_one();
    if (!thread)
      return;
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0)
      std::terminate(); // Do not proceed to unload with a live writer.
    CloseHandle(thread);
    HMODULE module;
    {
      std::lock_guard<std::mutex> lock(g_fileLogMutex);
      g_fileLogThread = nullptr;
      module = g_fileLogModule;
      g_fileLogModule = nullptr;
    }
    // MainThread still owns the original DLL reference until its final
    // FreeLibraryAndExitThread; the CRT worker has fully exited at this point.
    FreeLibrary(module);
  }

  uint64_t GetFileLogDropCount() {
    return g_fileLogDropped.load(std::memory_order_relaxed);
  }

  static bool IsValidUtf8(const char *text) {
    if (!text)
      return false;

    const unsigned char *p =
        reinterpret_cast<const unsigned char *>(text);
    while (*p) {
      if (*p < 0x80) {
        ++p;
        continue;
      }

      int trailing = 0;
      uint32_t codePoint = 0;
      if ((*p & 0xE0) == 0xC0) {
        trailing = 1;
        codePoint = *p & 0x1F;
        if (codePoint < 0x02)
          return false;
      } else if ((*p & 0xF0) == 0xE0) {
        trailing = 2;
        codePoint = *p & 0x0F;
      } else if ((*p & 0xF8) == 0xF0) {
        trailing = 3;
        codePoint = *p & 0x07;
        if (codePoint > 0x04)
          return false;
      } else {
        return false;
      }

      ++p;
      for (int i = 0; i < trailing; ++i, ++p) {
        if ((*p & 0xC0) != 0x80)
          return false;
        codePoint = (codePoint << 6) | (*p & 0x3F);
      }

      if ((trailing == 2 && codePoint < 0x800) ||
          (trailing == 3 && codePoint < 0x10000) ||
          (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
          codePoint > 0x10FFFF) {
        return false;
      }
    }
    return true;
  }

  static std::string NormalizeLogTextToUtf8(const char *text) {
    if (!text)
      return {};

    if (IsValidUtf8(text))
      return std::string(text);

    // 기존 일반 문자열 리터럴 중 CP949 바이트가 섞인 경우 UTF-8로 변환합니다.
    constexpr UINT kKoreanCodePage = 949;
    int wideLength =
        MultiByteToWideChar(kKoreanCodePage, 0, text, -1, nullptr, 0);
    if (wideLength <= 0)
      return std::string(text);

    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(kKoreanCodePage, 0, text, -1,
                            wide.data(), wideLength) <= 0) {
      return std::string(text);
    }

    int utf8Length =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                            nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
      return std::string(text);

    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                            utf8.data(), utf8Length,
                            nullptr, nullptr) <= 0) {
      return std::string(text);
    }

    if (!utf8.empty() && utf8.back() == '\0')
      utf8.pop_back();
    return utf8;
  }

  // 로그 추가 함수
  void AddLog(const char *fmt, ...) {
    // 디버그 또는 파일 로그 옵션이 모두 꺼져있다면 아무것도 하지 않음
    if (!bShowDebug && !bFileLog)
      return;

    const bool perfEnabled = PerfDiagnosticsEnabled();
    const uint64_t callStart = perfEnabled ? PerfRealNow100ns() : 0;

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (bShowDebug) {
      const uint64_t lockStart = perfEnabled ? PerfRealNow100ns() : 0;
      std::unique_lock<std::mutex> lock(g_logMutex);
      if (perfEnabled) {
        const uint64_t lockEnd = PerfRealNow100ns();
        PerfRecordNoReport(PerfMetric::AddLogMutexWait,
                           lockEnd >= lockStart ? lockEnd - lockStart : 0);
      }
      g_loveLogs.push_back(buf);
      if (g_loveLogs.size() > 1000)
        g_loveLogs.erase(g_loveLogs.begin());
    }

    if (bFileLog) {
      // Capture producer time and preserve the original UTF-8/CP949 behavior.
      auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      struct tm tm_info;
      localtime_s(&tm_info, &now);
      std::stringstream ss;
      ss << "[" << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S")
         << "] " << NormalizeLogTextToUtf8(buf) << "\r\n";
      std::string entry = ss.str();
      bool queued = false;
      {
        std::lock_guard<std::mutex> lock(g_fileLogMutex);
        if (!g_fileLogStopping && g_fileLogThread &&
            g_fileLogQueue.size() < kFileLogMaxEntries &&
            entry.size() <= kFileLogMaxBytes - g_fileLogQueueBytes) {
          const size_t bytes = entry.size();
          g_fileLogQueue.push_back(std::move(entry));
          g_fileLogQueueBytes += bytes;
          queued = true;
        } else {
          g_fileLogDropped.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (queued)
        g_fileLogWake.notify_one();
    }

    if (perfEnabled) {
      const uint64_t callEnd = PerfRealNow100ns();
      PerfRecordNoReport(PerfMetric::AddLogCall,
                         callEnd >= callStart ? callEnd - callStart : 0);
    }
  }

  static char logFilter[256] = "";

  void showLoveLogs() {
    // 1. 현재 배율 가져오기
    float scale = ImGui::GetIO().FontGlobalScale;

    // 2. 제목 출력
    // ImGui::Spacing();
    // ImGui::Separator();
    // ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // 노란색
    // ImGui::Text(u8"실행 로그");
    // ImGui::PopStyleColor();

    // 3. 필터 입력란
    ImGui::Text(u8"필터:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(-100.0f * scale);
    ImGui::InputText(u8"##LogFilter", logFilter, IM_ARRAYSIZE(logFilter));
    
    ImGui::SameLine();
    if (ImGui::Button(u8"지우기", ImVec2(-1, 0))) {
        logFilter[0] = '\0';
    }

    // 4. 로그 창 높이 결정 (남은 영역 전체 사용)
    float logWindowHeight = ImGui::GetContentRegionAvail().y;
    if (logWindowHeight < 100.0f) logWindowHeight = 100.0f; // 최소 높이 보장

    ImGui::BeginChild("LoveLogWindow", ImVec2(0, logWindowHeight), true, ImGuiWindowFlags_HorizontalScrollbar);

    {
      std::lock_guard<std::mutex> lock(g_logMutex);
      if (g_loveLogs.empty()) {
        ImGui::TextDisabled(u8"대기 중...");
      } else {
        std::string filterStr = logFilter;
        bool useFilter = !filterStr.empty();

        for (const auto &log : g_loveLogs) {
          // 필터링 로직 (대소문자 구분함)
          if (useFilter && log.find(filterStr) == std::string::npos)
              continue;

          ImGui::TextUnformatted(log.c_str());
        }
      }
    }

    // 5. 자동 스크롤 로직 (최하단 고정)
    // 로그가 추가될 때마다 자동으로 바닥으로 내려줍니다.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
  }

  std::string GetFullLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::string result;
    std::string filterStr = logFilter;
    bool useFilter = !filterStr.empty();

    for (const auto &log : g_loveLogs) {
      if (!useFilter || log.find(filterStr) != std::string::npos) {
        result += log + "\n";
      }
    }
    return result;
  }

  void ClearLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_loveLogs.clear();
  }

  void SaveMemoryLog(uintptr_t p1) {
    if (p1 == 0)
      return;

    std::string logPath = "officer_log.txt";
    bool isNew = !std::filesystem::exists(logPath) || std::filesystem::file_size(logPath) == 0;
    std::ofstream logFile(logPath, std::ios::app | std::ios::binary);
    if (!logFile.is_open())
      return;

    if (isNew) {
      unsigned char bom[] = {0xEF, 0xBB, 0xBF};
      logFile.write((char *)bom, sizeof(bom));
    }

    bool changed = false;

    for (int i = 0xBF7; i < 0xBF7 + 0x3D0; i++) {
      unsigned char currentVal = *(unsigned char *)(p1 + i);
      int bufIdx = i - 0xBF7;

      // 처음 실행할 때는 현재 값을 복사만 함
      if (g_FirstRun) {
        g_OldData[bufIdx] = currentVal;
        continue;
      }

      // 값이 변했다면!
      if (g_OldData[bufIdx] != currentVal) {
        // 시간 기록
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        struct tm tm_info;
        localtime_s(&tm_info, &now);

        std::stringstream ss;
        ss << "[" << std::put_time(&tm_info, "%H:%M:%S") << "] ";
        ss << "Offset +0x" << std::hex << std::uppercase << i << " Changed: " << (int)g_OldData[bufIdx] << " -> "
           << (int)currentVal << "\r\n";

        std::string entry = ss.str();
        logFile.write(entry.c_str(), entry.size());

        g_OldData[bufIdx] = currentVal; // 값 업데이트
        changed = true;
      }
    }

    if (g_FirstRun)
      g_FirstRun = false;
    logFile.close();
  }
} // namespace DX11Base