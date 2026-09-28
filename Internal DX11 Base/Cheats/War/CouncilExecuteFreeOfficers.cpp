#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "CouncilExecuteFreeOfficers.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace DX11Base {

  bool bCouncilExecuteFreeOfficers = false;

  namespace {
    constexpr uintptr_t kHookOffset = 0x018C9CA8;
    constexpr uintptr_t kOriginalCallOffset = 0x0175B800;
    constexpr uintptr_t kReturnOffset = 0x018C9CAD;
    constexpr uintptr_t kManagerRootOffset = 0x02E98BC8;
    constexpr uintptr_t kNativeAppendOffset = 0x0001A0C0;
    constexpr size_t kCaveSize = 0x400;

    constexpr std::array<uint8_t, 5> kOriginal = {
        0xE8, 0x53, 0x1B, 0xE9, 0xFF};

    bool gApplied = false;
    uintptr_t gCave = 0;
    std::array<uint8_t, kOriginal.size()> gPatched{};

    bool ReadBytes(uintptr_t address, void* output, size_t size) {
      if (!address || !output || size == 0)
        return false;
      __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t* expected, size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(address, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t address, const uint8_t* bytes, size_t size) {
      if (!address || !bytes || size == 0)
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<void*>(address), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool written = false;
      __try {
        std::memcpy(reinterpret_cast<void*>(address), bytes, size);
        written = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
      if (written)
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<const void*>(address), size);
      return written;
    }

    void Emit(std::vector<uint8_t>& code,
              std::initializer_list<uint8_t> bytes) {
      code.insert(code.end(), bytes.begin(), bytes.end());
    }

    size_t EmitRel32(std::vector<uint8_t>& code) {
      const size_t offset = code.size();
      code.insert(code.end(), sizeof(int32_t), 0);
      return offset;
    }

    bool PatchRel32(std::vector<uint8_t>& code, size_t displacementOffset,
                    int64_t displacement) {
      if (displacementOffset + sizeof(int32_t) > code.size() ||
          displacement < INT32_MIN || displacement > INT32_MAX)
        return false;
      const int32_t relative = static_cast<int32_t>(displacement);
      std::memcpy(code.data() + displacementOffset, &relative, sizeof(relative));
      return true;
    }

    bool PatchInternalRel32(std::vector<uint8_t>& code,
                            size_t displacementOffset, size_t targetOffset) {
      return PatchRel32(
          code, displacementOffset,
          static_cast<int64_t>(targetOffset) -
              static_cast<int64_t>(displacementOffset + sizeof(int32_t)));
    }

    bool PatchExternalRel32(std::vector<uint8_t>& code, uintptr_t cave,
                            size_t displacementOffset, uintptr_t target) {
      const uintptr_t nextInstruction =
          cave + displacementOffset + sizeof(int32_t);
      return PatchRel32(code, displacementOffset,
                        static_cast<int64_t>(target) -
                            static_cast<int64_t>(nextInstruction));
    }

    size_t EmitNearJump(std::vector<uint8_t>& code, uint8_t condition) {
      Emit(code, {0x0F, condition});
      return EmitRel32(code);
    }

    bool BuildPayload(uintptr_t exeBase, uintptr_t cave,
                      std::vector<uint8_t>& code) {
      code.clear();
      code.reserve(256);

      // Preserve the original CALL at +18C9CA8 before adding candidates.
      Emit(code, {0xE8});
      const size_t originalCallRel = EmitRel32(code);

      // CT original stack/register sequence.
      Emit(code, {0x53, 0x56, 0x41, 0x54, 0x41, 0x55,
                  0x48, 0x83, 0xEC, 0x30});

      // r13 = [SAN8RPK.exe+2E98BC8]
      Emit(code, {0x4C, 0x8B, 0x2D});
      const size_t managerRootRel = EmitRel32(code);
      Emit(code, {0x4D, 0x85, 0xED});
      const size_t rootNull = EmitNearJump(code, 0x84); // jz cleanup

      // Only the player protagonist acting as ruler may receive additions.
      Emit(code, {0x49, 0x39, 0xBD, 0xE0, 0x00, 0x00, 0x00});
      const size_t notProtagonist = EmitNearJump(code, 0x85); // jne cleanup
      Emit(code, {0x48, 0x8B, 0x47, 0x10, 0x48, 0x85, 0xC0});
      const size_t noRulerStatus = EmitNearJump(code, 0x84);
      Emit(code, {0x80, 0x78, 0x08, 0x01});
      const size_t notRuler = EmitNearJump(code, 0x85);
      Emit(code, {0x48, 0x8B, 0x5F, 0x18, 0x48, 0x85, 0xDB});
      const size_t noRulerForce = EmitNearJump(code, 0x84);

      Emit(code, {0x41, 0xBC, 0x01, 0x00, 0x00, 0x00}); // r12d = 1
      const size_t loop = code.size();

      // rsi = [r13 + r12*8 + 21BDF0]
      Emit(code, {0x4B, 0x8B, 0xB4, 0xE5, 0xF0, 0xBD, 0x21, 0x00,
                  0x48, 0x85, 0xF6});
      const size_t noOfficer = EmitNearJump(code, 0x84); // jz next

      // Native virtual activity check: officer->vtable[0x48](officer).
      Emit(code, {0x48, 0x8B, 0x06, 0x48, 0x85, 0xC0});
      const size_t noVtable = EmitNearJump(code, 0x84);
      Emit(code, {0x48, 0x89, 0xF1, 0xFF, 0x50, 0x48,
                  0x84, 0xC0});
      const size_t inactive = EmitNearJump(code, 0x84);

      // status exists and status->kind == 0x0A (unaffiliated/free officer).
      Emit(code, {0x48, 0x8B, 0x46, 0x10, 0x48, 0x85, 0xC0});
      const size_t noStatus = EmitNearJump(code, 0x84);
      Emit(code, {0x80, 0x78, 0x08, 0x0A});
      const size_t wrongStatus = EmitNearJump(code, 0x85);

      // The officer itself must not already belong to a force.
      Emit(code, {0x48, 0x83, 0x7E, 0x18, 0x00});
      const size_t hasForce = EmitNearJump(code, 0x85);

      // officer->city->district->force must match the acting ruler's force.
      Emit(code, {0x48, 0x8B, 0x46, 0x20, 0x48, 0x85, 0xC0});
      const size_t noCity = EmitNearJump(code, 0x84);
      Emit(code, {0x48, 0x8B, 0x80, 0x90, 0x00, 0x00, 0x00,
                  0x48, 0x85, 0xC0});
      const size_t noDistrict = EmitNearJump(code, 0x84);
      Emit(code, {0x48, 0x39, 0x58, 0x10});
      const size_t otherForceCity = EmitNearJump(code, 0x85);

      // Native append(list=R15, value=&officer) from the CT.
      Emit(code, {0x48, 0x89, 0x74, 0x24, 0x20,
                  0x48, 0x8D, 0x54, 0x24, 0x20,
                  0x4C, 0x89, 0xF9, 0xE8});
      const size_t appendCallRel = EmitRel32(code);

      const size_t next = code.size();
      Emit(code, {0x41, 0xFF, 0xC4,
                  0x41, 0x81, 0xFC, 0x50, 0x14, 0x00, 0x00});
      const size_t continueLoop = EmitNearJump(code, 0x8E); // jle loop

      const size_t cleanup = code.size();
      Emit(code, {0x48, 0x83, 0xC4, 0x30,
                  0x41, 0x5D, 0x41, 0x5C, 0x5E, 0x5B,
                  0xE9});
      const size_t returnRel = EmitRel32(code);

      const size_t nextBranches[] = {
          noOfficer, noVtable, inactive, noStatus, wrongStatus,
          hasForce, noCity, noDistrict, otherForceCity};
      for (const size_t branch : nextBranches) {
        if (!PatchInternalRel32(code, branch, next))
          return false;
      }

      const size_t cleanupBranches[] = {
          rootNull, notProtagonist, noRulerStatus, notRuler, noRulerForce};
      for (const size_t branch : cleanupBranches) {
        if (!PatchInternalRel32(code, branch, cleanup))
          return false;
      }

      return PatchInternalRel32(code, continueLoop, loop) &&
             PatchExternalRel32(code, cave, originalCallRel,
                                exeBase + kOriginalCallOffset) &&
             PatchExternalRel32(code, cave, managerRootRel,
                                exeBase + kManagerRootOffset) &&
             PatchExternalRel32(code, cave, appendCallRel,
                                exeBase + kNativeAppendOffset) &&
             PatchExternalRel32(code, cave, returnRel,
                                exeBase + kReturnOffset);
    }

    bool BuildPatchedBytes(uintptr_t hook, uintptr_t cave) {
      const int64_t distance = static_cast<int64_t>(cave) -
                               static_cast<int64_t>(hook + 5);
      if (distance < INT32_MIN || distance > INT32_MAX)
        return false;
      gPatched[0] = 0xE9;
      const int32_t relative = static_cast<int32_t>(distance);
      std::memcpy(gPatched.data() + 1, &relative, sizeof(relative));
      return true;
    }
  } // namespace

  bool IsCouncilExecuteFreeOfficersApplied() {
    return gApplied;
  }

  bool SetCouncilExecuteFreeOfficers(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (!exeBase) {
      AddLog(u8"[평정처단] SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hook = exeBase + kHookOffset;
    if (enable) {
      if (gApplied)
        return true;

      if (!BytesEqual(hook, kOriginal.data(), kOriginal.size())) {
        AddLog(u8"[평정처단] 적용 거부: +18C9CA8 원본 바이트가 예상값과 다릅니다.");
        return false;
      }

      if (!gCave) {
        gCave = AllocNear(hook, kCaveSize);
        if (!gCave) {
          AddLog(u8"[평정처단] 적용 실패: 코드케이브를 할당하지 못했습니다.");
          return false;
        }
      }

      std::vector<uint8_t> payload;
      if (!BuildPayload(exeBase, gCave, payload) ||
          payload.size() > kCaveSize || !BuildPatchedBytes(hook, gCave)) {
        AddLog(u8"[평정처단] 적용 실패: 코드케이브를 구성하지 못했습니다.");
        return false;
      }

      std::memset(reinterpret_cast<void*>(gCave), 0xCC, kCaveSize);
      std::memcpy(reinterpret_cast<void*>(gCave), payload.data(), payload.size());
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<const void*>(gCave), kCaveSize);

      if (!ApplyJmp(hook, gCave, kOriginal.size())) {
        AddLog(u8"[평정처단] 적용 실패: +18C9CA8 훅을 기록하지 못했습니다.");
        return false;
      }
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<const void*>(hook), kOriginal.size());

      if (!BytesEqual(hook, gPatched.data(), gPatched.size())) {
        WriteBytes(hook, kOriginal.data(), kOriginal.size());
        AddLog(u8"[평정처단] 적용 실패: 훅 검증 실패 후 원본을 복구했습니다.");
        return false;
      }

      gApplied = true;
      AddLog(u8"[평정처단] 세력 도시 재야 무장을 처단 후보에 추가");
      return true;
    }

    if (!gApplied)
      return true;

    // Never overwrite another patch while disabling.
    if (!BytesEqual(hook, gPatched.data(), gPatched.size())) {
      AddLog(u8"[평정처단] 해제 거부: +18C9CA8이 이 기능의 훅과 다릅니다.");
      return false;
    }

    if (!WriteBytes(hook, kOriginal.data(), kOriginal.size()) ||
        !BytesEqual(hook, kOriginal.data(), kOriginal.size())) {
      AddLog(u8"[평정처단] 해제 실패: 원본 바이트를 복구하지 못했습니다.");
      return false;
    }

    gApplied = false;
    // A thread may still be returning through the cave, so retain it until exit.
    AddLog(u8"[평정처단] 세력 도시 재야 무장 처단 후보 추가 해제");
    return true;
  }

} // namespace DX11Base
