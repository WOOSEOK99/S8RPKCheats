#define TickBatchRandomTraitJobT05 TickBatchRandomTraitJobT05Legacy
#include "SelectOfficercapture_t05_impl.inc"
#undef TickBatchRandomTraitJobT05

namespace DX11Base {

void TickBatchRandomTraitJobT05() {
  if (!s_batchRandomJob.running)
    return;

  // 이 기능은 게임 시작 직후 사실상 1회만 사용하는 초기화성 작업입니다.
  // 따라서 프레임/FPS 보호용 cadence를 적용하지 않고, 수집 -> 기재 검색 -> 적용을
  // 한 번의 호출에서 가능한 한 끝까지 연속 처리합니다.
  // 단건 기재 cache-miss 작업이 진행 중이면 그 작업을 우선하고 다음 maintenance에서 재개합니다.
  constexpr size_t kSafetyIterations = 200000;
  size_t iterations = 0;

  while (s_batchRandomJob.running && iterations++ < kSafetyIterations) {
    if (s_batchRandomJob.scanningTraits && g_pendingTraitChange.active)
      break;

    // legacy 본체 내부의 50ms/200ms gate를 이 1회용 경로에서만 해제합니다.
    g_t05BatchLastTickMs = 0;
    g_t05BatchLastScanMs = 0;

    const bool wasCollecting = s_batchRandomJob.collectingOfficers;
    const bool wasScanning = s_batchRandomJob.scanningTraits;
    const size_t beforeCollect = g_t05BatchCollectCursor;
    const size_t beforeCursor = s_batchRandomJob.cursor;
    const uintptr_t beforeScanAddress = s_batchRandomJob.scanAddress;

    TickBatchRandomTraitJobT05Legacy();

    if (!s_batchRandomJob.running)
      break;

    // 상태/커서가 전혀 전진하지 않았다면 예상치 못한 무한 반복을 피하고
    // 다음 maintenance 호출에서 다시 시도합니다.
    const bool progressed =
        wasCollecting != s_batchRandomJob.collectingOfficers ||
        wasScanning != s_batchRandomJob.scanningTraits ||
        beforeCollect != g_t05BatchCollectCursor ||
        beforeCursor != s_batchRandomJob.cursor ||
        beforeScanAddress != s_batchRandomJob.scanAddress;

    if (!progressed)
      break;
  }

  if (iterations >= kSafetyIterations && s_batchRandomJob.running) {
    AddLog(u8"[랜덤기재/T05] 초고속 일괄 처리 안전 상한 도달 - 다음 maintenance에서 계속 진행");
  }
}

} // namespace DX11Base
