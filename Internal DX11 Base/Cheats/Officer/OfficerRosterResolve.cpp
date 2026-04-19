#include "../../pch.h"
#include "../../Cheats.h"
#include "OfficerRosterResolve.h"
#include "OfficerData.h"
#include "../../showlog.h"

namespace DX11Base {

namespace {

constexpr std::uintptr_t kExeStaticPtrRva = 0x034C8630;
// 각 단계: 이전 주소에서 *(base + offset) 로 다음 포인터를 읽음
constexpr std::uintptr_t kChainAddends[] = {0x48, 0x8, 0x10, 0x0, 0x8};
constexpr std::uintptr_t kSpecialtyAnchorRvas[] = {0x037B0000, 0x037FF430};
constexpr std::uintptr_t kSpecialtyChainAddends[] = {0x0, 0x20, 0x0};

inline bool ReadPointer(std::uintptr_t addr, std::uintptr_t *out) {
  if (!out || !addr)
    return false;
  if (!IsValidPtr(addr, sizeof(std::uintptr_t)))
    return false;
  std::uintptr_t v = *reinterpret_cast<std::uintptr_t *>(addr);
  if (v < 0x10000)
    return false;
  *out = v;
  return true;
}

} // namespace

bool TryResolveOfficerRosterArrayBase(std::uintptr_t exeBase, std::uintptr_t *outRosterBase) {
  if (!exeBase || !outRosterBase)
    return false;
  *outRosterBase = 0;

  std::uintptr_t p = 0;
  if (!ReadPointer(exeBase + kExeStaticPtrRva, &p))
    return false;

  for (std::uintptr_t add : kChainAddends) {
    if (!ReadPointer(p + add, &p))
      return false;
  }

  if (!IsValidPtr(p, 0x3D0))
    return false;

  // 사용자님의 확언에 따라, 이 체인이 성공하면 항상 1번 무장을 가리키는 것으로 신뢰합니다.
  *outRosterBase = p;
  
  return true;
}

bool TryResolveSpecialtyArrayBase(std::uintptr_t exeBase, std::uintptr_t *outSpecialtyBase) {
  if (!exeBase || !outSpecialtyBase)
    return false;
  *outSpecialtyBase = 0;

  // 1) 최신 기준: game root (SAN8R.exe + 0x034C8630)에서 우선 해석
  //    *[exe+034C8630] -> +0x20 -> +0x0
  {
    std::uintptr_t p = 0;
    if (ReadPointer(exeBase + kExeStaticPtrRva, &p)) {
      bool chainOk = true;
      int step = 0;
      for (std::uintptr_t add : kSpecialtyChainAddends) {
        std::uintptr_t nextP = 0;
        if (!ReadPointer(p + add, &nextP)) {
          AddLog(u8"[명품체인] 단계 %d 실패 (Addr:%p, Offset:+%p)", step, (void*)p, (void*)add);
          chainOk = false;
          break;
        }
        p = nextP;
        step++;
      }
      if (chainOk && IsValidPtr(p, 0x38)) {
        *outSpecialtyBase = p;
        AddLog(u8"[명품체인] 최종 주소 확보 성공: %p", (void*)p);
        return true;
      }

    } else {
        AddLog(u8"[명품체인] 루트 포인터 읽기 실패 (Addr:%p)", (void*)(exeBase + kExeStaticPtrRva));
    }
  }


  // 2) 구버전/대체 빌드 fallback: officerBase static anchor 기반
  for (std::uintptr_t anchorRva : kSpecialtyAnchorRvas) {
    std::uintptr_t p = 0;
    if (!ReadPointer(exeBase + anchorRva, &p))
      continue;

    bool chainOk = true;
    for (std::uintptr_t add : kSpecialtyChainAddends) {
      if (!ReadPointer(p + add, &p)) {
        chainOk = false;
        break;
      }
    }
    if (!chainOk)
      continue;

    if (!IsValidPtr(p, 0x38))
      continue;

    *outSpecialtyBase = p;
    return true;
  }

  return false;
}

} // namespace DX11Base
