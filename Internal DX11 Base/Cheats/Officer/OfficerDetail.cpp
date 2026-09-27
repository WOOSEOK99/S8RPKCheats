#define ScanTraitObjectsForBatchStep ScanTraitObjectsForBatchStepLegacy
#include "OfficerDetail_impl.inc"
#undef ScanTraitObjectsForBatchStep

namespace DX11Base {

bool ScanTraitObjectsForBatchStep(
    const std::vector<uint16_t>& traitIDs,
    std::unordered_map<uint16_t, uintptr_t>& outObjects,
    uintptr_t traitVtable,
    uintptr_t& scanAddress,
    size_t maxReadableBytes,
    bool& finished) {
  // 단건 기재 cache-miss 검색은 기존 bounded cadence를 그대로 유지합니다.
  // 여러 기재를 한꺼번에 준비하는 일괄 작업만 실제 시간 budget 안에서 가속합니다.
  if (traitIDs.size() <= 1 || maxReadableBytes < 2 * 1024 * 1024) {
    return ScanTraitObjectsForBatchStepLegacy(
        traitIDs, outObjects, traitVtable, scanAddress,
        maxReadableBytes, finished);
  }

  using Clock = std::chrono::steady_clock;
  constexpr size_t kChunkBytes = 2 * 1024 * 1024;
  constexpr size_t kHardByteCap = 64 * 1024 * 1024;
  const auto deadline = Clock::now() + std::chrono::milliseconds(4);

  bool anyFound = false;
  size_t attemptedBytes = 0;
  finished = false;

  do {
    bool stepFinished = false;
    if (ScanTraitObjectsForBatchStepLegacy(
            traitIDs, outObjects, traitVtable, scanAddress,
            kChunkBytes, stepFinished)) {
      anyFound = true;
    }

    attemptedBytes += kChunkBytes;
    if (stepFinished) {
      finished = true;
      return anyFound;
    }
  } while (attemptedBytes < kHardByteCap && Clock::now() < deadline);

  return anyFound;
}

} // namespace DX11Base
