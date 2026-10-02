#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "MissionCpuHeroExclusion.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  bool bMissionCpuHeroExclusion = false;

  namespace {
    constexpr uintptr_t kHookOffset = 0x01453801;
    constexpr uintptr_t kAuxAssertOffset = 0x01453790;
    constexpr uintptr_t kManagerRootOffset = 0x02E98BC8;
    constexpr uintptr_t kResumeOffset = 0x0145380A;
    constexpr uintptr_t kSkipOffset = 0x01453870;

    constexpr size_t kHookSize = 9;
    constexpr size_t kAuxAssertSize = 7;
    constexpr size_t kCaveSize = 0x100;

    const uint8_t kHookOriginal[kHookSize] = {
        0x49, 0x8D, 0x7F, 0x20,
        0xBE, 0x05, 0x00, 0x00, 0x00};

    const uint8_t kAuxAssertOriginal[kAuxAssertSize] = {
        0x48, 0x3B, 0x98, 0xE0, 0x00, 0x00, 0x00};

    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_hookPatched[kHookSize]{};

    bool ReadBytes(uintptr_t addr, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(addr, size))
        return false;

      __try {
        std::memcpy(out, reinterpret_cast<const void *>(addr), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(out, 0, size);
        return false;
      }
    }

    bool BytesEqual(uintptr_t addr,
                    const uint8_t *expected,
                    size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(addr, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t addr,
                    const uint8_t *bytes,
                    size_t size) {
      if (!bytes || !size || !IsValidPtr(addr, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr),
                          size,
                          PAGE_EXECUTE_READWRITE,
                          &oldProtect)) {
        return false;
      }

      bool ok = false;
      __try {
        std::memcpy(reinterpret_cast<void *>(addr), bytes, size);
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(addr),
                     size,
                     oldProtect,
                     &ignored);

      if (ok) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(addr),
                              size);
      }
      return ok;
    }

    bool BuildRelativeJump(uintptr_t hookAddr,
                           uintptr_t caveAddr,
                           uint8_t out[kHookSize]) {
      const int64_t relative =
          static_cast<int64_t>(caveAddr) -
          static_cast<int64_t>(hookAddr + 5);
      if (relative < INT32_MIN || relative > INT32_MAX)
        return false;

      std::memset(out, 0x90, kHookSize);
      out[0] = 0xE9;
      const int32_t rel32 = static_cast<int32_t>(relative);
      std::memcpy(out + 1, &rel32, sizeof(rel32));
      return true;
    }

    void AppendBytes(std::vector<uint8_t> &out,
                     std::initializer_list<uint8_t> bytes) {
      out.insert(out.end(), bytes.begin(), bytes.end());
    }

    void AppendU64(std::vector<uint8_t> &out, uintptr_t value) {
      const uint64_t v = static_cast<uint64_t>(value);
      const auto *p = reinterpret_cast<const uint8_t *>(&v);
      out.insert(out.end(), p, p + sizeof(v));
    }

    size_t AppendRel8Jump(std::vector<uint8_t> &out, uint8_t opcode) {
      out.push_back(opcode);
      out.push_back(0);
      return out.size() - 1;
    }

    bool PatchRel8(std::vector<uint8_t> &out,
                   size_t displacementIndex,
                   size_t targetIndex) {
      if (displacementIndex >= out.size())
        return false;

      const int64_t relative =
          static_cast<int64_t>(targetIndex) -
          static_cast<int64_t>(displacementIndex + 1);
      if (relative < INT8_MIN || relative > INT8_MAX)
        return false;

      out[displacementIndex] =
          static_cast<uint8_t>(static_cast<int8_t>(relative));
      return true;
    }

    void AppendAbsoluteJump(std::vector<uint8_t> &out,
                            uintptr_t destination) {
      AppendBytes(out, {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
      AppendU64(out, destination);
    }

    bool BuildPayload(uintptr_t exeBase, std::vector<uint8_t> &out) {
      out.clear();
      out.reserve(128);

      std::vector<size_t> nativeBranches;

      // 원래 흐름의 RFLAGS와 RAX를 보존한 채 조건만 검사합니다.
      AppendBytes(out, {0x9C, 0x50}); // pushfq / push rax

      AppendBytes(out, {0x48, 0xB8}); // mov rax, imm64
      AppendU64(out, exeBase + kManagerRootOffset);
      AppendBytes(out, {0x48, 0x8B, 0x00}); // mov rax, [rax]
      AppendBytes(out, {0x48, 0x85, 0xC0}); // test rax, rax
      nativeBranches.push_back(AppendRel8Jump(out, 0x74)); // jz native

      // rbx == [managerRoot+0xE0]
      AppendBytes(out, {0x48, 0x3B, 0x98, 0xE0, 0x00, 0x00, 0x00});
      nativeBranches.push_back(AppendRel8Jump(out, 0x75)); // jne native

      // [rbx+0x10] != nullptr
      AppendBytes(out, {0x48, 0x8B, 0x43, 0x10}); // mov rax, [rbx+10h]
      AppendBytes(out, {0x48, 0x85, 0xC0});       // test rax, rax
      nativeBranches.push_back(AppendRel8Jump(out, 0x74)); // jz native

      // byte ptr [[rbx+0x10]+0x8] in [4, 9]
      AppendBytes(out, {0x0F, 0xB6, 0x40, 0x08}); // movzx eax, byte ptr [rax+8]
      AppendBytes(out, {0x83, 0xF8, 0x04});       // cmp eax, 4
      nativeBranches.push_back(AppendRel8Jump(out, 0x72)); // jb native
      AppendBytes(out, {0x83, 0xF8, 0x09});       // cmp eax, 9
      nativeBranches.push_back(AppendRel8Jump(out, 0x77)); // ja native

      // byte ptr [rbx+0x350] == 0
      AppendBytes(out, {0x80, 0xBB, 0x50, 0x03, 0x00, 0x00, 0x00});
      nativeBranches.push_back(AppendRel8Jump(out, 0x75)); // jne native

      // 모든 조건이 맞으면 CPU의 강제 후보 등록 구간을 건너뜁니다.
      AppendBytes(out, {0x58, 0x9D}); // pop rax / popfq
      AppendAbsoluteJump(out, exeBase + kSkipOffset);

      const size_t nativeIndex = out.size();

      // 원본 9바이트를 재실행한 뒤 원래 다음 명령으로 복귀합니다.
      AppendBytes(out, {0x58, 0x9D});
      out.insert(out.end(),
                 std::begin(kHookOriginal),
                 std::end(kHookOriginal));
      AppendAbsoluteJump(out, exeBase + kResumeOffset);

      for (const size_t displacementIndex : nativeBranches) {
        if (!PatchRel8(out, displacementIndex, nativeIndex))
          return false;
      }

      return out.size() <= kCaveSize;
    }

    bool WritePayload(uintptr_t cave, const std::vector<uint8_t> &payload) {
      if (!cave || payload.empty() || payload.size() > kCaveSize)
        return false;

      bool ok = false;
      __try {
        std::memset(reinterpret_cast<void *>(cave), 0xCC, kCaveSize);
        std::memcpy(reinterpret_cast<void *>(cave),
                    payload.data(),
                    payload.size());
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      if (ok) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(cave),
                              kCaveSize);
      }
      return ok;
    }
  } // namespace

  bool IsMissionCpuHeroExclusionApplied() {
    return g_applied;
  }

  bool SetMissionCpuHeroExclusion(bool enable) {
    if (enable == g_applied) {
      bMissionCpuHeroExclusion = g_applied;
      return true;
    }

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[임무CPU] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    const uintptr_t hookAddr = exeBase + kHookOffset;
    const uintptr_t auxAssertAddr = exeBase + kAuxAssertOffset;

    if (!enable) {
      if (!BytesEqual(hookAddr, g_hookPatched, kHookSize)) {
        AddLog(u8"[임무CPU] 해제 보류: 현재 hook이 이 기능의 patch와 다릅니다. (+1453801)");
        bMissionCpuHeroExclusion = g_applied;
        return false;
      }

      if (!WriteBytes(hookAddr, kHookOriginal, kHookSize) ||
          !BytesEqual(hookAddr, kHookOriginal, kHookSize)) {
        AddLog(u8"[임무CPU] 해제 실패: 원본 바이트 복구 또는 검증 실패");
        bMissionCpuHeroExclusion = g_applied;
        return false;
      }

      // hook을 통과 중인 스레드가 있을 수 있으므로 cave는 프로세스 종료까지 유지합니다.
      g_applied = false;
      bMissionCpuHeroExclusion = false;
      AddLog(u8"[임무CPU] 미지원 주인공 CPU 강제 배정 제외 비활성화");
      return true;
    }

    if (!BytesEqual(auxAssertAddr, kAuxAssertOriginal, kAuxAssertSize) ||
        !BytesEqual(hookAddr, kHookOriginal, kHookSize)) {
      AddLog(u8"[임무CPU] 적용 보류: 원본 바이트 불일치 (+1453790 / +1453801)");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    bool allocatedNow = false;
    if (!g_cave) {
      g_cave = AllocNear(hookAddr, kCaveSize);
      allocatedNow = g_cave != 0;
      if (!g_cave) {
        AddLog(u8"[임무CPU] 근거리 코드케이브 할당 실패");
        bMissionCpuHeroExclusion = g_applied;
        return false;
      }
    }

    if (!BuildRelativeJump(hookAddr, g_cave, g_hookPatched)) {
      if (allocatedNow) {
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
      }
      AddLog(u8"[임무CPU] 적용 보류: 코드케이브가 rel32 JMP 범위를 벗어났습니다.");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    std::vector<uint8_t> payload;
    if (!BuildPayload(exeBase, payload) ||
        !WritePayload(g_cave, payload)) {
      if (allocatedNow) {
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
      }
      AddLog(u8"[임무CPU] 코드케이브 payload 구성 또는 기록 실패");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    // cave 준비 중 외부 패치가 들어온 경우 덮어쓰지 않습니다.
    if (!BytesEqual(auxAssertAddr, kAuxAssertOriginal, kAuxAssertSize) ||
        !BytesEqual(hookAddr, kHookOriginal, kHookSize)) {
      AddLog(u8"[임무CPU] 적용 보류: 설치 직전 원본 상태가 변경되었습니다.");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    if (!WriteBytes(hookAddr, g_hookPatched, kHookSize)) {
      if (BytesEqual(hookAddr, g_hookPatched, kHookSize))
        WriteBytes(hookAddr, kHookOriginal, kHookSize);
      AddLog(u8"[임무CPU] hook 적용 실패");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    if (!BytesEqual(hookAddr, g_hookPatched, kHookSize)) {
      // 검증 시점에 외부에서 바뀐 상태라면 임의로 원본을 덮어쓰지 않습니다.
      AddLog(u8"[임무CPU] hook 적용 후 검증 실패 - 현재 바이트를 보존합니다.");
      bMissionCpuHeroExclusion = g_applied;
      return false;
    }

    g_applied = true;
    bMissionCpuHeroExclusion = true;
    AddLog(u8"[임무CPU] 미지원 주인공 CPU 강제 배정 제외 활성화 (+1453801)");
    return true;
  }
} // namespace DX11Base
