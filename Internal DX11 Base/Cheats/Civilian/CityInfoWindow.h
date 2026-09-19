#pragma once
#include "../../Framework/imgui.h"
#include <cstdint>

namespace DX11Base {
  // 자동 환전 설정 변수 (Config 저장/로드 대상)
  extern int g_cityMaxGrainLimit;   // 양식 한도 (초과 시 자동 환전)
  extern int g_cityKeepGrain;       // 초과 시 남겨둘 군량 수치
  extern int g_cityExchangeRate;    // 1회 교환 단위 (비율)
  extern bool g_cityAutoExchangeEnabled;
  extern bool g_cityRevoltAlwaysZero;

  // 군단 자동배치 설정 (설정 파일 저장 대상)
  extern bool g_corpsAutoDeploymentEachCouncil;
  extern int g_corpsAutoScenarioId;
  extern int g_corpsAutoScenarioStartYear;
  extern int g_corpsAutoScenarioStartMonth;
  extern int g_corpsAutoForceLordId;
  extern int g_corpsAutoCorpsNo;
  extern int g_corpsAutoGovernorGeneralId;

  void DrawCityInfoWindow(uintptr_t p1, float scale);
  void RunAutoCityExchange();
  void RunYearlyRearSupport(uintptr_t p1);
  void ResetCorpsAutoDeploymentSession();
  void RunCityRevoltAlwaysZero();
  void ResetAllCityRevoltCounters();
  void MaximizeAllCityResources();
  void MaximizeAllCityDevMax();
  void MaximizeAllCityComMax();
  void MaximizeAllCityDefMax();
  void MaximizeAllCityTecMax();
  void MaximizeAllCitySoldierMax();
} // namespace DX11Base
