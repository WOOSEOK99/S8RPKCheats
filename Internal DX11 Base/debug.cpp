#define NOMINMAX
#include "debug.h"
#include "Cheats.h"
#include "Cheats\InstantLoveCave.h"
#include "Cheats\MonthCapture.h"
#include "Cheats\OfficerDetail.h"
#include "Cheats\SelectOfficercapture.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuState.h"
#include "pch.h"
#include "showcal.h"
#include "showlog.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace DX11Base {

  bool bShowDebug = false;
  static bool bShowOffset = false;

  bool bShowMemoryEditor = false;
  static uintptr_t hexEditorAddr = 0;
  static int hexEditorRows = 16;

  static bool SafeWriteMemory(uintptr_t address, unsigned char val) {
    bool success = false;
    __try {
      *(unsigned char *)address = val;
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }
    return success;
  }

  static bool SafeReadMemory(uintptr_t address, unsigned char *out_val) {
    bool success = false;
    __try {
      *out_val = *(unsigned char *)address;
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }
    return success;
  }

  static bool SafeReadMemoryRaw(uintptr_t address, void *buffer, size_t size) {
    bool success = false;
    __try {
      memcpy(buffer, (void *)address, size);
      success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      success = false;
    }
    return success;
  }

  // --- 변화 감지 데이터 ---
  static unsigned char s_refBuffer[4096];
  static uintptr_t s_refAddr = 0;
  static int s_refRows = 0;
  static bool s_isRefCaptured = false;

  // --- 메모리 검색기 엔진 ---

  enum ScanValueType { SVT_Byte = 0, SVT_2Bytes, SVT_4Bytes, SVT_8Bytes, SVT_Float, SVT_Double };

  struct ScanResult {
    uintptr_t address;
    unsigned long long valA = 0; // 첫 번째 스캔에서 찾은 값
    unsigned long long valB = 0; // 주변 검색/필터링으로 찾은 값
    int matchOffset = 0;
    bool hasValB = false;
  };

  enum ScanFilterMode { SFM_Fixed = 0, SFM_Nearby = 1 };

  struct ScannerSettings {
    ScanValueType type = SVT_8Bytes;
    unsigned long long valueU64 = 0;
    float valueFloat = 0.0f;
    double valueDouble = 0.0f;
    bool isHex = true;

    bool useFilter = false;
    ScanFilterMode filterMode = SFM_Fixed;
    int filterOffset = 0;
    int nearbyRange = 0x50;
  };

  static ScannerSettings s_scannerSettings;
  static std::vector<ScanResult> s_scanResults;
  static std::atomic<bool> s_isScanning{false};
  static std::atomic<bool> s_stopScan{false};
  static std::atomic<float> s_scanProgress{0.0f};
  static std::mutex s_resultsMutex;
  static int s_scannerLimit = 1000;

  static void InternalFirstScan(ScannerSettings settings) {
    s_isScanning = true;
    s_stopScan = false;
    s_scanProgress = 0.0f;

    {
      std::lock_guard<std::mutex> lock(s_resultsMutex);
      s_scanResults.clear();
      s_scanResults.shrink_to_fit();
    }

    uintptr_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi;
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    unsigned long long totalSize = 0;

    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
      if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
          (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
        regions.push_back(mbi);
        totalSize += mbi.RegionSize;
      }
      addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    size_t valSize = 1;
    if (settings.type == SVT_2Bytes)
      valSize = 2;
    else if (settings.type == SVT_4Bytes || settings.type == SVT_Float)
      valSize = 4;
    else if (settings.type == SVT_8Bytes || settings.type == SVT_Double)
      valSize = 8;

    unsigned long long processedSize = 0;
    for (const auto &region : regions) {
      if (s_stopScan)
        break;

      uintptr_t start = (uintptr_t)region.BaseAddress;
      uintptr_t end = start + region.RegionSize;
      const size_t bufferSize = 4096 * 16;
      std::vector<unsigned char> buffer(bufferSize + valSize);

      uintptr_t curr = start;
      while (curr < end) {
        if (s_stopScan)
          break;

        size_t remaining = (size_t)(end - curr);
        size_t toRead = (std::min)(remaining, bufferSize);

        if (toRead < valSize) {
          processedSize += toRead;
          break;
        }

        if (SafeReadMemoryRaw(curr, buffer.data(), toRead)) {
          size_t scanLimit = toRead - valSize;
          for (size_t i = 0; i <= scanLimit; i++) {
            bool match = false;
            unsigned char *p = buffer.data() + i;
            if (settings.type == SVT_Byte)
              match = (*p == (unsigned char)settings.valueU64);
            else if (settings.type == SVT_2Bytes)
              match = (*(unsigned short *)p == (unsigned short)settings.valueU64);
            else if (settings.type == SVT_4Bytes)
              match = (*(unsigned int *)p == (unsigned int)settings.valueU64);
            else if (settings.type == SVT_8Bytes)
              match = (*(unsigned long long *)p == settings.valueU64);
            else if (settings.type == SVT_Float)
              match = (*(float *)p == settings.valueFloat);
            else if (settings.type == SVT_Double)
              match = (*(double *)p == settings.valueDouble);

            if (match) {
              std::lock_guard<std::mutex> lock(s_resultsMutex);
              unsigned long long foundVal = 0;
              if (settings.type == SVT_Byte)
                foundVal = *p;
              else if (settings.type == SVT_2Bytes)
                foundVal = *(unsigned short *)p;
              else if (settings.type == SVT_4Bytes)
                foundVal = *(unsigned int *)p;
              else if (settings.type == SVT_8Bytes)
                foundVal = *(unsigned long long *)p;
              else if (settings.type == SVT_Float)
                foundVal = *(unsigned int *)p; // 비트 비교용
              else if (settings.type == SVT_Double)
                foundVal = *(unsigned long long *)p;

              s_scanResults.push_back({curr + i, foundVal, 0, 0, false});
              if (s_scanResults.size() > 100000)
                break;
            }
          }
        }

        if (s_scanResults.size() > 100000)
          break;

        if (toRead == bufferSize) {
          size_t advance = bufferSize - valSize + 1;
          curr += advance;
          processedSize += advance;
        } else {
          processedSize += toRead;
          curr = end;
        }
        s_scanProgress = (float)processedSize / (float)totalSize;
      }
      if (s_scanResults.size() > 100000 || s_stopScan)
        break;
    }

    s_isScanning = false;
    AddLog(u8"[스캐너] 검색 완료: %d개 발견", (int)s_scanResults.size());
  }

  static void InternalNextScan(ScannerSettings settings) {
    s_isScanning = true;
    s_stopScan = false;
    s_scanProgress = 0.0f;

    std::vector<ScanResult> inputResults;
    {
      std::lock_guard<std::mutex> lock(s_resultsMutex);
      inputResults = s_scanResults;
    }

    std::vector<ScanResult> nextResults;
    size_t total = inputResults.size();
    size_t valSize = 1;
    if (settings.type == SVT_2Bytes)
      valSize = 2;
    else if (settings.type == SVT_4Bytes || settings.type == SVT_Float)
      valSize = 4;
    else if (settings.type == SVT_8Bytes || settings.type == SVT_Double)
      valSize = 8;

    nextResults.reserve(total);

    for (size_t i = 0; i < total; i++) {
      if (s_stopScan)
        break;
      uintptr_t baseAddr = inputResults[i].address;
      bool found = false;
      int foundOffset = 0;
      unsigned long long foundValB = 0;

      if (settings.filterMode == SFM_Nearby && settings.nearbyRange > 0) {
        // 주변 범위 검색
        int range = settings.nearbyRange;
        uintptr_t startScan = (baseAddr > (uintptr_t)range) ? (baseAddr - range) : 0;
        size_t readSize = (size_t)(range * 2 + valSize);
        std::vector<unsigned char> neighborhood(readSize);

        if (startScan > 0 && SafeReadMemoryRaw(startScan, neighborhood.data(), readSize)) {
          for (size_t off = 0; off <= readSize - valSize; off++) {
            bool match = false;
            unsigned char *p = neighborhood.data() + off;
            if (settings.type == SVT_Byte)
              match = (*p == (unsigned char)settings.valueU64);
            else if (settings.type == SVT_2Bytes)
              match = (*(unsigned short *)p == (unsigned short)settings.valueU64);
            else if (settings.type == SVT_4Bytes)
              match = (*(unsigned int *)p == (unsigned int)settings.valueU64);
            else if (settings.type == SVT_8Bytes)
              match = (*(unsigned long long *)p == settings.valueU64);
            else if (settings.type == SVT_Float)
              match = (*(float *)p == settings.valueFloat);
            else if (settings.type == SVT_Double)
              match = (*(double *)p == settings.valueDouble);

            if (match) {
              found = true;
              foundOffset = (int)(startScan + off - baseAddr);
              // 발견된 값 B 저장
              if (settings.type == SVT_Byte)
                foundValB = *p;
              else if (settings.type == SVT_2Bytes)
                foundValB = *(unsigned short *)p;
              else if (settings.type == SVT_4Bytes)
                foundValB = *(unsigned int *)p;
              else if (settings.type == SVT_8Bytes)
                foundValB = *(unsigned long long *)p;
              else if (settings.type == SVT_Float)
                foundValB = *(unsigned int *)p;
              else if (settings.type == SVT_Double)
                foundValB = *(unsigned long long *)p;
              break;
            }
          }
        }
      } else {
        // 고정 오프셋 확인
        uintptr_t checkAddr = baseAddr + settings.filterOffset;
        unsigned char temp[8];
        if (SafeReadMemoryRaw(checkAddr, temp, valSize)) {
          if (settings.type == SVT_Byte)
            found = (temp[0] == (unsigned char)settings.valueU64), foundValB = temp[0];
          else if (settings.type == SVT_2Bytes)
            found = (*(unsigned short *)temp == (unsigned short)settings.valueU64), foundValB = *(unsigned short *)temp;
          else if (settings.type == SVT_4Bytes)
            found = (*(unsigned int *)temp == (unsigned int)settings.valueU64), foundValB = *(unsigned int *)temp;
          else if (settings.type == SVT_8Bytes)
            found = (*(unsigned long long *)temp == settings.valueU64), foundValB = *(unsigned long long *)temp;
          else if (settings.type == SVT_Float)
            found = (*(float *)temp == settings.valueFloat), foundValB = *(unsigned int *)temp;
          else if (settings.type == SVT_Double)
            found = (*(double *)temp == settings.valueDouble), foundValB = *(unsigned long long *)temp;

          if (found)
            foundOffset = settings.filterOffset;
        }
      }

      if (found) {
        ScanResult res = inputResults[i];
        res.valB = foundValB;
        res.matchOffset = foundOffset;
        res.hasValB = true;
        nextResults.push_back(res);
      }
      s_scanProgress = (float)i / (float)total;
    }

    {
      std::lock_guard<std::mutex> lock(s_resultsMutex);
      s_scanResults = std::move(nextResults);
      s_scanResults.shrink_to_fit();
    }

    s_isScanning = false;
    AddLog(u8"[스캐너] 필터링 완료: %d개 남음", (int)s_scanResults.size());
  }

  void renderMemoryScannerTab() {
    ImGui::BeginChild("ScannerControls", ImVec2(0, 160), true);

    ImGui::Columns(2, "ScannerCols", false);
    ImGui::SetColumnWidth(0, 320);

    // --- 검색 설정 ---
    const char *types[] = {"Byte", "2 Bytes", "4 Bytes", "8 Bytes", "Float", "Double"};
    ImGui::Combo(u8"값 타입", (int *)&s_scannerSettings.type, types, IM_ARRAYSIZE(types));

    ImGui::Checkbox(u8"Hex 입력", &s_scannerSettings.isHex);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    if (s_scannerSettings.type == SVT_Float)
      ImGui::InputFloat(u8"검색 값", &s_scannerSettings.valueFloat);
    else if (s_scannerSettings.type == SVT_Double)
      ImGui::InputDouble(u8"검색 값", &s_scannerSettings.valueDouble);
    else
      ImGui::InputScalar(u8"검색 값", s_scannerSettings.isHex ? ImGuiDataType_U64 : ImGuiDataType_S64,
                         &s_scannerSettings.valueU64, NULL, NULL, s_scannerSettings.isHex ? "%llX" : "%lld");

    ImGui::NextColumn();

    // --- 필터 모드 선택 ---
    ImGui::Checkbox(u8"필터링(Next Scan) 옵션", &s_scannerSettings.useFilter);
    if (s_scannerSettings.useFilter) {
      ImGui::RadioButton(u8"고정 위치", (int *)&s_scannerSettings.filterMode, SFM_Fixed);
      ImGui::SameLine();
      ImGui::RadioButton(u8"주변 검색", (int *)&s_scannerSettings.filterMode, SFM_Nearby);

      if (s_scannerSettings.filterMode == SFM_Fixed) {
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt(u8"오프셋(Hex)", &s_scannerSettings.filterOffset, 1, 100, ImGuiInputTextFlags_CharsHexadecimal);
      } else {
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt(u8"범위 ±(Hex)", &s_scannerSettings.nearbyRange, 1, 100, ImGuiInputTextFlags_CharsHexadecimal);
      }
    }

    ImGui::Columns(1);

    if (s_isScanning) {
      ImGui::ProgressBar(s_scanProgress, ImVec2(-1, 0), u8"스캐닝 중...");
      if (ImGui::Button(u8"스캔 중단", ImVec2(-1, 25)))
        s_stopScan = true;
    } else {
      if (ImGui::Button(u8"첫 검색 (First Scan)", ImVec2(160, 30))) {
        std::thread(InternalFirstScan, s_scannerSettings).detach();
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"다음 검색 (Next Scan)", ImVec2(160, 30))) {
        if (!s_scanResults.empty())
          std::thread(InternalNextScan, s_scannerSettings).detach();
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"결과 초기화", ImVec2(100, 30))) {
        std::lock_guard<std::mutex> lock(s_resultsMutex);
        s_scanResults.clear();
        s_scanResults.shrink_to_fit();
      }
    }

    ImGui::EndChild();

    // --- 결과 표시 ---
    ImGui::Text(u8"검색 결과: %d 개 (최대 %d 개 표시)", (int)s_scanResults.size(), s_scannerLimit);
    if (ImGui::BeginTable("ScannerResultsV4", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150);
      ImGui::TableSetupColumn(u8"Initial Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn(u8"Nearby Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn(u8"Offset", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableHeadersRow();

      std::lock_guard<std::mutex> lock(s_resultsMutex);
      int count = 0;
      for (const auto &res : s_scanResults) {
        if (++count > s_scannerLimit)
          break;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        char addrBuf[18];
        sprintf_s(addrBuf, "%016llX", res.address);
        if (ImGui::Selectable(addrBuf))
          hexEditorAddr = res.address;

        // Initial Value (첫 검색 시 찾은 값)
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%llX", res.valA);

        // Nearby Value (Next Scan으로 필터링한 값)
        ImGui::TableSetColumnIndex(2);
        if (res.hasValB) {
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%llX", res.valB);
        } else {
          ImGui::TextDisabled("-");
        }

        ImGui::TableSetColumnIndex(3);
        if (res.matchOffset != 0 || res.hasValB)
          ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%+X", res.matchOffset);
        else
          ImGui::TextDisabled("-");
      }
      ImGui::EndTable();
    }
  }

  void renderMemoryEditorWindow(uintptr_t gameBase, uintptr_t p1) {
    if (!bShowMemoryEditor)
      return;

    ImGui::SetNextWindowSize(ImVec2(750, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"메모리 에디터", &bShowMemoryEditor)) {

      ImGui::Text(u8"주소:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140);
      ImGui::InputScalar("##Addr", ImGuiDataType_U64, &hexEditorAddr, NULL, NULL, "%016llX",
                         ImGuiInputTextFlags_CharsHexadecimal);
      ImGui::SameLine();
      if (ImGui::Button(u8"붙여넣기")) {
        const char *clip = ImGui::GetClipboardText();
        if (clip) {
          unsigned long long val = 0;
          if (sscanf_s(clip, "%llx", &val) == 1)
            hexEditorAddr = (uintptr_t)val;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("GameBase"))
        hexEditorAddr = gameBase;
      ImGui::SameLine();
      if (ImGui::Button("Player"))
        hexEditorAddr = p1;

      ImGui::SameLine();
      ImGui::SetNextItemWidth(80);
      ImGui::InputInt(u8"행", &hexEditorRows);
      if (hexEditorRows < 1)
        hexEditorRows = 1;
      else if (hexEditorRows > 256)
        hexEditorRows = 256;

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
      if (ImGui::Button("+T")) {
        hexEditorAddr -= 16;
        hexEditorRows = (std::min)(hexEditorRows + 1, 256);
      }
      ImGui::SameLine();
      if (ImGui::Button("-T")) {
        if (hexEditorRows > 1) {
          hexEditorAddr += 16;
          hexEditorRows--;
        }
      }
      ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ImGui::Button(u8"클립보드 복사", ImVec2(120, 25))) {
        std::string clipboard;
        for (int row = 0; row < hexEditorRows; row++) {
          uintptr_t rowAddr = hexEditorAddr + (row * 16);
          char line[256];
          sprintf_s(line, sizeof(line), "%016llX: ", rowAddr);
          clipboard += line;
          char asciiStr[17];
          asciiStr[16] = '\0';
          for (int col = 0; col < 16; col++) {
            uintptr_t cellAddr = rowAddr + col;
            unsigned char val = 0;
            if (SafeReadMemory(cellAddr, &val)) {
              sprintf_s(line, sizeof(line), "%02X ", val);
              asciiStr[col] = (val >= 32 && val <= 126) ? (char)val : '.';
            } else {
              sprintf_s(line, sizeof(line), "?? ");
              asciiStr[col] = '?';
            }
            clipboard += line;
          }
          clipboard += "| ";
          clipboard += asciiStr;
          clipboard += "\n";
        }
        ImGui::SetClipboardText(clipboard.c_str());
        AddLog(u8"[메모리 에디터] 현재 데이터가 클립보드에 복사되었습니다.");
      }
      ImGui::SameLine();
      ImGui::Checkbox(u8"클릭 차단", &bBlockClickInMemoryEditor);
      ImGui::SameLine();
      if (ImGui::Button(u8"메모장", ImVec2(90, 25))) {
        bShowMemoryNotepadWin = !bShowMemoryNotepadWin;
      }

      // --- 제어 버튼 (2행) ---
      ImGui::Spacing();
      if (ImGui::Button(u8"현재 상태 캡처", ImVec2(130, 25))) {
        s_refAddr = hexEditorAddr;
        s_refRows = hexEditorRows;
        for (int i = 0; i < (std::min)(s_refRows * 16, 4096); i++) {
          unsigned char v = 0;
          if (SafeReadMemory(s_refAddr + i, &v))
            s_refBuffer[i] = v;
          else
            s_refBuffer[i] = 0;
        }
        s_isRefCaptured = true;
        AddLog(u8"[메모리 에디터] 현재 상태를 캡처했습니다. 변하는 값은 빨간색으로 표시됩니다.");
      }

      ImGui::SameLine();
      if (ImGui::Button(u8"캡처 초기화", ImVec2(100, 25))) {
        s_isRefCaptured = false;
      }

      ImGui::SameLine();
      if (s_isRefCaptured) {
        ImGui::SameLine();
        if (ImGui::Button(u8"기준 초기화"))
          s_isRefCaptured = false;
      }


      if (ImGui::BeginTabBar("MemTabs")) {
        if (ImGui::BeginTabItem(u8"헥스 뷰어")) {
          // 헥스 에디터 테이블 (기존 동일)
          if (ImGui::BeginTable("##Grid", 18, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                                ImVec2(0, ImGui::GetContentRegionAvail().y - 20))) {
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 130);
            for (int i = 0; i < 16; i++) {
              char b[4];
              sprintf_s(b, "%02X", i);
              ImGui::TableSetupColumn(b, ImGuiTableColumnFlags_WidthFixed, 25);
            }
            ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int r = 0; r < hexEditorRows; r++) {
              uintptr_t rowAddr = hexEditorAddr + (r * 16);
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::Text("%016llX", rowAddr);
              char ascii[17];
              ascii[16] = '\0';
              for (int c = 0; c < 16; c++) {
                ImGui::TableSetColumnIndex(c + 1);
                unsigned char v = 0;
                if (SafeReadMemory(rowAddr + c, &v)) {
                  ImGui::PushID(r * 16 + c);
                  ImGui::SetNextItemWidth(25);

                  bool isChanged = false;
                  if (s_isRefCaptured) {
                    uintptr_t cellAddr = rowAddr + c;
                    // 현재 바이트 위치가 캡처된 영역 내에 있는지 확인 (최대 4096바이트)
                    if (cellAddr >= s_refAddr && cellAddr < (s_refAddr + (std::min)(s_refRows * 16, 4096))) {
                      size_t refIdx = (size_t)(cellAddr - s_refAddr);
                      if (v != s_refBuffer[refIdx]) {
                        isChanged = true;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f)); // 빨간색 강조
                      }
                    }
                  }

                  if (ImGui::InputScalar("##v", ImGuiDataType_U8, &v, NULL, NULL, "%02X",
                                         ImGuiInputTextFlags_CharsHexadecimal)) {
                    SafeWriteMemory(rowAddr + c, v);
                    if (s_isRefCaptured) {
                      uintptr_t cellAddr = rowAddr + c;
                      if (cellAddr >= s_refAddr && cellAddr < (s_refAddr + 4096)) {
                        s_refBuffer[cellAddr - s_refAddr] = v;
                      }
                    }
                  }

                  if (isChanged)
                    ImGui::PopStyleColor();

                  ImGui::PopID();
                  ascii[c] = (v >= 32 && v <= 126) ? (char)v : '.';
                } else {
                  ImGui::TextDisabled("??");
                  ascii[c] = '?';
                }
              }
              ImGui::TableSetColumnIndex(17);
              ImGui::TextUnformatted(ascii);
            }
            ImGui::EndTable();
          }
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"검색기 (Scanner)")) {
          renderMemoryScannerTab();
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::End();
    }
  }

  void debuging(uintptr_t gameBase, uintptr_t p1) {
    if (bShowDebug) {
      if (ImGui::Begin(u8"디버그 메뉴", &bShowDebug)) {
        if (ImGui::Button(u8"기본 포인터 검색 (초기화)", ImVec2(-1, 30))) {
          DX11Base::InitCheats();
        }
        if (ImGui::Button(u8"메모리 에디터 열기", ImVec2(160, 30)))
          bShowMemoryEditor = true;
        ImGui::SameLine();
        if (ImGui::Button(u8"로그 복사", ImVec2(160, 30))) {
          ImGui::SetClipboardText(GetFullLogs().c_str());
          AddLog(u8"[Debug] 모든 로그가 클립보드에 복사되었습니다.");
        }

        ImGui::Separator();
        ImGui::Text("GameBase: %llX", gameBase);
        ImGui::Text("Player: %llX", p1);
        ImGui::Separator();
        showLoveLogs();
      }
      ImGui::End();
    }
    renderMemoryEditorWindow(gameBase, p1);
  }

} // namespace DX11Base