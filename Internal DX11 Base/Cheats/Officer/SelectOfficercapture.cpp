#define DrawBatchRandomTraitAssignmentWindow DrawBatchRandomTraitAssignmentWindowLegacyT05
#include "SelectOfficercapture_t05_base.inc"
#undef DrawBatchRandomTraitAssignmentWindow

namespace DX11Base {

static void SeedBatchTraitObjectsFromRuntimeTableT05() {
  if (!s_batchRandomJob.running || !s_batchRandomJob.scanningTraits)
    return;

  constexpr uintptr_t kTraitPointerArrayOffset = 0x57A4D0;
  const uintptr_t gameBase = GetGameBaseFast();
  if (gameBase <= 0x10000)
    return;

  for (uint16_t expectedId : s_batchRandomJob.requestedPool) {
    uintptr_t traitObject = 0;
    uintptr_t traitVtable = 0;
    unsigned short actualId = 0;

    if (!UnsafeReadPtr(
            gameBase + kTraitPointerArrayOffset +
                static_cast<uintptr_t>(expectedId) * sizeof(uintptr_t),
            &traitObject) ||
        traitObject <= 0x10000 ||
        !UnsafeReadPtr(traitObject, &traitVtable) || traitVtable <= 0x10000 ||
        !UnsafeRead16(traitObject + 0x08, &actualId) || actualId != expectedId) {
      continue;
    }

    if (!s_batchRandomJob.traitVtable)
      s_batchRandomJob.traitVtable = traitVtable;
    s_batchRandomJob.traitObjects[expectedId] = traitObject;
  }

  if (s_batchRandomJob.traitObjects.size() >=
      s_batchRandomJob.requestedPool.size()) {
    s_batchRandomJob.scanningTraits = false;
    s_batchRandomJob.scanAddress = 0;
    s_batchRandomJob.pool = s_batchRandomJob.requestedPool;
  }
}

void DrawBatchRandomTraitAssignmentWindow(float scale) {
  const bool wasRunning = s_batchRandomJob.running;
  DrawBatchRandomTraitAssignmentWindowLegacyT05(scale);

  // 실행 버튼을 누른 프레임 직후, 기존 전체 프로세스 검색이 시작되기 전에
  // 검증된 런타임 기재 포인터 배열에서 필요한 객체를 직접 확보합니다.
  if (!wasRunning && s_batchRandomJob.running)
    SeedBatchTraitObjectsFromRuntimeTableT05();
}

} // namespace DX11Base
