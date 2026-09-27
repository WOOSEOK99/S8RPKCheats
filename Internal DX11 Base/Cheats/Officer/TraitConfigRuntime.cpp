#define TickTraitConfigRuntime TickTraitConfigRuntimeBase
#define CustomTraitEffectQuery CustomTraitEffectQueryBase
#define CustomOfficerTraitQuery CustomOfficerTraitQueryBase
#define CustomMonthlyTraitUpdate CustomMonthlyTraitUpdateBase
#define CustomTransferTraitEvent CustomTransferTraitEventBase
#define EnsureTraitEffectHook EnsureTraitEffectHookBase
#define EnsureExtendedTraitCompatibility EnsureExtendedTraitCompatibilityBase
#include "TraitConfigRuntime_impl.inc"
#undef EnsureExtendedTraitCompatibility
#undef EnsureTraitEffectHook
#undef CustomTransferTraitEvent
#undef CustomMonthlyTraitUpdate
#undef CustomOfficerTraitQuery
#undef CustomTraitEffectQuery
#undef TickTraitConfigRuntime

#include <atomic>

namespace DX11Base {
namespace {

std::atomic<bool> g_traitCompatibilityBypass{false};

int __fastcall CustomTraitEffectQueryT05(
    void *officer, uint16_t requestedTraitId) {
  RuntimeState &state = State();
  if (g_traitCompatibilityBypass.load(std::memory_order_relaxed)) {
    g_traitEffectMatchCache = {};
    const uintptr_t original = state.originalTraitEffectQuery;
    return original
        ? reinterpret_cast<TraitEffectQuery>(original)(officer, requestedTraitId)
        : 0;
  }
  return CustomTraitEffectQueryBase(officer, requestedTraitId);
}

int __fastcall CustomOfficerTraitQueryT05(
    void *officer, uint32_t requestedId) {
  RuntimeState &state = State();
  if (g_traitCompatibilityBypass.load(std::memory_order_relaxed)) {
    g_traitEffectMatchCache = {};
    const uintptr_t original = state.originalOfficerTraitQuery;
    return original
        ? reinterpret_cast<OfficerTraitQuery>(original)(officer, requestedId)
        : 0;
  }
  return CustomOfficerTraitQueryBase(officer, requestedId);
}

int __fastcall CustomMonthlyTraitUpdateT05(void *turn) {
  RuntimeState &state = State();
  if (g_traitCompatibilityBypass.load(std::memory_order_relaxed)) {
    const uintptr_t original = state.originalMonthlyUpdate;
    return original
        ? reinterpret_cast<MonthlyTraitUpdate>(original)(turn)
        : 0;
  }
  return CustomMonthlyTraitUpdateBase(turn);
}

int __fastcall CustomTransferTraitEventT05(
    void *firstPerson, void *secondPerson, void *personList,
    void *firstCity, void *secondCity) {
  RuntimeState &state = State();
  if (g_traitCompatibilityBypass.load(std::memory_order_relaxed)) {
    g_traitEffectMatchCache = {};
    const uintptr_t original = state.originalTransferEvent;
    return original
        ? reinterpret_cast<TransferTraitEvent>(original)(
              firstPerson, secondPerson, personList, firstCity, secondCity)
        : 0;
  }
  return CustomTransferTraitEventBase(
      firstPerson, secondPerson, personList, firstCity, secondCity);
}

bool EnsureTraitEffectHookT05() {
  RuntimeState &state = State();
  if (state.traitEffectHookInstalled)
    return true;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t target = gameBase + kTraitEffectQueryOffset;
  uint8_t prologue[sizeof(kTraitEffectQueryPrologue)] = {};
  if (!ReadMemorySafe(target, prologue, sizeof(prologue)) ||
      std::memcmp(prologue, kTraitEffectQueryPrologue,
                  sizeof(kTraitEffectQueryPrologue)) != 0) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 원본 바이트 불일치. 메인 효과 훅 설치 보류.");
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] MinHook 초기화 실패: %s",
             MH_StatusToString(initStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  LPVOID original = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(
      reinterpret_cast<LPVOID>(target),
      reinterpret_cast<LPVOID>(&CustomTraitEffectQueryT05),
      &original);
  if (createStatus != MH_OK) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 훅 생성 실패: %s",
             MH_StatusToString(createStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  if (!original ||
      !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 trampoline 검증 실패.");
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  state.originalTraitEffectQuery = reinterpret_cast<uintptr_t>(original);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    state.originalTraitEffectQuery = 0;
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 훅 활성화 실패: %s",
             MH_StatusToString(enableStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  state.traitEffectHookInstalled = true;
  state.traitEffectHookFailureLogged = false;
  AddLog(u8"[기재JSON/효과] TraitEffect 메인 훅 설치 완료: +17AAD60 target=%p trampoline=%p",
         reinterpret_cast<void *>(target),
         reinterpret_cast<void *>(state.originalTraitEffectQuery));
  return true;
}

void EnsureExtendedTraitCompatibilityT05() {
  RuntimeState &state = State();

  InstallCompatibilityHook(
      kOfficerTraitQueryOffset, kOfficerTraitQueryPrologue,
      sizeof(kOfficerTraitQueryPrologue),
      reinterpret_cast<void *>(&CustomOfficerTraitQueryT05),
      state.originalOfficerTraitQuery, state.officerTraitHookInstalled,
      state.officerTraitHookFailureLogged, u8"장수 보유 판정");

  InstallCompatibilityHook(
      kTraitMessageSetterOffset, kTraitMessageSetterPrologue,
      sizeof(kTraitMessageSetterPrologue),
      reinterpret_cast<void *>(&CustomTraitMessageSetter),
      state.originalTraitMessageSetter, state.traitMessageHookInstalled,
      state.traitMessageHookFailureLogged, u8"대화 효과 연결");

  InstallCompatibilityHook(
      kTraitDataGetterOffset, kTraitDataGetterPrologue,
      sizeof(kTraitDataGetterPrologue),
      reinterpret_cast<void *>(&CustomTraitDataGetter),
      state.originalTraitDataGetter, state.traitDataHookInstalled,
      state.traitDataHookFailureLogged, u8"효과 데이터 연결");

  InstallCompatibilityHook(
      kMonthlyTraitUpdateOffset, kMonthlyTraitUpdatePrologue,
      sizeof(kMonthlyTraitUpdatePrologue),
      reinterpret_cast<void *>(&CustomMonthlyTraitUpdateT05),
      state.originalMonthlyUpdate, state.monthlyHookInstalled,
      state.monthlyHookFailureLogged, u8"월별 효과 보완");

  InstallCompatibilityHook(
      kTransferTraitEventOffset, kTransferTraitEventPrologue,
      sizeof(kTransferTraitEventPrologue),
      reinterpret_cast<void *>(&CustomTransferTraitEventT05),
      state.originalTransferEvent, state.transferHookInstalled,
      state.transferHookFailureLogged, u8"특수 연출 연결");

  MaintainTraitEligibilityPatch(
      kTraitEligibilityPatchAOffset,
      state.eligibilityPatchAActive,
      state.eligibilityPatchAFailureLogged,
      u8"기재 사용 조건 A");
  MaintainTraitEligibilityPatch(
      kTraitEligibilityPatchBOffset,
      state.eligibilityPatchBActive,
      state.eligibilityPatchBFailureLogged,
      u8"기재 사용 조건 B");
}

} // namespace

void SetTraitCompatibilityBypass(bool bypass) {
  const bool previous =
      g_traitCompatibilityBypass.exchange(bypass, std::memory_order_relaxed);
  if (previous != bypass) {
    g_traitEffectMatchCache = {};
    AddLog(u8"[기재JSON/진단] 커스텀 기재 호환 처리: %s",
           bypass ? u8"우회(원본만 사용)" : u8"활성");
  }
}

bool IsTraitCompatibilityBypass() {
  return g_traitCompatibilityBypass.load(std::memory_order_relaxed);
}

void TickTraitConfigRuntime() {
  RuntimeState &state = State();
  const ULONGLONG now = GetTickCount64();

  if (now - state.lastTickMs < 500ull)
    return;
  state.lastTickMs = now;

  static bool externalVersionDllChecked = false;
  static bool externalVersionDllDetected = false;
  if (!externalVersionDllChecked) {
    externalVersionDllChecked = true;
    externalVersionDllDetected = HasExternalGameVersionDll();
    if (externalVersionDllDetected) {
      AddLog(u8"[기재JSON] 게임 폴더의 외부 version.dll 감지. S8RCheats 내장 기재 런타임을 사용하지 않습니다.");
    }
  }
  if (externalVersionDllDetected)
    return;

  if (!state.configLoaded || now - state.lastConfigProbeMs >= 2000ull) {
    state.lastConfigProbeMs = now;
    bool changed = false;
    if (!LoadConfig(changed))
      return;
    if (changed)
      state.applyPending = true;
  }

  if (!EnsureCustomNameHook())
    return;
  if (!EnsureCustomDescHook())
    return;

  SyncCustomDescTextTables();

  const uintptr_t gameDataRoot = GetGameBaseFast();
  const uintptr_t tableBase = ResolveTraitTable(gameDataRoot);
  if (!tableBase)
    return;

  if (state.lastTableBase != tableBase) {
    if (state.lastTableBase != 0) {
      AddLog(u8"[기재JSON] 기재 테이블 재생성 감지: %p -> %p",
             reinterpret_cast<void *>(state.lastTableBase),
             reinterpret_cast<void *>(tableBase));
    }
    state.lastTableBase = tableBase;
    state.applyPending = true;
  }

  if (!state.applyPending && state.sentinelIndex >= 0 &&
      !ConfigEntryMatches(tableBase, state.sentinelIndex)) {
    AddLog(u8"[기재JSON] 기재 효과 초기화 감지(index %d). 재적용합니다.",
           state.sentinelIndex);
    state.applyPending = true;
  }

  if (state.applyPending) {
    if (ApplyConfig(tableBase)) {
      state.applyPending = false;
      AddLog(u8"[기재JSON] 기재 설정 적용 완료: table=%p",
             reinterpret_cast<void *>(tableBase));
    } else {
      return;
    }
  }

  if (EnsureTraitEffectHookT05())
    EnsureExtendedTraitCompatibilityT05();
}

} // namespace DX11Base
