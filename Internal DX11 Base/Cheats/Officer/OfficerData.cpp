#include "../../pch.h"
#include "../../Cheats.h"

namespace {

SIZE_T WINAPI T05OfficerDataVirtualQuery(
    LPCVOID address,
    PMEMORY_BASIC_INFORMATION buffer,
    SIZE_T length) {
  static uintptr_t s_negativeGameBase = 0;
  static ULONGLONG s_negativeUntilMs = 0;

  const uintptr_t gameBase = DX11Base::GetGameBase();
  const uintptr_t queryAddress = reinterpret_cast<uintptr_t>(address);
  const ULONGLONG now = GetTickCount64();

  // OfficerData.cpp에서 VirtualQuery는 TryResolveSynergeticTable의
  // gameBase~gameBase+8MiB fallback에만 사용됩니다. 직전 전체 탐색이 끝까지
  // 실패한 같은 세션이면 5초 동안 새 탐색의 첫 호출을 즉시 중단합니다.
  if (gameBase > 0x10000 &&
      queryAddress == gameBase &&
      s_negativeGameBase == gameBase &&
      now < s_negativeUntilMs) {
    return 0;
  }

  const SIZE_T result = ::VirtualQuery(address, buffer, length);
  if (result != sizeof(MEMORY_BASIC_INFORMATION) || gameBase <= 0x10000)
    return result;

  // fallback의 마지막 메모리 region까지 도달했다면 이번 8MiB 탐색은
  // 후보를 찾지 못하고 끝날 예정입니다. 다음 호출부터만 cooldown을 적용합니다.
  if (queryAddress >= gameBase && queryAddress < gameBase + 0x800000) {
    const uintptr_t regionBase = reinterpret_cast<uintptr_t>(buffer->BaseAddress);
    const uintptr_t regionEnd = regionBase + buffer->RegionSize;
    if (regionEnd >= gameBase + 0x800000) {
      s_negativeGameBase = gameBase;
      s_negativeUntilMs = now + 5000;
    }
  }

  // 세션이 바뀌면 이전 실패 기록은 즉시 무효화합니다.
  if (s_negativeGameBase != 0 && s_negativeGameBase != gameBase) {
    s_negativeGameBase = 0;
    s_negativeUntilMs = 0;
  }

  return result;
}

} // namespace

#define VirtualQuery T05OfficerDataVirtualQuery
#include "OfficerData_impl.inc"
#undef VirtualQuery
