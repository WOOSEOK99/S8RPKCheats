#include "showlog.h"
#include "Cheats.h"
#include "Engine.h"
#include "Menu.h"
#include "pch.h"
#include <cstdarg>
#include <string>
#include <vector>


#include <chrono>
#include <fstream>
#include <iomanip>

namespace DX11Base {

  std::vector<std::string> g_loveLogs;
  std::mutex g_logMutex;
  // 이전 데이터를 저장할 버퍼 (구조체 크기 0x3D0 만큼)
  static unsigned char g_OldData[0x3D0] = {0};
  static bool g_FirstRun = true;

  // 로그 추가 함수
  void AddLog(const char *fmt, ...) {
#ifdef ENABLE_DEBUG_LOG
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_loveLogs.push_back(buf);

    // 로그가 너무 많아지면 메모리 관리를 위해 앞부분 삭제 (선택 사항)
    if (g_loveLogs.size() > 50) {
      g_loveLogs.erase(g_loveLogs.begin());
    }
#endif
  }

  void showLoveLogs() {
    // 1. 현재 배율 가져오기
    float scale = ImGui::GetIO().FontGlobalScale;

    // 2. 제목 출력
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // 노란색
    ImGui::Text(u8"실행 로그");
    ImGui::PopStyleColor();

    // 3. 로그 창 너비와 높이 결정
    // 너비 0은 현재 사용 가능한 가로 폭 전체를 채웁니다 (정렬된 버튼 라인에 맞춰짐)
    // 높이는 배율에 맞게 조절 (기본 150 * scale 정도면 적당합니다)
    float logWindowHeight = 150.0f * scale;

    ImGui::BeginChild("LoveLogWindow", ImVec2(0, logWindowHeight), true, ImGuiWindowFlags_HorizontalScrollbar);

    {
      std::lock_guard<std::mutex> lock(g_logMutex);
      if (g_loveLogs.empty()) {
        ImGui::TextDisabled(u8"대기 중...");
      } else {
        // 최신 로그가 아래로 쌓이는 구조라면 그대로 출력
        for (const auto &log : g_loveLogs) {
          ImGui::TextUnformatted(log.c_str());
        }
      }
    }

    // 4. 자동 스크롤 로직 (최하단 고정)
    // 로그가 추가될 때마다 자동으로 바닥으로 내려줍니다.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
  }

  void SaveMemoryLog(uintptr_t p1) {
    if (p1 == 0)
      return;

    // 파일 열기 (ios::app 모드로 기존 내용 뒤에 이어서 기록)
    std::ofstream logFile("officer_log.txt", std::ios::app);
    if (!logFile.is_open())
      return;

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
        logFile << "[" << std::put_time(&tm_info, "%H:%M:%S") << "] ";
        logFile << "Offset +0x" << std::hex << std::uppercase << i << " Changed: " << (int)g_OldData[bufIdx] << " -> "
                << (int)currentVal << std::endl;

        g_OldData[bufIdx] = currentVal; // 값 업데이트
        changed = true;
      }
    }

    if (g_FirstRun)
      g_FirstRun = false;
    logFile.close();
  }
} // namespace DX11Base
