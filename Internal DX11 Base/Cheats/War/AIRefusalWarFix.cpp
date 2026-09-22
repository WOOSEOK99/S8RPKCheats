#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "AIRefusalWarFix.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kAttackHookOffset = 0x0144D248;
    constexpr uintptr_t kAttackResumeOffset = 0x0144D27A;

    constexpr uintptr_t kPreHookOffset = 0x01451EAD;
    constexpr uintptr_t kPreResumeOffset = 0x01451EBD;

    constexpr uintptr_t kPostHookOffset = 0x01451ECD;
    constexpr uintptr_t kPostResumeOffset = 0x01451EDD;

    constexpr uintptr_t kRelationOffset = 0x017AF090;
    constexpr uintptr_t kScoreOffset = 0x017A5130;
    constexpr uintptr_t kManagerRootOffset = 0x02E98BC8;
    constexpr uintptr_t kResolveForceOffset = 0x017121D0;
    constexpr uintptr_t kRandomOffset = 0x00125AA0;
    constexpr uintptr_t kTransitionOffset = 0x0144CA10;

    constexpr size_t kHookSize = 16;
    constexpr size_t kAttackOffset = 0x000;
    constexpr size_t kPreOffset = 0x400;
    constexpr size_t kPostOffset = 0x600;
    constexpr size_t kSafeSkipOffset = 0x800;
    constexpr size_t kStatsOffset = 0x1000;
    constexpr size_t kCaveSize = 0x3000;

    constexpr size_t kAttackSize = 399;
    constexpr size_t kPreSize = 271;
    constexpr size_t kPostSize = 334;

    constexpr uint64_t kMarkerStats = 0x1111111111111111ull;
    constexpr uint64_t kMarkerRelation = 0x2222222222222222ull;
    constexpr uint64_t kMarkerScore = 0x3333333333333333ull;
    constexpr uint64_t kMarkerAttackResume = 0x4444444444444444ull;
    constexpr uint64_t kMarkerManager = 0x5555555555555555ull;
    constexpr uint64_t kMarkerResolveForce = 0x6666666666666666ull;
    constexpr uint64_t kMarkerPreResume = 0x7777777777777777ull;
    constexpr uint64_t kMarkerPostResume = 0x8888888888888888ull;
    constexpr uint64_t kMarkerSafeSkip = 0x9999999999999999ull;
    constexpr uint64_t kMarkerRandom = 0xAAAAAAAAAAAAAAAAull;
    constexpr uint64_t kMarkerTransition = 0xBBBBBBBBBBBBBBBBull;

    const uint8_t kAttackOriginal[kHookSize] = {
        0x49, 0x3B, 0x4D, 0x30,
        0x74, 0x0A,
        0x48, 0x8B, 0x47, 0x18,
        0x49, 0x3B, 0x45, 0x38,
        0x75, 0x22};

    const uint8_t kPreOriginal[kHookSize] = {
        0xC6, 0x44, 0x24, 0x20, 0x00,
        0x44, 0x0F, 0xB6, 0xCD,
        0x4D, 0x8B, 0x87, 0x98, 0x00, 0x00, 0x00};

    const uint8_t kPostOriginal[kHookSize] = {
        0x85, 0xED,
        0x74, 0x08,
        0x66, 0x41, 0x09, 0xBF, 0x20, 0x01, 0x00, 0x00,
        0x49, 0x8B, 0x5E, 0x10};

    const char kAttackHex[] =
        "49bb1111111111111111f049ff034885c90f846c010000498b55104885d20f845f0100004839d10f84470100004883ec2048b82222222222222222ffd04883c42084c00f852b010000488b8790000000488b4010493b45307419488b4718493b4538740f49bb1111111111111111f049ff43084889fa4889f14883ec2048b83333333333333333ffd04883c4204d8b45104c8b8f900000004d8b491049ba1111111111111111410fb640083d960000000f8784000000c1e0054d8d9402000100004d390275744d394a08756e48b85555555555555555488b0049394210755b488b88e00000004885c974064c39411874490fb790d07200006bd20c0fb680d272000001c2413b52187230413b521c732ab89a99993f660f6ec80f2fc1721cb80000a040660f6ec8f30f59c149bb1111111111111111f049ff4350488b8790000000488b4010493b4530750db8cdcc8c3f660f6ec8f30f59c10f2fc676260f28f04989ff49bb1111111111111111f049ff4310eb0f49bb1111111111111111f049ff431848b84444444444444444ffe0";

    const char kPreHex[] =
        "4883ec6048894c242048895424284c894424304c894c243848894424404c89f948b86666666666666666ffd04989c14d8b46104d85c00f84990000004d85c90f849000000049ba1111111111111111410fb640083d96000000777ac1e0054d8d9402000100004d3902756a4d394a08756448b85555555555555555488b00493942107551488b88e00000004885c974064c394118743f0fb790d07200006bd20c0fb680d272000001c2413b52187226413b521c732049bb1111111111111111f049ff43484883c46049bb999999999999999941ffe3488b4c2420488b5424284c8b4424304c8b4c2438488b4424404883c460c644242000440fb6cd4d8b879800000049bb777777777777777741ffe3";

    const char kPostHex[] =
        "4883ec604c89f948b86666666666666666ffd04989c14d8b46104d85c00f840a0100004d85c90f84010100004d39c80f84f800000048b85555555555555555488b004885c00f84e2000000488b88e00000004885c9740a4c3941180f84cc0000004c894424204c894c242848894424304c89c94c89c248b82222222222222222ffd084c00f85a300000049bb1111111111111111f049ff4338b96400000031d24531c048b8aaaaaaaaaaaaaaaaffd083f85f73794c8b4424204c8b4c2428410fb640083d960000007763c1e00549ba11111111111111114d8d940200010000488b4424300fb790d07200006bd20c0fb688d272000001ca4d894a08498942104189521883c2034189521c4d890249bb1111111111111111f049ff434031c94c89f248b8bbbbbbbbbbbbbbbbffd04883c46085ed7408664109bf20010000498b5e1049bb888888888888888841ffe3";

    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_attackPatched[kHookSize]{};
    uint8_t g_prePatched[kHookSize]{};
    uint8_t g_postPatched[kHookSize]{};

    int HexNibble(char c) {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      return -1;
    }

    bool DecodeHex(const char *hex,
                   size_t expectedSize,
                   std::vector<uint8_t>& out) {
      out.clear();
      if (!hex)
        return false;

      const size_t len = std::strlen(hex);
      if ((len & 1) != 0)
        return false;

      out.reserve(len / 2);
      for (size_t i = 0; i < len; i += 2) {
        const int hi = HexNibble(hex[i]);
        const int lo = HexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
          return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
      }

      return out.size() == expectedSize;
    }

    size_t ReplaceAll(std::vector<uint8_t>& blob,
                      uint64_t marker,
                      uintptr_t value) {
      const uint64_t replacement = static_cast<uint64_t>(value);
      size_t count = 0;

      for (size_t i = 0; i + sizeof(uint64_t) <= blob.size();) {
        uint64_t current = 0;
        std::memcpy(&current, blob.data() + i, sizeof(current));
        if (current == marker) {
          std::memcpy(blob.data() + i,
                      &replacement,
                      sizeof(replacement));
          ++count;
          i += sizeof(uint64_t);
        } else {
          ++i;
        }
      }
      return count;
    }

    bool ReadBytes(uintptr_t addr, uint8_t* out, size_t size) {
      if (!out || !size || !IsValidPtr(addr, size))
        return false;

      __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(out, 0, size);
        return false;
      }
    }

    bool BytesEqual(uintptr_t addr,
                    const uint8_t* expected,
                    size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(addr, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t addr,
                    const uint8_t* bytes,
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
        std::memcpy(reinterpret_cast<void*>(addr), bytes, size);
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

    void BuildAbsoluteJump(uintptr_t destination,
                           uint8_t out[kHookSize]) {
      std::memset(out, 0x90, kHookSize);
      out[0] = 0xFF;
      out[1] = 0x25;
      out[2] = 0x00;
      out[3] = 0x00;
      out[4] = 0x00;
      out[5] = 0x00;

      const uint64_t dst = static_cast<uint64_t>(destination);
      std::memcpy(out + 6, &dst, sizeof(dst));
    }

    bool BuildPayloads(uintptr_t exeBase,
                       uintptr_t cave,
                       std::vector<uint8_t>& attack,
                       std::vector<uint8_t>& pre,
                       std::vector<uint8_t>& post) {
      if (!DecodeHex(kAttackHex, kAttackSize, attack) ||
          !DecodeHex(kPreHex, kPreSize, pre) ||
          !DecodeHex(kPostHex, kPostSize, post)) {
        return false;
      }

      const uintptr_t stats = cave + kStatsOffset;
      const uintptr_t safeSkip = cave + kSafeSkipOffset;

      bool ok = true;

      ok &= ReplaceAll(attack, kMarkerStats, stats) > 0;
      ok &= ReplaceAll(attack, kMarkerRelation,
                       exeBase + kRelationOffset) > 0;
      ok &= ReplaceAll(attack, kMarkerScore,
                       exeBase + kScoreOffset) > 0;
      ok &= ReplaceAll(attack, kMarkerAttackResume,
                       exeBase + kAttackResumeOffset) > 0;
      ok &= ReplaceAll(attack, kMarkerManager,
                       exeBase + kManagerRootOffset) > 0;

      ok &= ReplaceAll(pre, kMarkerStats, stats) > 0;
      ok &= ReplaceAll(pre, kMarkerManager,
                       exeBase + kManagerRootOffset) > 0;
      ok &= ReplaceAll(pre, kMarkerResolveForce,
                       exeBase + kResolveForceOffset) > 0;
      ok &= ReplaceAll(pre, kMarkerPreResume,
                       exeBase + kPreResumeOffset) > 0;
      ok &= ReplaceAll(pre, kMarkerSafeSkip, safeSkip) > 0;

      ok &= ReplaceAll(post, kMarkerStats, stats) > 0;
      ok &= ReplaceAll(post, kMarkerRelation,
                       exeBase + kRelationOffset) > 0;
      ok &= ReplaceAll(post, kMarkerManager,
                       exeBase + kManagerRootOffset) > 0;
      ok &= ReplaceAll(post, kMarkerResolveForce,
                       exeBase + kResolveForceOffset) > 0;
      ok &= ReplaceAll(post, kMarkerRandom,
                       exeBase + kRandomOffset) > 0;
      ok &= ReplaceAll(post, kMarkerTransition,
                       exeBase + kTransitionOffset) > 0;
      ok &= ReplaceAll(post, kMarkerPostResume,
                       exeBase + kPostResumeOffset) > 0;

      return ok;
    }

    void BuildSafeSkipStub(uintptr_t destination,
                           uint8_t out[18]) {
      // Viewer 원형은 +1451ED9로 점프하지만 그 주소는 POST hook 내부입니다.
      // 원래 +1451ED9 명령을 cave에서 직접 재현한 뒤 POST hook 뒤로 복귀합니다.
      out[0] = 0x49;
      out[1] = 0x8B;
      out[2] = 0x5E;
      out[3] = 0x10; // mov rbx,[r14+10h]

      out[4] = 0xFF;
      out[5] = 0x25;
      out[6] = 0x00;
      out[7] = 0x00;
      out[8] = 0x00;
      out[9] = 0x00;

      const uint64_t dst = static_cast<uint64_t>(destination);
      std::memcpy(out + 10, &dst, sizeof(dst));
    }

    bool RestoreHooks(uintptr_t exeBase) {
      struct RestoreSpec {
        uintptr_t addr;
        const uint8_t* original;
        const uint8_t* patched;
      };

      const RestoreSpec specs[] = {
          {exeBase + kAttackHookOffset, kAttackOriginal, g_attackPatched},
          {exeBase + kPreHookOffset, kPreOriginal, g_prePatched},
          {exeBase + kPostHookOffset, kPostOriginal, g_postPatched},
      };

      for (const auto& spec : specs) {
        if (!BytesEqual(spec.addr, spec.patched, kHookSize))
          return false;
      }

      bool restored[_countof(specs)]{};
      for (size_t i = 0; i < _countof(specs); ++i) {
        if (!WriteBytes(specs[i].addr, specs[i].original, kHookSize)) {
          for (size_t j = 0; j < i; ++j) {
            if (restored[j])
              WriteBytes(specs[j].addr, specs[j].patched, kHookSize);
          }
          return false;
        }
        restored[i] = true;
      }
      return true;
    }
  } // namespace

  bool IsAIRefusalWarFixApplied() {
    return g_applied;
  }

  bool SetAIRefusalWarFix(bool enable) {
    if (enable == g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[AI항복권고] 공격전환 적용 보류: SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    if (!enable) {
      if (!RestoreHooks(exeBase)) {
        AddLog(u8"[AI항복권고] 공격전환 해제 실패: hook 상태 불일치 또는 원본 복구 실패");
        return false;
      }

      g_applied = false;
      AddLog(u8"[AI항복권고] 실패 후 공격 우선/재권고 차단 비활성화");
      return true;
    }

    const uintptr_t attackAddr = exeBase + kAttackHookOffset;
    const uintptr_t preAddr = exeBase + kPreHookOffset;
    const uintptr_t postAddr = exeBase + kPostHookOffset;

    // 세 지점을 모두 먼저 검증합니다. 하나라도 다르면 아무 것도 쓰지 않습니다.
    if (!BytesEqual(attackAddr, kAttackOriginal, kHookSize) ||
        !BytesEqual(preAddr, kPreOriginal, kHookSize) ||
        !BytesEqual(postAddr, kPostOriginal, kHookSize)) {
      AddLog(u8"[AI항복권고] 공격전환 적용 보류: hook 원본 바이트 불일치");
      return false;
    }

    if (!g_cave) {
      g_cave = reinterpret_cast<uintptr_t>(
          VirtualAlloc(nullptr,
                       kCaveSize,
                       MEM_COMMIT | MEM_RESERVE,
                       PAGE_EXECUTE_READWRITE));
      if (!g_cave) {
        AddLog(u8"[AI항복권고] 공격전환 코드케이브 할당 실패");
        return false;
      }
    }

    std::vector<uint8_t> attack;
    std::vector<uint8_t> pre;
    std::vector<uint8_t> post;
    if (!BuildPayloads(exeBase, g_cave, attack, pre, post)) {
      AddLog(u8"[AI항복권고] 공격전환 V2.0 payload 구성 실패");
      return false;
    }

    std::memset(reinterpret_cast<void*>(g_cave), 0, kCaveSize);

    std::memcpy(reinterpret_cast<void*>(g_cave + kAttackOffset),
                attack.data(),
                attack.size());
    std::memcpy(reinterpret_cast<void*>(g_cave + kPreOffset),
                pre.data(),
                pre.size());
    std::memcpy(reinterpret_cast<void*>(g_cave + kPostOffset),
                post.data(),
                post.size());

    uint8_t safeSkip[18]{};
    BuildSafeSkipStub(exeBase + kPostResumeOffset, safeSkip);
    std::memcpy(reinterpret_cast<void*>(g_cave + kSafeSkipOffset),
                safeSkip,
                sizeof(safeSkip));

    // stats + refusal record table는 매 활성화 시 초기화합니다.
    std::memset(reinterpret_cast<void*>(g_cave + kStatsOffset),
                0,
                kCaveSize - kStatsOffset);

    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_cave),
                          kCaveSize);

    BuildAbsoluteJump(g_cave + kAttackOffset, g_attackPatched);
    BuildAbsoluteJump(g_cave + kPreOffset, g_prePatched);
    BuildAbsoluteJump(g_cave + kPostOffset, g_postPatched);

    struct PatchSpec {
      uintptr_t addr;
      const uint8_t* original;
      const uint8_t* patched;
    };

    const PatchSpec specs[] = {
        {attackAddr, kAttackOriginal, g_attackPatched},
        {preAddr, kPreOriginal, g_prePatched},
        {postAddr, kPostOriginal, g_postPatched},
    };

    bool changed[_countof(specs)]{};
    for (size_t i = 0; i < _countof(specs); ++i) {
      if (!WriteBytes(specs[i].addr, specs[i].patched, kHookSize)) {
        for (size_t j = 0; j < i; ++j) {
          if (changed[j])
            WriteBytes(specs[j].addr, specs[j].original, kHookSize);
        }
        AddLog(u8"[AI항복권고] 공격전환 hook 적용 실패 (변경분 롤백)");
        return false;
      }
      changed[i] = true;
    }

    for (const auto& spec : specs) {
      if (!BytesEqual(spec.addr, spec.patched, kHookSize)) {
        for (const auto& rollback : specs) {
          if (BytesEqual(rollback.addr, rollback.patched, kHookSize))
            WriteBytes(rollback.addr, rollback.original, kHookSize);
        }
        AddLog(u8"[AI항복권고] 공격전환 hook 검증 실패 - 원본 복구");
        return false;
      }
    }

    g_applied = true;
    AddLog(u8"[AI항복권고] 실패 후 3개월 공격 우선/재권고 차단 활성화");
    AddLog(u8"[AI항복권고] 공격 후보 점수 1.2 이상에서 최대 x5 보정");
    return true;
  }

} // namespace DX11Base
