#pragma once
#include "Framework/imgui.h"
#include "pch.h"
#include <string>

namespace DX11Base {
  extern bool bShowOfficerDetail;
  extern bool bShowSelectedOfficerWin;

  extern const char *traitNames[];

  void SetupTableHeaders(float scale);
  void RenderStatRow(uintptr_t pBase, const char *label, uintptr_t offset, int size, int *outValue, float scale);
  void RenderCompactSkill(uintptr_t pBase, const char *label, uintptr_t offset, int *outValue, float scale);
  void RenderResearchRow(uintptr_t pBase, const char *catLabel, const char *items[], uintptr_t offsets[], int *vars[], int count, float scale);
  
  uint16_t GetTraitID(uintptr_t base, int slot);
  void SetTraitID(uintptr_t base, int slot, uint16_t traitID);
  uintptr_t GetSelectedOfficerBase();

  void RenderBasicTab(uintptr_t pBase, float scale, bool isCaptured);
  void RenderResearchTab(uintptr_t pBase, float scale);
  void RenderExpTab(uintptr_t pBase, float scale);
  
  void DrawOfficerDetailWindow(uintptr_t p1, ImVec2 mPos, ImVec2 mSize, float scale);
}    
