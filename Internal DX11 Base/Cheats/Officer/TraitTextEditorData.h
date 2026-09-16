#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace DX11Base {

struct TraitTextEditRow {
  std::string oldName;
  std::string oldDesc;
  std::string newName;
  std::string newDesc;
};

// CT 87200의 72개 기재 원문/편집 상태.
// Step 1에서는 데이터와 trait_texts.ini 저장/불러오기만 담당하며 게임 메모리는 건드리지 않습니다.
std::vector<TraitTextEditRow>& GetTraitTextEditRows();

std::string GetTraitTextStoragePath();
bool LoadTraitTextEdits(std::string* error = nullptr);
bool SaveTraitTextEdits(std::string* error = nullptr);
void ResetTraitTextEdits();
bool HasTraitTextEdits();

// CT와 동일한 입력 제약을 검사합니다.
// - UTF-8 유효성 / NUL 금지
// - UTF-16 기준 512 code unit 이하
// - 새 기재명은 최대 5 code unit
// - 새 설명의 printf 토큰은 전부 제거하거나 원문과 종류/순서가 같아야 함
bool ValidateTraitTextRow(std::size_t index, std::string* error = nullptr);

} // namespace DX11Base
