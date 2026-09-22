#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "AIExecutionConditionChange.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kPrisonHookOffset = 0x01E52A8D;
    constexpr uintptr_t kPrisonResumeOffset = 0x01E528A1;
    constexpr uintptr_t kClassifierHookOffset = 0x01E54190;
    constexpr uintptr_t kClassifierResumeOffset = 0x01E541A8;

    constexpr uintptr_t kManagerRootOffset = 0x02E98BC8;
    constexpr uintptr_t kRelationLookupOffset = 0x01712670;
    constexpr uintptr_t kRelationClassifyOffset = 0x017B0AB0;
    constexpr uintptr_t kPairCheckOffset = 0x017B4570;
    constexpr uintptr_t kPairExclusionOffset = 0x017A8270;

    constexpr size_t kHookSize = 16;
    constexpr size_t kPrisonPayloadOffset = 0x000;
    constexpr size_t kClassifierPayloadOffset = 0x800;
    constexpr size_t kStatsOffset = 0x1000;
    constexpr size_t kCaveSize = 0x2000;

    constexpr size_t kPrisonPayloadSize = 731;
    constexpr size_t kClassifierPayloadSize = 161;

    constexpr uint64_t kMarkerStats = 0x1111111111111111ull;
    constexpr uint64_t kMarkerPrisonResume = 0x2222222222222222ull;
    constexpr uint64_t kMarkerManager = 0x3333333333333333ull;
    constexpr uint64_t kMarkerRelationLookup = 0x4444444444444444ull;
    constexpr uint64_t kMarkerRelationClassify = 0x5555555555555555ull;
    constexpr uint64_t kMarkerPairCheck = 0x6666666666666666ull;
    constexpr uint64_t kMarkerPairExclusion = 0x7777777777777777ull;
    constexpr uint64_t kMarkerClassifierResume = 0x8888888888888888ull;

    const uint8_t kPrisonOriginal[kHookSize] = {
        0x3B, 0xCD,
        0x48, 0x8B, 0x6C, 0x24, 0x60,
        0x0F, 0x9C, 0xC0,
        0xE9, 0x05, 0xFE, 0xFF, 0xFF,
        0xCC};

    const uint8_t kClassifierOriginal[kHookSize] = {
        0xC7, 0x44, 0x24, 0x44, 0x01, 0x01, 0x00, 0x00,
        0x4C, 0x8D, 0x44, 0x24, 0x44,
        0x48, 0x8B, 0xD3};

    // SAN8RPK_AI_V2.0.exe ai_profile HATRED_PRISON.
    // 최종 처형 임계값은 상성 거리, 처분 주체의 의리, 포로의 군주 여부,
    // 관계 helper 결과와 양 세력의 특수 관계 상태까지 반영합니다.
    const char kPrisonPayloadHex[] =
        "49bb1111111111111111f049ff43200fb6475d0fb6565d29d09931d029d0ba9600000029c239d00f4fc241b8640000004129c00fb6565e83e20f488b47104885c07418807808017424488b47184885c074094839b8c000000074124531c983fa0b7d056bd203eb176bd205eb124183e81941b90100000083fa0b7c036bd2064101d0807e300175044183c00aba640000004139d0440f4fc231d24139d0440f4cc24183f864751c4585c974170fb6565e83e20f4101d04183e810807e3001750341ffc04883ec40894c2420448944242844894c242c48b83333333333333333488b084881c1c87200000fb75608440fb7470848b84444444444444444ffd00fb6c848b85555555555555555ffd08b4c2420448b442428448b4c242c4883c4404589ca84c075690fb6565e83e20f83fa0b7c5a4585c9752041b94b00000083fa0f740d83fa0e752e41b955000000eb2641b95a000000eb1e41b93200000083fa0f740d83fa0e750e41b942000000eb0641b94b0000004489c0410fafc131d241b96400000041f7f14189c0eb034531c04585d20f84110100000fb6565e83e20f83fa0f0f84010100004585c00f84f80000004883ec60894c24204489442424488b4f1848894c24304885c90f84cc000000488b01ff504884c00f84be000000488b4e1848894c24384885c90f84ac000000483b4c24300f84a1000000488b01ff504884c00f8493000000488b442438488b54243080b8c201000000741f483990d00100007516488b88c80100004885c9740a488b01ff504884c07532488b442430488b54243880b8c201000000744e483990d00100007545488b88c80100004885c97439488b01ff504884c0742f8b4424240fb6565e83e20f83fa0e741983fa0b72106bc05a31d241b96400000041f7f1eb06d1e8eb02ffc8894424248b4c2420448b4424244883c46049bb11111111111111114439c10f9dc084c07407f049ff4330eb05f049ff4328488b6c246049bb222222222222222241ffe3";

    // SAN8RPK_AI_V2.0.exe ai_profile HATRED_CLASSIFIER.
    const char kClassifierPayloadHex[] =
        "c7442444010100004883ec30c7442420010100004c8d4424204889da4c89f148b86666666666666666ffd084c07461410fb6565e83e20f83fa0b7c544c89f14889da48b87777777777777777ffd084c0753e48b83333333333333333488b084881c1c8720000410fb75608440fb7430848b84444444444444444ffd00fb6c848b85555555555555555ffd084c00f95c04883c43049bb888888888888888841ffe3";

    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_prisonPatched[kHookSize]{};
    uint8_t g_classifierPatched[kHookSize]{};

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
                   std::vector<uint8_t> &out) {
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

    size_t ReplaceAll(std::vector<uint8_t> &blob,
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
                       std::vector<uint8_t> &prison,
                       std::vector<uint8_t> &classifier) {
      if (!DecodeHex(kPrisonPayloadHex, kPrisonPayloadSize, prison) ||
          !DecodeHex(kClassifierPayloadHex,
                     kClassifierPayloadSize,
                     classifier)) {
        return false;
      }

      const uintptr_t stats = cave + kStatsOffset;
      bool ok = true;

      ok &= ReplaceAll(prison, kMarkerStats, stats) > 0;
      ok &= ReplaceAll(prison,
                       kMarkerPrisonResume,
                       exeBase + kPrisonResumeOffset) > 0;
      ok &= ReplaceAll(prison,
                       kMarkerManager,
                       exeBase + kManagerRootOffset) > 0;
      ok &= ReplaceAll(prison,
                       kMarkerRelationLookup,
                       exeBase + kRelationLookupOffset) > 0;
      ok &= ReplaceAll(prison,
                       kMarkerRelationClassify,
                       exeBase + kRelationClassifyOffset) > 0;

      ok &= ReplaceAll(classifier,
                       kMarkerManager,
                       exeBase + kManagerRootOffset) > 0;
      ok &= ReplaceAll(classifier,
                       kMarkerRelationLookup,
                       exeBase + kRelationLookupOffset) > 0;
      ok &= ReplaceAll(classifier,
                       kMarkerRelationClassify,
                       exeBase + kRelationClassifyOffset) > 0;
      ok &= ReplaceAll(classifier,
                       kMarkerPairCheck,
                       exeBase + kPairCheckOffset) > 0;
      ok &= ReplaceAll(classifier,
                       kMarkerPairExclusion,
                       exeBase + kPairExclusionOffset) > 0;
      ok &= ReplaceAll(classifier,
                       kMarkerClassifierResume,
                       exeBase + kClassifierResumeOffset) > 0;

      return ok;
    }

    bool RestoreHooks(uintptr_t exeBase) {
      struct HookSpec {
        uintptr_t address;
        const uint8_t *original;
        const uint8_t *patched;
      };

      const HookSpec hooks[] = {
          {exeBase + kPrisonHookOffset,
           kPrisonOriginal,
           g_prisonPatched},
          {exeBase + kClassifierHookOffset,
           kClassifierOriginal,
           g_classifierPatched},
      };

      for (const auto &hook : hooks) {
        if (!BytesEqual(hook.address, hook.patched, kHookSize))
          return false;
      }

      bool restored[_countof(hooks)]{};
      for (size_t i = 0; i < _countof(hooks); ++i) {
        if (!WriteBytes(hooks[i].address,
                        hooks[i].original,
                        kHookSize)) {
          for (size_t j = 0; j < i; ++j) {
            if (restored[j]) {
              WriteBytes(hooks[j].address,
                         hooks[j].patched,
                         kHookSize);
            }
          }
          return false;
        }
        restored[i] = true;
      }

      return true;
    }
  } // namespace

  bool IsAIExecutionConditionChangeApplied() {
    return g_applied;
  }

  bool SetAIExecutionConditionChange(bool enable) {
    if (enable == g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[AI처형] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t prisonHook = exeBase + kPrisonHookOffset;
    const uintptr_t classifierHook = exeBase + kClassifierHookOffset;

    if (!enable) {
      if (!RestoreHooks(exeBase)) {
        AddLog(u8"[AI처형] 해제 실패: hook 상태 불일치 또는 원본 복구 실패");
        return false;
      }

      // 실행 중인 cave가 있을 수 있으므로 메모리는 프로세스 종료까지 유지합니다.
      g_applied = false;
      AddLog(u8"[AI처형] 처형조건 변경 비활성화");
      return true;
    }

    // 두 hook 모두 원본 상태인지 먼저 확인합니다.
    if (!BytesEqual(prisonHook,
                    kPrisonOriginal,
                    kHookSize) ||
        !BytesEqual(classifierHook,
                    kClassifierOriginal,
                    kHookSize)) {
      AddLog(u8"[AI처형] 적용 보류: 원본 바이트 불일치 (+1E52A8D / +1E54190)");
      return false;
    }

    if (!g_cave) {
      g_cave = reinterpret_cast<uintptr_t>(
          VirtualAlloc(nullptr,
                       kCaveSize,
                       MEM_COMMIT | MEM_RESERVE,
                       PAGE_EXECUTE_READWRITE));
      if (!g_cave) {
        AddLog(u8"[AI처형] 코드케이브 할당 실패");
        return false;
      }
    }

    std::vector<uint8_t> prison;
    std::vector<uint8_t> classifier;
    if (!BuildPayloads(exeBase,
                       g_cave,
                       prison,
                       classifier)) {
      AddLog(u8"[AI처형] V2.0 payload 구성 실패");
      return false;
    }

    std::memset(reinterpret_cast<void *>(g_cave),
                0,
                kCaveSize);
    std::memcpy(reinterpret_cast<void *>(g_cave + kPrisonPayloadOffset),
                prison.data(),
                prison.size());
    std::memcpy(reinterpret_cast<void *>(g_cave + kClassifierPayloadOffset),
                classifier.data(),
                classifier.size());

    // Viewer의 stats 영역은 기능 상태 확인 외에는 사용하지 않지만
    // payload가 카운터를 증가시키므로 별도 공간을 확보해 0으로 초기화합니다.
    std::memset(reinterpret_cast<void *>(g_cave + kStatsOffset),
                0,
                kCaveSize - kStatsOffset);

    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_cave),
                          kCaveSize);

    BuildAbsoluteJump(g_cave + kPrisonPayloadOffset,
                      g_prisonPatched);
    BuildAbsoluteJump(g_cave + kClassifierPayloadOffset,
                      g_classifierPatched);

    if (!WriteBytes(prisonHook,
                    g_prisonPatched,
                    kHookSize)) {
      AddLog(u8"[AI처형] 포로 판정 hook 적용 실패");
      return false;
    }

    if (!WriteBytes(classifierHook,
                    g_classifierPatched,
                    kHookSize)) {
      WriteBytes(prisonHook,
                 kPrisonOriginal,
                 kHookSize);
      AddLog(u8"[AI처형] 관계 분류 hook 적용 실패 - 포로 판정 hook 원본 복구");
      return false;
    }

    if (!BytesEqual(prisonHook,
                    g_prisonPatched,
                    kHookSize) ||
        !BytesEqual(classifierHook,
                    g_classifierPatched,
                    kHookSize)) {
      if (BytesEqual(prisonHook,
                     g_prisonPatched,
                     kHookSize)) {
        WriteBytes(prisonHook,
                   kPrisonOriginal,
                   kHookSize);
      }
      if (BytesEqual(classifierHook,
                     g_classifierPatched,
                     kHookSize)) {
        WriteBytes(classifierHook,
                   kClassifierOriginal,
                   kHookSize);
      }
      AddLog(u8"[AI처형] hook 검증 실패 - 원본 복구");
      return false;
    }

    g_applied = true;
    AddLog(u8"[AI처형] 처형조건 변경 활성화 (+1E52A8D, +1E54190)");
    return true;
  }

} // namespace DX11Base
