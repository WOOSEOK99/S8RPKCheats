#pragma once

// ======================================================
// 특수 능력 (Special Unit Abilities) - 뼈대 파일
// 군악대 / 무쌍보명 / 불꽃기병 / 원격궁병 /
// 등갑군 / 총사령관 / 기습부대 / 대군사
// ======================================================
#include <cstdint>

namespace DX11Base {

  // ── 군악대 ──────────────────────────────────────────
  // 주변 아군 사기 회복 강화
  void SetGunakdae(bool enable);

  // ── 무쌍 보명 ────────────────────────────────────────
  // 무쌍 보명 전용 보정 (공격/방어 등)
  void SetMusangBomyeong(bool enable);

  // ── 불꽃기병 ────────────────────────────────────────
  // 불꽃기병 효과 강화 (화염 피해 등)
  void SetFlameKnight(bool enable);

  // ── 원격 궁병 ────────────────────────────────────────
  // 원격 궁병 사거리 / 공격력 강화
  void SetRangedArcher(bool enable);

  // ── 등갑군 ──────────────────────────────────────────
  // 등갑군 방어력 / 화공 취약 조건 조정
  void SetRattanArmor(bool enable);

  // ── 총사령관 ─────────────────────────────────────────
  // 총사령관 버프: 전군 사기/공격력 보정
  void SetGeneralissimo(bool enable);

  // ── 기습부대 ─────────────────────────────────────────
  // 기습부대: 선제 공격 / 측후방 보정
  void SetAmbushUnit(bool enable);

  // ── 대군사 ──────────────────────────────────────────
  // 대군사 권한 강화: 전법 강화 / 특수 지시 등
  void SetGrandStrategist(bool enable);

  // ── 런타임 갱신 ──────────────────────────────────────
  // 전장에 배치된 무장들을 스캔하여 특수 능력을 활성화/비활성화합니다.
  void UpdateSpecialAbilities(int unitCountTotal, uintptr_t unitListBase, uintptr_t exeBase);

} // namespace DX11Base
