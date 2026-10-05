#pragma once

#include <string>
#include <vector>

namespace DX11Base {

struct EmbeddedTraitTextInfo {
  int traitId = 0;
  std::string name;
  std::string desc;
};

struct EmbeddedTraitTextOverride {
  int traitId = 0;
  std::string name;
  std::string desc;
};

// 내장 S8RPK_traits_default.json의 customNames 항목을 실제 기재 ID 기준으로 반환합니다.
// JSON 키는 0-based index이므로 traitId는 index + 1입니다.
bool GetEmbeddedTraitTextCatalog(
    std::vector<EmbeddedTraitTextInfo> &out,
    std::string *error = nullptr);

// version.dll이 없는 내장 런타임에서만 ID 기반 이름/설명 오버레이를 적용합니다.
// 빈 name/desc는 해당 필드를 변경하지 않는다는 기존 편집기 규칙을 유지합니다.
bool ApplyEmbeddedTraitTextOverrides(
    const std::vector<EmbeddedTraitTextOverride> &overrides,
    std::string *error = nullptr);
bool ClearEmbeddedTraitTextOverrides(std::string *error = nullptr);

// san8r_traits_config.json의 기재 정의를 게임의 254개 기재 테이블에 적용합니다.
// Menu::Loops()에서 호출하며 내부에서 자체적으로 주기를 제한합니다.
void TickTraitConfigRuntime();

} // namespace DX11Base
