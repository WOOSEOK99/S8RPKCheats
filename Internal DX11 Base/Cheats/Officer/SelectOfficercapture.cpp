#define DrawBatchRandomTraitAssignmentWindow DrawBatchRandomTraitAssignmentWindowLegacyT05
#include "SelectOfficercapture_t05_base.inc"
#undef DrawBatchRandomTraitAssignmentWindow

namespace DX11Base {

static bool s_batchRandomIncludeCustomTraits = true;

void DrawBatchRandomTraitAssignmentWindow(float scale) {
  if (!s_showBatchRandomTraitWindow && !s_batchRandomJob.running)
    return;

  // 기존 처리 속도/검색/적용 로직은 그대로 사용합니다.
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

        if (!SeedTraitObjectsForBatch(
                s_batchRandomJob.requestedPool,
                s_batchRandomJob.traitObjects,
                s_batchRandomJob.traitVtable)) {
          s_batchRandomJob.running = false;
          s_batchRandomJob.collectingOfficers = false;
          s_batchRandomStatus =
              u8"기재 객체 형식(vtable)을 확인할 기준 기재를 찾지 못했습니다.";
        } else {
          s_batchRandomJob.scanningTraits =
              s_batchRandomJob.traitObjects.size() <
              s_batchRandomJob.requestedPool.size();

          if (!s_batchRandomJob.scanningTraits)
            s_batchRandomJob.pool = s_batchRandomJob.requestedPool;
          else
            s_batchRandomJob.scanAddress = 0;

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
