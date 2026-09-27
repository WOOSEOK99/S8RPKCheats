#define TickTraitConfigRuntime TickTraitConfigRuntimeBase
#include "TraitConfigRuntime_impl.inc"
#undef TickTraitConfigRuntime

#include "../../PerformanceDiagnostics.h"

namespace DX11Base {
namespace {

using FindMatchingCustomTraitFn = bool (*)(void *, uint16_t, uint16_t &);
using TraitEffectQueryDiagFn = int(__fastcall *)(void *, uint16_t);
using OfficerTraitQueryDiagFn = int(__fastcall *)(void *, uint32_t);

FindMatchingCustomTraitFn g_perfOriginalFindMatchingCustomTrait = nullptr;
TraitEffectQueryDiagFn g_perfOriginalTraitEffectQuery = nullptr;
OfficerTraitQueryDiagFn g_perfOriginalOfficerTraitQuery = nullptr;

bool g_perfFindMatchingHookInstalled = false;
bool g_perfTraitEffectHookInstalled = false;
bool g_perfOfficerTraitHookInstalled = false;
bool g_perfTraitHotPathInstallLogged = false;

thread_local uint32_t g_perfFindMatchingSampleCounter = 0;

bool PerfDiagnosticFindMatchingCustomTrait(
    void *officer, uint16_t requestedTraitId, uint16_t &matchedId) {
  PerfRecordFast(PerfMetric::FindMatchingCustomTrait);

  // 시간 API 자체가 hot path를 느리게 만들지 않도록 256회 중 1회만 샘플링합니다.
  const bool sample = ((++g_perfFindMatchingSampleCounter & 0xFFu) == 0);
  const uint64_t start = sample ? PerfRealNow100ns() : 0;

  const bool matched = g_perfOriginalFindMatchingCustomTrait
      ? g_perfOriginalFindMatchingCustomTrait(officer, requestedTraitId, matchedId)
      : false;

  if (matched) {
    // changes = 실제 custom 기재 match 성공 횟수
    PerfRecordFast(PerfMetric::FindMatchingCustomTrait, 0, 0, 1, 0);
  }

  if (sample) {
    const uint64_t end = PerfRealNow100ns();
    const uint64_t elapsed = end >= start ? end - start : 0;
    PerfRecordFast(PerfMetric::FindMatchingCustomTraitSample, elapsed);
  }

  return matched;
}

int __fastcall PerfDiagnosticTraitEffectQuery(
    void *officer, uint16_t requestedTraitId) {
  PerfRecordFast(PerfMetric::TraitEffectQuery);
  return g_perfOriginalTraitEffectQuery
      ? g_perfOriginalTraitEffectQuery(officer, requestedTraitId)
      : 0;
}

int __fastcall PerfDiagnosticOfficerTraitQuery(
    void *officer, uint32_t requestedId) {
  PerfRecordFast(PerfMetric::OfficerTraitQuery);
  return g_perfOriginalOfficerTraitQuery
      ? g_perfOriginalOfficerTraitQuery(officer, requestedId)
      : 0;
}

bool InstallOneTraitPerfHook(
    LPVOID target, LPVOID detour, LPVOID *original,
    bool &installed, const char *name) {
  if (installed)
    return true;

  LPVOID trampoline = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(target, detour, &trampoline);
  if (createStatus != MH_OK) {
    AddLog("[Perf:T05] %s diagnostic hook create failed: %s",
           name, MH_StatusToString(createStatus));
    return false;
  }

  const MH_STATUS enableStatus = MH_EnableHook(target);
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(target);
    AddLog("[Perf:T05] %s diagnostic hook enable failed: %s",
           name, MH_StatusToString(enableStatus));
    return false;
  }

  *original = trampoline;
  installed = true;
  return true;
}

void EnsureTraitHotPathPerformanceDiagnostics() {
  if (!PerfDiagnosticsEnabled())
    return;

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      AddLog("[Perf:T05] trait diagnostic MinHook init failed: %s",
             MH_StatusToString(initStatus));
    }
    return;
  }

  LPVOID findOriginal = reinterpret_cast<LPVOID>(g_perfOriginalFindMatchingCustomTrait);
  if (InstallOneTraitPerfHook(
          reinterpret_cast<LPVOID>(&FindMatchingCustomTrait),
          reinterpret_cast<LPVOID>(&PerfDiagnosticFindMatchingCustomTrait),
          &findOriginal, g_perfFindMatchingHookInstalled,
          "FindMatchingCustomTrait")) {
    g_perfOriginalFindMatchingCustomTrait =
        reinterpret_cast<FindMatchingCustomTraitFn>(findOriginal);
  }

  LPVOID effectOriginal = reinterpret_cast<LPVOID>(g_perfOriginalTraitEffectQuery);
  if (InstallOneTraitPerfHook(
          reinterpret_cast<LPVOID>(&CustomTraitEffectQuery),
          reinterpret_cast<LPVOID>(&PerfDiagnosticTraitEffectQuery),
          &effectOriginal, g_perfTraitEffectHookInstalled,
          "TraitEffectQuery")) {
    g_perfOriginalTraitEffectQuery =
        reinterpret_cast<TraitEffectQueryDiagFn>(effectOriginal);
  }

  LPVOID officerOriginal = reinterpret_cast<LPVOID>(g_perfOriginalOfficerTraitQuery);
  if (InstallOneTraitPerfHook(
          reinterpret_cast<LPVOID>(&CustomOfficerTraitQuery),
          reinterpret_cast<LPVOID>(&PerfDiagnosticOfficerTraitQuery),
          &officerOriginal, g_perfOfficerTraitHookInstalled,
          "OfficerTraitQuery")) {
    g_perfOriginalOfficerTraitQuery =
        reinterpret_cast<OfficerTraitQueryDiagFn>(officerOriginal);
  }

  if (g_perfFindMatchingHookInstalled &&
      g_perfTraitEffectHookInstalled &&
      g_perfOfficerTraitHookInstalled &&
      !g_perfTraitHotPathInstallLogged) {
    g_perfTraitHotPathInstallLogged = true;
    AddLog("[Perf:T05] trait hot-path diagnostics installed (FindMatching timing sample=1/256)");
  }
}

} // namespace

void TickTraitConfigRuntime() {
  TickTraitConfigRuntimeBase();
  EnsureTraitHotPathPerformanceDiagnostics();
}

} // namespace DX11Base
