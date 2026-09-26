#pragma once
#define DX11BASE_OFFICER_DETAIL_HEADER_INCLUDED 1

#include "../../Framework/imgui.h"
#include "../../pch.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace DX11Base {
  extern bool bShowOfficerDetail;
  extern bool bShowSelectedOfficerWin;

  extern const char *traitNames[];

  uintptr_t GetGameBaseFast();

  void SetupTableHeaders(float scale);
  void RenderStatRow(uintptr_t pBase, const char *label, uintptr_t offset, int size, int *outValue, float scale);
  void RenderCompactSkill(uintptr_t pBase, const char *label, uintptr_t offset, int *outValue, float scale);
  void RenderResearchRow(uintptr_t pBase, const char *catLabel, const char *items[], uintptr_t offsets[], int *vars[], int count, float scale, int expOffset = -1);
  
  uint16_t GetTraitID(uintptr_t base, int slot);
  bool SetTraitID(uintptr_t base, int slot, uint16_t traitID);

  inline bool SetTraitIDFast(uintptr_t officerBase, int slot, uint16_t traitID) {
    if (officerBase < 0x10000 || slot < 0 || slot >= 3 || traitID == 0)
      return false;

    constexpr uintptr_t kTraitPointerArrayOffset = 0x57A4D0;
    const uintptr_t gameDataRoot = GetGameBaseFast();
    if (gameDataRoot < 0x10000)
      return SetTraitID(officerBase, slot, traitID);

    uintptr_t traitObject = 0;
    uint16_t actualId = 0;
    __try {
      traitObject = *reinterpret_cast<const uintptr_t *>(
          gameDataRoot + kTraitPointerArrayOffset +
          static_cast<uintptr_t>(traitID) * sizeof(uintptr_t));
      if (traitObject >= 0x10000)
        actualId = *reinterpret_cast<const uint16_t *>(traitObject + 0x08);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      traitObject = 0;
      actualId = 0;
    }

    if (traitObject < 0x10000 || actualId != traitID)
      return SetTraitID(officerBase, slot, traitID);

    uintptr_t *slotPtr = reinterpret_cast<uintptr_t *>(
        officerBase + 0x88 + static_cast<uintptr_t>(slot) * sizeof(uintptr_t));
    DWORD oldProtect = 0;
    if (!VirtualProtect(slotPtr, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect))
      return false;

    *slotPtr = traitObject;
    DWORD ignored = 0;
    VirtualProtect(slotPtr, sizeof(uintptr_t), oldProtect, &ignored);

    uintptr_t verifyPtr = 0;
    uint16_t verifyId = 0;
    __try {
      verifyPtr = *slotPtr;
      if (verifyPtr >= 0x10000)
        verifyId = *reinterpret_cast<const uint16_t *>(verifyPtr + 0x08);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
    return verifyPtr == traitObject && verifyId == traitID;
  }

#ifdef DX11BASE_SELECT_OFFICER_CAPTURE_HEADER_INCLUDED
#define SetTraitID SetTraitIDFast
#endif

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
