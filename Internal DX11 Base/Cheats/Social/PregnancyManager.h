#pragma once
#include <cstdint>
#include <vector>

namespace DX11Base {
  bool GetPregnancySpousePointers(
      uintptr_t rosterBase, uintptr_t hero, std::vector<uintptr_t>& spouses);
  void RefreshPregnancyChildIds(uintptr_t hero);
  void RunPregnancyManagerUpdate();
  void RefreshPregnancyManagerOnWindowOpen(bool requestChildren = true);
  void RequestFamilyChildScan();
  void GetFamilyChildIds(uintptr_t hero, uintptr_t spouse, std::vector<uint16_t>& ids);
  void DrawPregnancyManagerSection(float scale);
}
