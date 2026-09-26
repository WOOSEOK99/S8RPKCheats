#pragma once
#define DX11BASE_OFFICER_DETAIL_HEADER_INCLUDED 1

#include "../../Framework/imgui.h"
#include "../../pch.h"
#include <string>
#include <vector>
#include <unordered_map>

// SelectOfficercapture.h가 먼저 포함된 번역 단위에서는 SetTraitID 매크로를
// 실제 API 선언을 읽는 동안만 해제하고, 헤더 끝에서 동일 고속 경로로 복구합니다.
#ifdef SetTraitID
#undef SetTraitID
#define DX11BASE_RESTORE_FAST_SET_TRAIT_ID 1
#endif

namespace DX11Base {
  extern bool bShowOfficerDetail;
  extern bool bShowSelectedOfficerWin;

  extern const char *traitNames[];

  void SetupTableHeaders(float scale);
  void RenderStatRow(uintptr_t pBase, const char *label, uintptr_t offset, int size, int *outValue, float scale);
  void RenderCompactSkill(uintptr_t pBase, const char *label, uintptr_t offset, int *outValue, float scale);
  void RenderResearchRow(uintptr_t pBase, const char *catLabel, const char *items[], uintptr_t offsets[], int *vars[], int count, float scale, int expOffset = -1);
  
  uint16_t GetTraitID(uintptr_t base, int slot);
  bool SetTraitID(uintptr_t base, int slot, uint16_t traitID);

  // 일괄 랜덤 부여용 고속 경로:
  // 필요한 기재 객체를 전체 메모리 스캔 최대 1회로 준비하고,
  // 이후 슬롯에는 검증된 객체 포인터를 직접 기록합니다.
  bool SeedTraitObjectsForBatch(
      const std::vector<uint16_t>& traitIDs,
      std::unordered_map<uint16_t, uintptr_t>& outObjects,
      uintptr_t& outTraitVtable);
  bool ScanTraitObjectsForBatchStep(
      const std::vector<uint16_t>& traitIDs,
      std::unordered_map<uint16_t, uintptr_t>& outObjects,
      uintptr_t traitVtable,
      uintptr_t& scanAddress,
      size_t maxReadableBytes,
      bool& finished);
  bool SetTraitObjectFast(
      uintptr_t officerBase, int slot, uint16_t traitID, uintptr_t traitObject);

  uintptr_t GetSelectedOfficerBase();

  void RenderBasicTab(uintptr_t pBase, float scale, bool isCaptured);
  void RenderResearchTab(uintptr_t pBase, float scale);
  void RenderExpTab(uintptr_t pBase, float scale);
  void RenderSpecialAbilityTab(uintptr_t pBase, float scale);
  
  void DrawOfficerDetailWindow(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale);
  void DrawBatchOfficerEditWindow(float scale);
  void DrawFactionTechEditor(float scale);
}

#ifdef DX11BASE_RESTORE_FAST_SET_TRAIT_ID
#undef DX11BASE_RESTORE_FAST_SET_TRAIT_ID
#define SetTraitID SetTraitIDForOfficerUiFast
#endif
