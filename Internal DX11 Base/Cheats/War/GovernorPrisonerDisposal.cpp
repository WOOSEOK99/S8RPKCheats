#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "GovernorPrisonerDisposal.h"

#include <cstdint>
#include <cstring>
#include <climits>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kQuestionFuncOffset = 0x00016710;
    constexpr uintptr_t kConsumePrivilegeFuncOffset = 0x017275E0;
    constexpr uintptr_t kGlobalPtrOffset = 0x02E98BC8;
    constexpr uintptr_t kUiCountPtrOffset = 0x034C8628;
    constexpr uintptr_t kUiLimitPtrOffset = 0x034C8660;
    constexpr uintptr_t kUiArrayPtrOffset = 0x034C8630;

    constexpr uintptr_t kMainHookOffset = 0x01E53BB2;
    constexpr uintptr_t kCollectCallOffset = 0x01E5CE39;
    constexpr uintptr_t kRelationsCallOffset = 0x01E5DE47;

    constexpr uintptr_t kAllowOffset = 0x01E53BBB;
    constexpr uintptr_t kDenyOffset = 0x01E540B7;
    constexpr uintptr_t kCollectTargetOffset = 0x01740200;
    constexpr uintptr_t kRelationsTargetOffset = 0x017A0EF0;

    constexpr size_t kCaveSize = 0x3E0;
    constexpr size_t kCollectWrapperOffset = 0x25E;
    constexpr size_t kRelationsWrapperOffset = 0x26C;

    constexpr size_t kGlobalPtrDataOffset = 0x310;
    constexpr size_t kUiCountDataOffset = 0x318;
    constexpr size_t kUiLimitDataOffset = 0x320;
    constexpr size_t kUiArrayDataOffset = 0x328;
    constexpr size_t kQuestionFuncDataOffset = 0x330;
    constexpr size_t kConsumeFuncDataOffset = 0x338;
    constexpr size_t kAllowDataOffset = 0x340;
    constexpr size_t kDenyDataOffset = 0x348;
    constexpr size_t kCollectTargetDataOffset = 0x350;
    constexpr size_t kRelationsTargetDataOffset = 0x358;
    constexpr size_t kConsumeFlagDataOffset = 0x360;
    constexpr size_t kFreeTextOffset = 0x368;
    constexpr size_t kConsumeTextOffset = 0x398;

    const uint8_t kMainOriginal[9] =
        {0x4C, 0x3B, 0xF6, 0x0F, 0x85, 0xFC, 0x04, 0x00, 0x00};
    const uint8_t kCollectOriginal[5] =
        {0xE8, 0xC2, 0x33, 0x8E, 0xFF};
    const uint8_t kRelationsOriginal[5] =
        {0xE8, 0xA4, 0x30, 0x94, 0xFF};
    const uint8_t kQuestionSignature[12] =
        {0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    const uint8_t kConsumeSignature[9] =
        {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC};

    const char kCaveHex[] =
        "4939F60F84F20000005052488B46104885C00F84DC000000807808020F85D2000000488B45E8483946180F85C4000000488B0500000000488B004885C00F84B10000000FB69030E01E0083FA010F87A10000004869D2A8000000488B841008DF1E004885C00F8489000000488B80900000004885C0747D488B561848395010757348397020756D488B05000000008038007418488B0500000000488B004885C0745280B8EA0000000074494D85ED7444488B0500000000488338007437418B5500488B05000000003B107328488B0500000000488B004885C0741948833CD0007412488B442460E82100000085C074045A58EB075A584939F6EB09488B0500000000FFE0488B0500000000FFE0515241504151415241534881ECA8000000F30F7F442440F30F7F4C2450F30F7F542460F30F7F5C2470F30F7FA42480000000F30F7FAC2490000000C74424300000000048894424384885C00F84C00000004889C1488B00FF504884C00F84AF000000488B442438488B48104885C90F849D00000048894C2438488B01FF504884C00F848A000000488B4424380FB640082C0C3C02767B488D0D00000000488B05000000008038007407488D0D00000000BAD004000041B8AC0400004531C9C7442420FFFFFF7F48C744242800000000488B0500000000FFD085C07435488B05000000008038007421488B0500000000488B004885C0741A80B8EA000000007411488B0500000000FFD0C744243001000000F30F6F442440F30F6F4C2450F30F6F542460F30F6F5C2470F30F6FA42480000000F30F6FAC24900000008B4424304881C4A8000000415B415A415941585A59C3E817000000488B0500000000FFE0E809000000488B0500000000FFE09C505241524153488B0500000000488B004885C074774C8B90E00000004D85D2746B4D8B5A104D85DB746241807B0802755B0FB69030E01E0083FA01774F4869D2A80000004C8B9C1010DF1E004D85DB743B4D395A18753549398BC0000000752C488B841008DF1E004885C0741F4C8B98900000004D85DB7413498B42184939431075094D39532075034C89D1415B415A5A589DC3900000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000074C788BC200004C82CD258C72000ECD35CB87CB92000C1C911C8200098CC84BD58D5DCC2A0ACB5C2C8B24CAE3F000000B9D28CAD200031001CAC7CB920008CC144BE58D5ECC5200074C788BC200004C82CD258C72000ECD35CB87CB92000C1C911C8200098CC84BD58D5DCC2A0ACB5C2C8B24CAE3F000000";

    struct LocalReloc {
      size_t fieldOffset;
      size_t targetOffset;
    };

    const LocalReloc kLocalRelocs[] = {
        {0x033, kGlobalPtrDataOffset},
        {0x08A, kConsumeFlagDataOffset},
        {0x096, kGlobalPtrDataOffset},
        {0x0B3, kUiCountDataOffset},
        {0x0C4, kUiLimitDataOffset},
        {0x0CF, kUiArrayDataOffset},
        {0x0FE, kAllowDataOffset},
        {0x107, kDenyDataOffset},
        {0x1A6, kFreeTextOffset},
        {0x1AD, kConsumeFlagDataOffset},
        {0x1B9, kConsumeTextOffset},
        {0x1DF, kQuestionFuncDataOffset},
        {0x1EC, kConsumeFlagDataOffset},
        {0x1F8, kGlobalPtrDataOffset},
        {0x210, kConsumeFuncDataOffset},
        {0x266, kCollectTargetDataOffset},
        {0x274, kRelationsTargetDataOffset},
        {0x284, kGlobalPtrDataOffset},
    };

    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_mainPatched[9]{};
    uint8_t g_collectPatched[5]{};
    uint8_t g_relationsPatched[5]{};

    int HexNibble(char c) {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      return -1;
    }

    bool DecodeCave(std::vector<uint8_t> &out) {
      out.clear();
      const size_t len = strlen(kCaveHex);
      if ((len & 1) != 0)
        return false;
      out.reserve(len / 2);
      for (size_t i = 0; i < len; i += 2) {
        const int hi = HexNibble(kCaveHex[i]);
        const int lo = HexNibble(kCaveHex[i + 1]);
        if (hi < 0 || lo < 0)
          return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
      }
      return out.size() == kCaveSize;
    }

    bool PatchLocalRel32(std::vector<uint8_t> &blob, size_t field, size_t target) {
      if (field + sizeof(int32_t) > blob.size() || target >= blob.size())
        return false;
      const int64_t rel = static_cast<int64_t>(target) -
                          static_cast<int64_t>(field + sizeof(int32_t));
      if (rel < INT32_MIN ||
          rel > INT32_MAX)
        return false;
      const int32_t rel32 = static_cast<int32_t>(rel);
      memcpy(blob.data() + field, &rel32, sizeof(rel32));
      return true;
    }

    bool PutQword(std::vector<uint8_t> &blob, size_t offset, uintptr_t value) {
      if (offset + sizeof(value) > blob.size())
        return false;
      memcpy(blob.data() + offset, &value, sizeof(value));
      return true;
    }

    bool ReadBytes(uintptr_t addr, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(addr, size))
        return false;
      __try {
        memcpy(out, reinterpret_cast<const void *>(addr), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BytesEqual(uintptr_t addr, const uint8_t *expected, size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(addr, current.data(), size) &&
             memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t addr, const uint8_t *bytes, size_t size) {
      if (!bytes || !size || !IsValidPtr(addr, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool ok = false;
      __try {
        memcpy(reinterpret_cast<void *>(addr), bytes, size);
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(addr), size, oldProtect, &ignored);
      if (ok)
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(addr), size);
      return ok;
    }

    bool BuildRelPatch(uint8_t opcode, uintptr_t from, uintptr_t to,
                       uint8_t *out, size_t size) {
      if (!out || size < 5)
        return false;
      const int64_t rel = static_cast<int64_t>(to) -
                          static_cast<int64_t>(from + 5);
      if (rel < INT32_MIN ||
          rel > INT32_MAX)
        return false;

      memset(out, 0x90, size);
      out[0] = opcode;
      const int32_t rel32 = static_cast<int32_t>(rel);
      memcpy(out + 1, &rel32, sizeof(rel32));
      return true;
    }

    bool ValidateTargets(uintptr_t exeBase) {
      struct Check {
        uintptr_t offset;
        const uint8_t *bytes;
        size_t size;
        const char *name;
      };
      const Check checks[] = {
          {kMainHookOffset, kMainOriginal, sizeof(kMainOriginal), "main"},
          {kCollectCallOffset, kCollectOriginal, sizeof(kCollectOriginal), "collect"},
          {kRelationsCallOffset, kRelationsOriginal, sizeof(kRelationsOriginal), "relations"},
          {kQuestionFuncOffset, kQuestionSignature, sizeof(kQuestionSignature), "question"},
          {kConsumePrivilegeFuncOffset, kConsumeSignature, sizeof(kConsumeSignature), "consume"},
      };

      for (const auto &check : checks) {
        if (!BytesEqual(exeBase + check.offset, check.bytes, check.size)) {
          AddLog(u8"[포로처분] 적용 거부: %s 지점 원본 바이트 불일치 (+%llX)",
                 check.name,
                 static_cast<unsigned long long>(check.offset));
          return false;
        }
      }
      return true;
    }

    bool BuildAndInstallCave(uintptr_t exeBase) {
      std::vector<uint8_t> blob;
      if (!DecodeCave(blob)) {
        AddLog(u8"[포로처분] 코드케이브 데이터 해석 실패");
        return false;
      }

      for (const auto &reloc : kLocalRelocs) {
        if (!PatchLocalRel32(blob, reloc.fieldOffset, reloc.targetOffset)) {
          AddLog(u8"[포로처분] 코드케이브 내부 상대주소 보정 실패");
          return false;
        }
      }

      if (!PutQword(blob, kGlobalPtrDataOffset, exeBase + kGlobalPtrOffset) ||
          !PutQword(blob, kUiCountDataOffset, exeBase + kUiCountPtrOffset) ||
          !PutQword(blob, kUiLimitDataOffset, exeBase + kUiLimitPtrOffset) ||
          !PutQword(blob, kUiArrayDataOffset, exeBase + kUiArrayPtrOffset) ||
          !PutQword(blob, kQuestionFuncDataOffset, exeBase + kQuestionFuncOffset) ||
          !PutQword(blob, kConsumeFuncDataOffset, exeBase + kConsumePrivilegeFuncOffset) ||
          !PutQword(blob, kAllowDataOffset, exeBase + kAllowOffset) ||
          !PutQword(blob, kDenyDataOffset, exeBase + kDenyOffset) ||
          !PutQword(blob, kCollectTargetDataOffset, exeBase + kCollectTargetOffset) ||
          !PutQword(blob, kRelationsTargetDataOffset, exeBase + kRelationsTargetOffset) ||
          !PutQword(blob, kConsumeFlagDataOffset,
                    reinterpret_cast<uintptr_t>(&bGovernorPrisonerConsumePrivilege))) {
        AddLog(u8"[포로처분] 코드케이브 주소 데이터 설정 실패");
        return false;
      }

      const uintptr_t mainHook = exeBase + kMainHookOffset;
      g_cave = AllocNear(mainHook, 0x1000);
      if (!g_cave) {
        AddLog(u8"[포로처분] 코드케이브 메모리 할당 실패");
        return false;
      }

      memcpy(reinterpret_cast<void *>(g_cave), blob.data(), blob.size());
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(g_cave), blob.size());

      if (!BuildRelPatch(0xE9, mainHook, g_cave,
                         g_mainPatched, sizeof(g_mainPatched)) ||
          !BuildRelPatch(0xE8, exeBase + kCollectCallOffset,
                         g_cave + kCollectWrapperOffset,
                         g_collectPatched, sizeof(g_collectPatched)) ||
          !BuildRelPatch(0xE8, exeBase + kRelationsCallOffset,
                         g_cave + kRelationsWrapperOffset,
                         g_relationsPatched, sizeof(g_relationsPatched))) {
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
        AddLog(u8"[포로처분] 후킹 상대주소 생성 실패");
        return false;
      }

      const uintptr_t collectHook = exeBase + kCollectCallOffset;
      const uintptr_t relationsHook = exeBase + kRelationsCallOffset;

      if (!WriteBytes(mainHook, g_mainPatched, sizeof(g_mainPatched)) ||
          !WriteBytes(collectHook, g_collectPatched, sizeof(g_collectPatched)) ||
          !WriteBytes(relationsHook, g_relationsPatched, sizeof(g_relationsPatched))) {
        WriteBytes(mainHook, kMainOriginal, sizeof(kMainOriginal));
        WriteBytes(collectHook, kCollectOriginal, sizeof(kCollectOriginal));
        WriteBytes(relationsHook, kRelationsOriginal, sizeof(kRelationsOriginal));
        VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
        g_cave = 0;
        AddLog(u8"[포로처분] 후킹 적용 실패 - 원본 복구 시도");
        return false;
      }

      return true;
    }
  } // namespace

  bool IsGovernorPrisonerDisposalApplied() {
    return g_applied;
  }

  bool SetGovernorPrisonerDisposal(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[포로처분] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t mainHook = exeBase + kMainHookOffset;
    const uintptr_t collectHook = exeBase + kCollectCallOffset;
    const uintptr_t relationsHook = exeBase + kRelationsCallOffset;

    if (enable) {
      if (g_applied)
        return true;

      if (!ValidateTargets(exeBase))
        return false;

      if (!BuildAndInstallCave(exeBase))
        return false;

      g_applied = true;
      AddLog(u8"[포로처분] 도독 포로 직접 처분 활성화 (%s)",
             bGovernorPrisonerConsumePrivilege ? u8"특권 1개 소비" : u8"특권 소비 없음");
      return true;
    }

    if (!g_applied)
      return true;

    if (!BytesEqual(mainHook, g_mainPatched, sizeof(g_mainPatched)) ||
        !BytesEqual(collectHook, g_collectPatched, sizeof(g_collectPatched)) ||
        !BytesEqual(relationsHook, g_relationsPatched, sizeof(g_relationsPatched))) {
      AddLog(u8"[포로처분] 해제 거부: 후킹 지점에 외부 변경이 감지되었습니다.");
      return false;
    }

    const bool restoredMain =
        WriteBytes(mainHook, kMainOriginal, sizeof(kMainOriginal));
    const bool restoredCollect =
        WriteBytes(collectHook, kCollectOriginal, sizeof(kCollectOriginal));
    const bool restoredRelations =
        WriteBytes(relationsHook, kRelationsOriginal, sizeof(kRelationsOriginal));

    if (!restoredMain || !restoredCollect || !restoredRelations) {
      AddLog(u8"[포로처분] 원본 복구 실패");
      return false;
    }

    if (g_cave) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
    }

    g_applied = false;
    AddLog(u8"[포로처분] 도독 포로 직접 처분 해제");
    return true;
  }
} // namespace DX11Base
