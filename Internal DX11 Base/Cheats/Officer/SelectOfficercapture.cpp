#define TickBatchRandomTraitJobT05 TickBatchRandomTraitJobT05Legacy
#include "SelectOfficercapture_t05_impl.inc"
#undef TickBatchRandomTraitJobT05

namespace DX11Base {

void TickBatchRandomTraitJobT05() {
  if (!s_batchRandomJob.running)
    return;

  using Clock = std::chrono::steady_clock;

  // 기재 객체 검색은 OfficerDetail 쪽 adaptive scanner가 호출당 최대 4ms를 사용합니다.
  // 기존 200ms 간격 제한만 제거해 50ms maintenance마다 진행되도록 합니다.
  if (s_batchRandomJob.scanningTraits) {
    g_t05BatchLastTickMs = 0;
    g_t05BatchLastScanMs = 0;
    TickBatchRandomTraitJobT05Legacy();
    return;
  }

  // 무장 수집/실제 부여 단계는 한 maintenance에서 여러 기존 chunk를 연속 처리하되
  // 총 실제 점유시간을 약 4ms로 제한합니다. 따라서 FPS에 의존하지 않으면서
  // 기존 체감 속도에 가깝게 처리량을 회복합니다.
  const auto deadline = Clock::now() + std::chrono::milliseconds(4);
  do {
    g_t05BatchLastTickMs = 0;
    TickBatchRandomTraitJobT05Legacy();

    if (!s_batchRandomJob.running || s_batchRandomJob.scanningTraits)
      break;
  } while (Clock::now() < deadline);
}

} // namespace DX11Base
