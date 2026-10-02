#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "DomesticRewardCondition.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kHookOffset = 0x01C813D1;
    constexpr uintptr_t kResumeOffset = 0x01C813D6;
    constexpr size_t kHookSize = 5;
    constexpr size_t kCaveSize = 0x100;

    const uint8_t kOriginal[kHookSize] = {
        0x48, 0x89, 0x74, 0x24, 0x30};

    volatile LONG g_modeValue = 0;
    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_patched[kHookSize]{};

    bool ReadBytes(uintptr_t address, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(address, size))
        return false;

      __try {
        std::memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(out, 0, size);
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t *expected, size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(address, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t address, const uint8_t *bytes, size_t size) {
      if (!bytes || !size || !IsValidPtr(address, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address),
                          size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
      }

      bool success = false;
      __try {
        std::memcpy(reinterpret_cast<void *>(address), bytes, size);
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);
      if (success) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(address), size);
      }
      return success;
    }

    bool BuildRelativeJump(uintptr_t hookAddress,
                           uintptr_t caveAddress,
                           uint8_t out[kHookSize]) {
      const int64_t relative =
          static_cast<int64_t>(caveAddress) -
          static_cast<int64_t>(hookAddress + kHookSize);
      if (relative < INT32_MIN || relative > INT32_MAX)
        return false;

      out[0] = 0xE9;
      const int32_t rel32 = static_cast<int32_t>(relative);
      std::memcpy(out + 1, &rel32, sizeof(rel32));
      return true;
    }

    void AppendU64(std::vector<uint8_t> &out, uintptr_t value) {
      const uint64_t v = static_cast<uint64_t>(value);
      const auto *bytes = reinterpret_cast<const uint8_t *>(&v);
      out.insert(out.end(), bytes, bytes + sizeof(v));
    }

    size_t AppendRel8(std::vector<uint8_t> &out, uint8_t opcode) {
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

    void AppendAbsoluteJump(std::vector<uint8_t> &out, uintptr_t destination) {
      const uint8_t jump[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
      out.insert(out.end(), std::begin(jump), std::end(jump));
      AppendU64(out, destination);
    }

    bool BuildPayload(uintptr_t exeBase, std::vector<uint8_t> &out) {
      out.clear();
      out.reserve(128);

      std::vector<size_t> nativeBranches;
      size_t matchedBranch = 0;

      // 검사 중 변경하는 RFLAGS/RAX를 보존합니다.
      out.insert(out.end(), {0x9C, 0x50}); // pushfq / push rax

      // r13 != 0
      out.insert(out.end(), {0x4D, 0x85, 0xED});
      nativeBranches.push_back(AppendRel8(out, 0x74)); // jz native

      // r13 != [rbp-48]
      out.insert(out.end(), {0x4C, 0x3B, 0x6D, 0xB8});
      nativeBranches.push_back(AppendRel8(out, 0x74)); // je native

      // [r13+20] == [rbp-68]
      out.insert(out.end(), {0x49, 0x8B, 0x45, 0x20});
      out.insert(out.end(), {0x48, 0x3B, 0x45, 0x98});
      nativeBranches.push_back(AppendRel8(out, 0x75)); // jne native

      // byte ptr [r13+F8] != 0
      out.insert(out.end(), {0x41, 0x80, 0xBD, 0xF8, 0x00, 0x00, 0x00, 0x00});
      nativeBranches.push_back(AppendRel8(out, 0x74)); // je native

      // dword ptr [r13+348] >= 200
      out.insert(out.end(), {0x41, 0x81, 0xBD, 0x48, 0x03, 0x00, 0x00,
                             0xC8, 0x00, 0x00, 0x00});
      nativeBranches.push_back(AppendRel8(out, 0x72)); // jb native

      // 현재 모드는 cave에서 읽습니다. 2=등수 무관, 1=Top3입니다.
      out.insert(out.end(), {0x48, 0xB8});
      AppendU64(out, reinterpret_cast<uintptr_t>(&g_modeValue));
      out.insert(out.end(), {0x8B, 0x00}); // mov eax,[rax]
      out.insert(out.end(), {0x83, 0xF8, 0x02});
      matchedBranch = AppendRel8(out, 0x74); // je matched
      out.insert(out.end(), {0x83, 0xF8, 0x01});
      nativeBranches.push_back(AppendRel8(out, 0x75)); // jne native

      // Top3: ([r13+34C] - 1) <= 2
      out.insert(out.end(), {0x41, 0x8B, 0x85, 0x4C, 0x03, 0x00, 0x00});
      out.insert(out.end(), {0xFF, 0xC8});
      out.insert(out.end(), {0x83, 0xF8, 0x02});
      nativeBranches.push_back(AppendRel8(out, 0x77)); // ja native

      const size_t matchedIndex = out.size();
      out.insert(out.end(), {0x4C, 0x89, 0xEE}); // mov rsi,r13

      const size_t nativeIndex = out.size();
      out.insert(out.end(), {0x58, 0x9D}); // pop rax / popfq

      // RSP를 복구한 뒤 원본 명령을 정확히 재실행합니다.
      out.insert(out.end(), std::begin(kOriginal), std::end(kOriginal));
      AppendAbsoluteJump(out, exeBase + kResumeOffset);

      if (!PatchRel8(out, matchedBranch, matchedIndex))
        return false;
      for (const size_t displacementIndex : nativeBranches) {
        if (!PatchRel8(out, displacementIndex, nativeIndex))
          return false;
      }

      return out.size() <= kCaveSize;
    }

    bool WritePayload(uintptr_t cave, const std::vector<uint8_t> &payload) {
      if (!cave || payload.empty() || payload.size() > kCaveSize)
        return false;

      bool success = false;
      __try {
        std::memset(reinterpret_cast<void *>(cave), 0xCC, kCaveSize);
        std::memcpy(reinterpret_cast<void *>(cave), payload.data(), payload.size());
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      if (success) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(cave), kCaveSize);
      }
      return success;
    }

    bool IsValidMode(DomesticRewardMode mode) {
      return mode == DomesticRewardMode::Off ||
             mode == DomesticRewardMode::Achievement200Top3 ||
             mode == DomesticRewardMode::Achievement200AnyRank;
    }
  } // namespace

  DomesticRewardMode GetDomesticRewardMode() {
    const LONG value = InterlockedCompareExchange(&g_modeValue, 0, 0);
    if (value == static_cast<LONG>(DomesticRewardMode::Achievement200Top3))
      return DomesticRewardMode::Achievement200Top3;
    if (value == static_cast<LONG>(DomesticRewardMode::Achievement200AnyRank))
      return DomesticRewardMode::Achievement200AnyRank;
    return DomesticRewardMode::Off;
  }

  bool IsDomesticRewardHookApplied() {
    return g_applied;
  }

  bool SetDomesticRewardMode(DomesticRewardMode mode) {
    if (!IsValidMode(mode)) {
      AddLog(u8"[내정포상] 지원하지 않는 모드입니다.");
      return false;
    }

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[내정포상] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hookAddress = exeBase + kHookOffset;

    if (mode == DomesticRewardMode::Off) {
      if (!g_applied) {
        InterlockedExchange(&g_modeValue, 0);
        return true;
      }

      if (!BytesEqual(hookAddress, g_patched, kHookSize)) {
        AddLog(u8"[내정포상] 해제 거부: 현재 hook이 이 기능의 patch와 다릅니다. (+1C813D1)");
        return false;
      }

      if (!WriteBytes(hookAddress, kOriginal, kHookSize) ||
          !BytesEqual(hookAddress, kOriginal, kHookSize)) {
        AddLog(u8"[내정포상] 원본 hook 복구 또는 검증 실패");
        return false;
      }

      // 실행 중인 스레드가 cave에 있을 수 있으므로 cave는 프로세스 종료까지 유지합니다.
      InterlockedExchange(&g_modeValue, 0);
      g_applied = false;
      AddLog(u8"[내정포상] 포상 조건 완화 OFF");
      return true;
    }

    if (g_applied) {
      InterlockedExchange(&g_modeValue, static_cast<LONG>(mode));
      AddLog(u8"[내정포상] 모드 전환: %s",
             mode == DomesticRewardMode::Achievement200Top3
                 ? "200% 이상 / 3위 이내"
                 : "200% 이상 / 등수 무관");
      return true;
    }

    if (!BytesEqual(hookAddress, kOriginal, kHookSize)) {
      AddLog(u8"[내정포상] 적용 거부: 원본 바이트 불일치 (+1C813D1)");
      return false;
    }

    bool allocatedNow = false;
    if (!g_cave) {
      g_cave = AllocNear(hookAddress, kCaveSize);
      allocatedNow = g_cave != 0;
      if (!g_cave) {
        AddLog(u8"[내정포상] 근거리 코드케이브 할당 실패");
        return false;
      }
    }

    if (!BuildRelativeJump(hookAddress, g_cave, g_patched)) {
      if (allocatedNow) {
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
      }
      AddLog(u8"[내정포상] 코드케이브가 rel32 JMP 범위를 벗어났습니다.");
      return false;
    }

    std::vector<uint8_t> payload;
    if (!BuildPayload(exeBase, payload) || !WritePayload(g_cave, payload)) {
      if (allocatedNow) {
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
      }
      AddLog(u8"[내정포상] 코드케이브 payload 구성 또는 기록 실패");
      return false;
    }

    // cave가 실행되기 전에 원하는 모드 값을 먼저 게시합니다.
    InterlockedExchange(&g_modeValue, static_cast<LONG>(mode));

    // 준비 중 외부 패치가 들어온 경우 덮어쓰지 않습니다.
    if (!BytesEqual(hookAddress, kOriginal, kHookSize)) {
      InterlockedExchange(&g_modeValue, 0);
      AddLog(u8"[내정포상] 적용 거부: 설치 직전 원본 상태가 변경되었습니다.");
      return false;
    }

    if (!WriteBytes(hookAddress, g_patched, kHookSize)) {
      InterlockedExchange(&g_modeValue, 0);
      AddLog(u8"[내정포상] hook 적용 실패");
      return false;
    }

    if (!BytesEqual(hookAddress, g_patched, kHookSize)) {
      // 외부에서 바뀐 상태라면 임의로 원본을 덮어쓰지 않습니다.
      InterlockedExchange(&g_modeValue, 0);
      AddLog(u8"[내정포상] hook 적용 후 검증 실패 - 현재 바이트를 보존합니다.");
      return false;
    }

    g_applied = true;
    AddLog(u8"[내정포상] 포상 조건 완화 활성화: %s",
           mode == DomesticRewardMode::Achievement200Top3
               ? "200% 이상 / 3위 이내"
               : "200% 이상 / 등수 무관");
    return true;
  }
} // namespace DX11Base
