#include "TraitConfigRuntime.h"

#define TickBatchRandomTraitJob TickBatchRandomTraitJobLegacyT05
#define DrawBatchRandomTraitAssignmentWindow DrawBatchRandomTraitAssignmentWindowLegacyT05
#include "SelectOfficercapture_t05_base.inc"
#undef DrawBatchRandomTraitAssignmentWindow
#undef TickBatchRandomTraitJob

namespace DX11Base {

static bool s_batchRandomIncludeCustomTraits = true;

static bool s_batchRandomDiagActive = false;
static bool s_batchRandomDiagApplyStarted = false;
static int s_batchRandomDiagNextChangedMark = 250;
static ULONGLONG s_batchRandomDiagStartMs = 0;
static ULONGLONG s_batchRandomDiagApplyStartMs = 0;

static unsigned long long BatchDiagElapsedMs(ULONGLONG since) {
  const ULONGLONG now = GetTickCount64();
  return static_cast<unsigned long long>(now >= since ? now - since : 0);
}

static void ResetBatchRandomDiag() {
  s_batchRandomDiagActive = true;
  s_batchRandomDiagApplyStarted = false;
  s_batchRandomDiagNextChangedMark = 250;
  s_batchRandomDiagStartMs = GetTickCount64();
  s_batchRandomDiagApplyStartMs = 0;
}

static size_t SeedBatchTraitObjectsFromRuntimeTableT05(
    const std::vector<uint16_t>& traitIds,
    std::unordered_map<uint16_t, uintptr_t>& outObjects,
    uintptr_t& inoutTraitVtable) {
  constexpr uintptr_t kTraitPointerArrayOffset = 0x57A4D0;

  const uintptr_t gameBase = GetGameBaseFast();
  if (gameBase <= 0x10000)
    return outObjects.size();

  for (uint16_t expectedId : traitIds) {
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

    if (!inoutTraitVtable)
      inoutTraitVtable = traitVtable;

    outObjects[expectedId] = traitObject;
  }

  return outObjects.size();
}

static void TickBatchRandomTraitJob() {
  if (!s_batchRandomJob.running)
    return;

  const bool wasCollecting = s_batchRandomJob.collectingOfficers;
  const bool wasScanning = s_batchRandomJob.scanningTraits;
  const bool wasRunning = s_batchRandomJob.running;

  TickBatchRandomTraitJobLegacyT05();

  if (!s_batchRandomDiagActive)
    return;

  if (wasCollecting && !s_batchRandomJob.collectingOfficers) {
    AddLog(u8"[랜덤기재/일괄/T05DIAG] 단계1 목록 수집 완료: %zu명 / 시작후 %llums",
           s_batchRandomJob.officers.size(),
           BatchDiagElapsedMs(s_batchRandomDiagStartMs));
  }

  if (wasScanning && !s_batchRandomJob.scanningTraits) {
    AddLog(u8"[랜덤기재/일괄/T05DIAG] 단계2 기재 객체 검색 완료: 사용가능 %zu개 / 요청 %zu개 / 시작후 %llums",
           s_batchRandomJob.pool.size(),
           s_batchRandomJob.requestedPool.size(),
           BatchDiagElapsedMs(s_batchRandomDiagStartMs));
  }

  if (!s_batchRandomJob.collectingOfficers && !s_batchRandomJob.scanningTraits &&
      s_batchRandomJob.running && !s_batchRandomDiagApplyStarted) {
    s_batchRandomDiagApplyStarted = true;
    s_batchRandomDiagApplyStartMs = GetTickCount64();
    AddLog(u8"[랜덤기재/일괄/T05DIAG] 단계3 실제 슬롯 적용 시작: 대상 %zu명 / pool %zu개",
           s_batchRandomJob.officers.size(),
           s_batchRandomJob.pool.size());
  }

  while (s_batchRandomDiagApplyStarted &&
         s_batchRandomJob.changedOfficers >= s_batchRandomDiagNextChangedMark) {
    AddLog(u8"[랜덤기재/일괄/T05DIAG] 적용 진행: 변경 %d명 / cursor %zu/%zu / 부여 슬롯 %d개 / 적용시작후 %llums / 전체 %llums",
           s_batchRandomJob.changedOfficers,
           s_batchRandomJob.cursor,
           s_batchRandomJob.officers.size(),
           s_batchRandomJob.filledSlots,
           BatchDiagElapsedMs(s_batchRandomDiagApplyStartMs),
           BatchDiagElapsedMs(s_batchRandomDiagStartMs));
    s_batchRandomDiagNextChangedMark += 250;
  }

  if (wasRunning && !s_batchRandomJob.running) {
    AddLog(u8"[랜덤기재/일괄/T05DIAG] 단계4 작업 종료: 변경 %d명 / 부여 슬롯 %d개 / 전체 %llums",
           s_batchRandomJob.changedOfficers,
           s_batchRandomJob.filledSlots,
           BatchDiagElapsedMs(s_batchRandomDiagStartMs));
    s_batchRandomDiagActive = false;
  }
}

void DrawBatchRandomTraitAssignmentWindow(float scale) {
  if (!s_showBatchRandomTraitWindow && !s_batchRandomJob.running)
    return;

  TickBatchRandomTraitJob();

  if (!s_showBatchRandomTraitWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(470.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"모든 무장 일괄 랜덤기재 부여###BatchRandomTraits",
                    &s_showBatchRandomTraitWindow,
                    ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  const std::vector<uint16_t> rawPool = BuildBatchRandomTraitPool();
  std::vector<uint16_t> allPool;
  allPool.reserve(rawPool.size());
  for (uint16_t id : rawPool) {
    const bool builtin = (id >= 1 && id <= 70) || id == 201 || id == 202;
    if (s_batchRandomIncludeCustomTraits || builtin)
      allPool.push_back(id);
  }

  const bool hasGreen = BatchPoolHasGrade(allPool, 2);
  const bool hasRed = BatchPoolHasGrade(allPool, 3);

  int goldCount = 0, greenCount = 0, redCount = 0;
  for (uint16_t id : allPool) {
    const int grade = GetBatchTraitGrade(id);
    if (grade == 1) ++goldCount;
    else if (grade == 2) ++greenCount;
    else if (grade == 3) ++redCount;
  }

  ImGui::TextWrapped(u8"모든 유효 무장의 기존 기재는 유지하고, 비어 있는 기재 슬롯만 중복 없이 랜덤으로 채웁니다.");
  ImGui::Spacing();

  if (s_batchRandomJob.running)
    ImGui::BeginDisabled();

  if (ImGui::Checkbox(u8"커스텀 기재 포함##BatchRandomIncludeCustom", &s_batchRandomIncludeCustomTraits)) {
    AddLog(u8"[랜덤기재/일괄] 커스텀 기재 포함: %s",
           s_batchRandomIncludeCustomTraits ? "ON" : "OFF (기본 72개만)");
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(u8"OFF: 기본 게임 기재 ID 1~70, 201, 202만 사용\nON: JSON에 정의된 커스텀 기재도 후보에 포함");
  }

  bool bypassCompatibility = IsTraitCompatibilityBypass();
  if (ImGui::Checkbox(u8"커스텀 기재 호환 처리 우회 (진단)##TraitCompatBypass", &bypassCompatibility)) {
    SetTraitCompatibilityBypass(bypassCompatibility);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(u8"ON: 이미 설치된 호환 훅은 유지하지만 커스텀 확장 처리는 건너뛰고 게임 원본 판정만 사용합니다.\n기재 데이터 자체는 변경하지 않습니다.");
  }

  ImGui::Text(u8"황금 후보: %d개", goldCount);
  ImGui::SameLine();
  ImGui::Text(u8"녹색: %d개", greenCount);
  ImGui::SameLine();
  ImGui::Text(u8"적색: %d개", redCount);
  ImGui::Separator();

  ImGui::Checkbox(u8"황금##BatchRandomGold", &s_batchRandomGold);
  ImGui::SameLine();

  if (!hasGreen)
    ImGui::BeginDisabled();
  ImGui::Checkbox(u8"녹색##BatchRandomGreen", &s_batchRandomGreen);
  if (!hasGreen) {
    ImGui::EndDisabled();
    s_batchRandomGreen = false;
  }

  ImGui::SameLine();
  if (!hasRed)
    ImGui::BeginDisabled();
  ImGui::Checkbox(u8"적색##BatchRandomRed", &s_batchRandomRed);
  if (!hasRed) {
    ImGui::EndDisabled();
    s_batchRandomRed = false;
  }

  if (s_batchRandomJob.running)
    ImGui::EndDisabled();

  if (!s_batchRandomIncludeCustomTraits) {
    ImGui::TextColored(ImVec4(0.45f, 0.9f, 1.0f, 1.0f),
                       u8"진단 모드: 기본 게임 기재 72개만 랜덤 부여합니다.");
  } else if (!HasCustomTraitConfigFile()) {
    ImGui::TextDisabled(u8"※ san8r_traits_config.json 없음: 기본 황금 기재 72개만 사용");
  }

  if (IsTraitCompatibilityBypass()) {
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f, 1.0f),
                       u8"진단 모드: 커스텀 기재 호환 확장 우회 중 (게임 원본 판정만 사용)");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                     u8"※ 기존 기재는 유지하고 빈 슬롯만 변경합니다.");

  if (s_batchRandomJob.running) {
    if (s_batchRandomJob.collectingOfficers) {
      ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                         u8"1~5102 전체 무장 공간 목록 수집 중...");
    } else if (s_batchRandomJob.scanningTraits) {
      ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                         u8"기재 객체 검색 중... %zu / %zu개 발견",
                         s_batchRandomJob.traitObjects.size(),
                         s_batchRandomJob.requestedPool.size());
    } else {
      const float progress = s_batchRandomJob.officers.empty()
          ? 0.0f
          : static_cast<float>(s_batchRandomJob.cursor) /
            static_cast<float>(s_batchRandomJob.officers.size());

      char progressText[128];
      snprintf(progressText, sizeof(progressText),
               u8"%zu / %zu명", s_batchRandomJob.cursor,
               s_batchRandomJob.officers.size());
      ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressText);
    }
  } else {
    const bool noGradeSelected =
        !s_batchRandomGold && !s_batchRandomGreen && !s_batchRandomRed;

    if (noGradeSelected)
      ImGui::BeginDisabled();

    if (ImGui::Button(u8"모든 무장 일괄 랜덤기재 부여 실행",
                      ImVec2(-1.0f, 34.0f * scale))) {
      std::vector<uint16_t> enabledPool;
      enabledPool.reserve(allPool.size());
      for (uint16_t id : allPool) {
        const int grade = GetBatchTraitGrade(id);
        if ((grade == 1 && s_batchRandomGold) ||
            (grade == 2 && s_batchRandomGreen) ||
            (grade == 3 && s_batchRandomRed)) {
          enabledPool.push_back(id);
        }
      }

      if (enabledPool.empty()) {
        s_batchRandomStatus = u8"선택한 등급에 사용 가능한 기재가 없습니다.";
      } else {
        s_batchRandomJob = {};
        s_batchRandomJob.running = true;
        s_batchRandomJob.collectingOfficers = true;
        s_batchRandomJob.requestedPool = std::move(enabledPool);
        ResetBatchRandomDiag();
        AddLog(u8"[랜덤기재/일괄/T05DIAG] 단계0 작업 시작: 요청 기재 %zu개 / 커스텀 포함=%s",
               s_batchRandomJob.requestedPool.size(),
               s_batchRandomIncludeCustomTraits ? "ON" : "OFF");

        if (!SeedTraitObjectsForBatch(
                s_batchRandomJob.requestedPool,
                s_batchRandomJob.traitObjects,
                s_batchRandomJob.traitVtable)) {
          s_batchRandomJob.running = false;
          s_batchRandomJob.collectingOfficers = false;
          s_batchRandomDiagActive = false;
          s_batchRandomStatus =
              u8"기재 객체 형식(vtable)을 확인할 기준 기재를 찾지 못했습니다.";
        } else {
          const size_t directResolved = SeedBatchTraitObjectsFromRuntimeTableT05(
              s_batchRandomJob.requestedPool,
              s_batchRandomJob.traitObjects,
              s_batchRandomJob.traitVtable);
          AddLog(u8"[랜덤기재/일괄/T05FAST] 런타임 포인터 배열 직접 확인: %zu/%zu개",
                 directResolved,
                 s_batchRandomJob.requestedPool.size());

          s_batchRandomJob.scanningTraits =
              s_batchRandomJob.traitObjects.size() <
              s_batchRandomJob.requestedPool.size();

          if (!s_batchRandomJob.scanningTraits) {
            s_batchRandomJob.pool = s_batchRandomJob.requestedPool;
            AddLog(u8"[랜덤기재/일괄/T05FAST] 전체 프로세스 기재 객체 스캔 생략");
          } else {
            s_batchRandomJob.scanAddress = 0;
            AddLog(u8"[랜덤기재/일괄/T05FAST] 미확인 %zu개 - 기존 bounded 전체 스캔 fallback",
                   s_batchRandomJob.requestedPool.size() - s_batchRandomJob.traitObjects.size());
          }

          StartCollectValidOfficerBasesForBatchWorker();
          s_batchRandomStatus = s_batchRandomIncludeCustomTraits
              ? u8"1~5102 전체 무장 공간 목록 수집 시작 (커스텀 포함)"
              : u8"1~5102 전체 무장 공간 목록 수집 시작 (기본 72개만)";
        }
      }
    }

    if (noGradeSelected)
      ImGui::EndDisabled();
  }

  if (!s_batchRandomStatus.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", s_batchRandomStatus.c_str());
  }

  ImGui::End();
}

} // namespace DX11Base
