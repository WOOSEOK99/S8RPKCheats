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

  // 기존 72개 CT 행은 false/0을 유지합니다. 내장 기본기재 행은 true이며
  // traitId는 실제 게임 기재 ID(1..254)입니다.
  bool embedded = false;
  int traitId = 0;
};

// 기존 72개 기재와 내장 기본기재의 편집 상태를 반환합니다.
// 기존 72개 행의 내용/순서는 하위 호환을 위해 그대로 유지합니다.
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
