#include "showlog.h"
#include "Framework/imgui.h"
#include "MenuState.h"
#include "debug.h"
#include <cstdarg>
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

  // 로그 추가 함수
  void AddLog(const char *fmt, ...) {
    // 디버그 또는 파일 로그 옵션이 모두 꺼져있다면 아무것도 하지 않음
    if (!bShowDebug && !bFileLog)
      return;

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_logMutex);
    
    // UI 디버그용 메모리 저장
    if (bShowDebug) {
      g_loveLogs.push_back(buf);
      if (g_loveLogs.size() > 1000) { // 로그 저장 개수 상향 (50 -> 1000)
        g_loveLogs.erase(g_loveLogs.begin());
      }
    }

    // 파일 로그용 저장
    if (bFileLog) {
      std::string logPath = "S8RPK_cheat.log";
      bool isNew = !std::filesystem::exists(logPath) || std::filesystem::file_size(logPath) == 0;
      
      std::ofstream logFile(logPath, std::ios::app | std::ios::binary);
      if (logFile.is_open()) {
          if (isNew) {
              unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
              logFile.write((char*)bom, sizeof(bom));
          }
          auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
          struct tm tm_info;
          localtime_s(&tm_info, &now);
          
          std::stringstream ss;
          ss << "[" << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S") << "] " << buf << "\r\n";
          std::string entry = ss.str();
          logFile.write(entry.c_str(), entry.size());
          logFile.close();
      }
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
