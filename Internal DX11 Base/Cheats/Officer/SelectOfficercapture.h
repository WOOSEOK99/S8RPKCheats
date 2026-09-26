#pragma once
#define DX11BASE_SELECT_OFFICER_CAPTURE_HEADER_INCLUDED 1

#include "../../pch.h"
#include "Framework/imgui.h"
#include "OfficerData.h"
#include "CustomTraitDisplay.h"
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <functional>

namespace DX11Base {
  /*
   * [ 메모리 오프셋 정리 ]
   * 
   * 1. 무장 마스터 데이터 (Officer Master Array) - Stride: 0x3D0
   *    +0x08 : 무장 고유 ID (unsigned short)
   *    +0x10 : 상태 / VTable Byte (uint8) - (0x18:군사, 0x28:일반, 0x58:재야, 0x88:사망, 0xC8:군주 등)
   *    +0x18 : 소속 세력 주소 (uintptr_t)
   *    +0x20 : 소속 도시/군단 주소 (uintptr_t)
   *    +0x2E : 얼굴 번호 / CG ID (unsigned short)
   *    +0x30 : 성별 (uint8) - (0:남성, 1:여성)
   *    +0x32 : 등장년도 (unsigned short)
   *    +0x34 : 생년 (unsigned short)
   *    +0x36 : 몰년 / 수명 (unsigned short)
   *    +0x40 : 가문 포인터 (uintptr_t)
   *    +0x48 : 부친 포인터 (uintptr_t)
   *    +0x50 : 모친 포인터 (uintptr_t)
   *    +0x60 : 전략 성향 (uint8) - (1:소극, 2:보통, 3:호전, 4:적극, 5:사욕)
   *    +0x64 : 성격 (uint8) - (1:대담, 2:저돌, 3:온화, 4:침착, 5:나약)
   *    +0xA5 : 모델 번호 (uint8)
   *    +0xA6 : 모델 색상 (uint8)
   *    +0x100: 공적 (unsigned short)
   *    +0x104: 문명 (unsigned short)
   *    +0x106: 무명 (unsigned short)
   *    +0x108: 악명 (unsigned short)
   *    +0x374: 사망 플래그 (uint32) - (0:생존, 그외:사망관련)
   * 
   * 2. 인연/관계 데이터 (Relationship - Spouse 스캐너 기준)
   *    -0x08 : 인연 플래그 (uint8) - (0x02: 배우자)
   *    +0x00 : 주인공(Hero) 주소 (uintptr_t)
   *    +0x08 : 대상 무장(Spouse) 주소 (uintptr_t)
   *    +0x28 : 슬롯 번호 / 순서 (uint32)
   * 
   * 3. 세력 데이터 (Force Data)
   *    +0x09 : 세력 색상 (uint8)
   * 
   * 4. 도시 데이터 (City Data)
   *    Stride: 0x2A0
   */

  extern uintptr_t g_capturedOfficerBase;
  uintptr_t GetGameBaseFast();
  bool SetTraitID(uintptr_t base, int slot, uint16_t traitID);

#ifndef DX11BASE_OFFICER_DETAIL_HEADER_INCLUDED
  // [모든 무장 UI 고속 경로]
  // 기존 SetTraitID는 캐시 미스 시 5102명 x 3슬롯 전체 검색과 프로세스 메모리
  // 스캔까지 수행할 수 있습니다. 런타임 기재 포인터 배열에서 ID로 바로 찾아
  // 슬롯에 기록하고, 구조가 예상과 다를 때만 기존 경로로 폴백합니다.
  inline bool SetTraitIDForOfficerUiFast(uintptr_t officerBase, int slotIndex, uint16_t traitID) {
    if (officerBase < 0x10000 || slotIndex < 0 || slotIndex >= 3 || traitID == 0)
      return false;

    constexpr uintptr_t kTraitPointerArrayOffset = 0x57A4D0;
    const uintptr_t gameDataRoot = GetGameBaseFast();
    if (gameDataRoot > 0x10000) {
      __try {
        const uintptr_t targetTrait =
            *reinterpret_cast<const uintptr_t *>(
                gameDataRoot + kTraitPointerArrayOffset +
                static_cast<uintptr_t>(traitID) * sizeof(uintptr_t));

        if (targetTrait > 0x10000) {
          const uintptr_t vtable =
              *reinterpret_cast<const uintptr_t *>(targetTrait);
          const uint16_t actualId =
              *reinterpret_cast<const uint16_t *>(targetTrait + 0x08);

          if (vtable > 0x10000 && actualId == traitID) {
            uintptr_t *slotPtr = reinterpret_cast<uintptr_t *>(
                officerBase + 0x88 +
                static_cast<uintptr_t>(slotIndex) * sizeof(uintptr_t));
            *slotPtr = targetTrait;

            const uintptr_t verifyTrait = *slotPtr;
            if (verifyTrait == targetTrait &&
                *reinterpret_cast<const uint16_t *>(verifyTrait + 0x08) == traitID) {
              return true;
            }
          }
        }
      }
      __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    return SetTraitID(officerBase, slotIndex, traitID);
  }
#endif

  void DrawSelectedOfficerWindow(ImVec2 mPos, ImVec2 mSize, float scale, bool asChild = false);

  void DrawOfficerListWindow(uintptr_t p1, float scale);
  void DrawOfficerTalents(uintptr_t pBase, float scale, uintptr_t pGame = 0);
  void DrawOfficerHeader(uintptr_t pGame, float scale, uintptr_t pViewSnap = 0);
  
  void StartSpouseScannerAsync();
  void DrawSpouseListWindow(float scale);
  void DrawSpecialtyInfoWindow(float scale);
  void UpdateAutoSpecialtyDistribution(bool isCouncil);

  // 메인 UI용: 모든 유효 무장의 빈 기재 슬롯을 등급 필터에 맞춰 랜덤으로 채웁니다.
  void OpenBatchRandomTraitAssignmentWindow();
  void DrawBatchRandomTraitAssignmentWindow(float scale);

  // 다중 선택 무장 일괄 패치 API
  size_t GetSelectedOfficerIDCount();
  void   ApplyPatchToSelectedOfficers(std::function<void(uintptr_t)> patchFn);
} // namespace DX11Base

#ifndef DX11BASE_OFFICER_DETAIL_HEADER_INCLUDED
// SelectOfficercapture.cpp의 개별/랜덤 기재 변경 호출만 고속 경로로 보냅니다.
#define SetTraitID SetTraitIDForOfficerUiFast
#endif

// DrawOfficerTalents()의 기존 Unknown 분기는
//   "[*] 기재 N : #ID (ID ID)"
// 형식으로 출력합니다. 이 한 형식에 대해서만 san8r_traits_config.json의
// customNames[ID-1]을 보조 조회해 커스텀 기재명/설명을 같은 줄에 표시합니다.
// 그 외 TextColored 호출과 아래 effect_definitions.json 기반 회색 효과 출력은 그대로 전달합니다.
namespace ImGui {
inline void CustomTraitAwareTextColored(const ImVec4 &col, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  static const char *kUnknownTraitFormat = u8"[*] 기재 %d : #%d (ID %d)";
  if (fmt && std::strcmp(fmt, kUnknownTraitFormat) == 0) {
    va_list readArgs;
    va_copy(readArgs, args);
    const int slotNo = va_arg(readArgs, int);
    (void)va_arg(readArgs, int); // 기존 #ID 인자
    const int traitId = va_arg(readArgs, int);
    va_end(readArgs);

    DX11Base::CustomTraitDisplayInfo custom;
    if (traitId > 0 && DX11Base::GetCustomTraitDisplayInfo(static_cast<uint16_t>(traitId), custom) &&
        !custom.name.empty()) {
      va_end(args);
      if (!custom.desc.empty()) {
        ImGui::TextColored(col, u8"[*] 기재 %d : %s (ID %d) - %s", slotNo, custom.name.c_str(), traitId,
                           custom.desc.c_str());
      } else {
        ImGui::TextColored(col, u8"[*] 기재 %d : %s (ID %d)", slotNo, custom.name.c_str(), traitId);
      }
      return;
    }
  }

  ImGui::TextColoredV(col, fmt, args);
  va_end(args);
}
} // namespace ImGui

// 이 헤더를 포함한 기존 소스의 ImGui::TextColored 호출을 투명하게 감쌉니다.
// CustomTraitAwareTextColored 내부 정의 뒤에 두어 자기 자신에는 적용되지 않습니다.
#define TextColored CustomTraitAwareTextColored
