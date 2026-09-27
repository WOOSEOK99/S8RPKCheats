#include "TraitConfigRuntime.h"

// Keep the implementation body intact, but expose it under an internal entry
// point so this translation unit can enforce the runtime source policy first.
#define TickTraitConfigRuntime TickTraitConfigRuntimeImpl
#include "TraitConfigRuntime_impl.inc"
#undef TickTraitConfigRuntime

namespace DX11Base {
namespace {

bool EnsureEmbeddedTraitConfigLoaded() {
  RuntimeState &state = State();
  if (state.configLoaded && state.configFromEmbedded)
    return true;

  std::string jsonText;
  if (!LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText)) {
    AddLog(u8"[기재JSON] 내장 기본 설정을 읽지 못했습니다.");
    return false;
  }

  std::array<TraitConfigEntry, kTraitCount> traits{};
  std::array<TraitMetaEntry, kTraitCount> meta{};
  int traitCount = 0;
  int customCount = 0;
  int sentinel = -1;

  std::istringstream file(jsonText);
  bool inCustomNames = false;
  std::string line;

  while (std::getline(file, line)) {
    if (!inCustomNames) {
      if (line.find("\"customNames\"") != std::string::npos) {
        inCustomNames = true;
        continue;
      }

      int index = -1;
      TraitConfigEntry entry;
      if (ParseTraitLine(line, index, entry)) {
        traits[index] = entry;
        ++traitCount;
      }
      continue;
    }

    int index = -1;
    TraitMetaEntry entry;
    if (ParseCustomMetaLine(line, index, entry)) {
      meta[index] = entry;
      ++customCount;
    }
  }

  if (traitCount == 0) {
    AddLog(u8"[기재JSON] 내장 기본 설정 형식을 읽지 못했습니다.");
    return false;
  }

  for (int i = 0; i < kTraitCount; ++i) {
    if (!traits[i].present)
      continue;
    if (traits[i].id != i + 1) {
      AddLog(u8"[기재JSON] 내장 기본 설정의 기재 ID 배열이 올바르지 않습니다: index=%d id=%d",
             i, traits[i].id);
      return false;
    }
  }

  for (int i = 0; i < kTraitCount; ++i) {
    if (traits[i].present && meta[i].present && meta[i].hasCustomText) {
      sentinel = i;
      break;
    }
  }
  if (sentinel < 0) {
    for (int i = 0; i < kTraitCount; ++i) {
      if (traits[i].present) {
        sentinel = i;
        break;
      }
    }
  }

  if (!PublishNativeTexts(meta)) {
    AddLog(u8"[기재JSON/문구] 이름/설명 준비 실패. 내장 기본기재 적용을 중지합니다.");
    return false;
  }

  state.traits = std::move(traits);
  state.meta = std::move(meta);
  state.configPath.clear();
  state.configLoaded = true;
  state.configFromEmbedded = true;
  state.hasWriteTime = false;
  state.sentinelIndex = sentinel;
  state.applyPending = true;
  state.descTablesSynced = false;
  state.descTableFailureLogged = false;

  AddLog(u8"[기재JSON] version.dll 없음 -> 내장 기본기재 사용 / traits %d개 / customNames %d개 / 감시 index %d",
         traitCount, customCount, sentinel);
  return true;
}

} // namespace

void TickTraitConfigRuntime() {
  static bool externalPairLogged = false;

  // version.dll과 san8r_traits_config.json은 외부 기재 런타임 한 세트입니다.
  // version.dll이 있으면 S8RCheats는 기재 테이블에 개입하지 않고 외부 DLL에 맡깁니다.
  if (HasExternalGameVersionDll()) {
    if (!externalPairLogged) {
      AddLog(u8"[기재JSON] 외부 version.dll 감지 -> version.dll + san8r_traits_config.json 세트에 기재 처리를 맡깁니다.");
      externalPairLogged = true;
    }
    return;
  }
  externalPairLogged = false;

  // version.dll이 없을 때는 같은 폴더에 san8r_traits_config.json이 남아 있어도
  // 단독 설정으로 취급하지 않습니다. 임베디드 기본기재만 사용합니다.
  if (!EnsureEmbeddedTraitConfigLoaded())
    return;

  // 기존 구현의 주기적 외부 JSON 탐색을 건너뛰되, 이름/설명/효과 훅과
  // 기재 테이블 재생성 감지/재적용 로직은 그대로 유지합니다.
  State().lastConfigProbeMs = GetTickCount64();
  TickTraitConfigRuntimeImpl();
}

} // namespace DX11Base
