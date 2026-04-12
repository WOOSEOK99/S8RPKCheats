#pragma once
#include "Framework/imgui.h"
#include "OfficerData.h"
#include <cstdint>

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
  void DrawSelectedOfficerWindow(ImVec2 mPos, ImVec2 mSize, float scale, bool asChild = false);

  void DrawOfficerListWindow(uintptr_t p1, float scale);
  void DrawOfficerTalents(uintptr_t pBase, float scale);
  void DrawOfficerHeader(uintptr_t pGame, float scale, uintptr_t pViewSnap = 0);
  
  void StartSpouseScannerAsync();
  void DrawSpouseListWindow(float scale);
  void DrawSpecialtyInfoWindow(float scale);
} // namespace DX11Base