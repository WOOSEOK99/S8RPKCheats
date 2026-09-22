#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "DeputyCaptureFix.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kHookOffset = 0x01E33056;
    constexpr uintptr_t kResumeOffset = 0x01E32F06;

    constexpr size_t kHookSize = 14;
    constexpr size_t kCaveSize = 0x1000;
    constexpr size_t kPayloadSize = 0x210;
    constexpr size_t kStatsOffset = 0x300;

    constexpr size_t kBaseQwordOffset = 0x1F8;
    constexpr size_t kStatsQwordOffset = 0x200;
    constexpr size_t kResumeQwordOffset = 0x208;

    const uint8_t kOriginal[kHookSize] = {
        0x4D, 0x89, 0xA6, 0x10, 0x03, 0x00, 0x00,
        0xB0, 0x01,
        0xE9, 0xA2, 0xFE, 0xFF, 0xFF};

    const char kPayloadHex[] =
        "4d89a6100300009c50515241504151415241534883ec204c89f14c89e2e84f0000004883c420415b415a415941585a59589db001ff25ce01000031d24885ff742d488d8648856802483b077521488b47104885c074184881c6287d6802483b30750c8a400831d2ffc83c060f96c289d0c3f30f1efa41564989c84155415455575653488b1d6f010000488b2d70010000488b8bc88be9024885c90f844b0100004889de4c89c74989d1e88cffffff85c00f84350100004889de4c89cfe879ffffff85c00f84220100004d39c80f84190100004d3988100300000f850c0100004d8b60184d85e40f84ff000000498b4118488d93988c6802493914240f85ea0000004885c00f84e10000004839100f85d80000004939c40f84cf0000004c8d9908df1e00488db958e01e0041be700000004d8d53b8498b0a4d8b6a084939cd0f828a0000004c89ee31d24829ce4889f049f7f64885d275774881fe00380000776e4c39e973694c394108755d488b41304885c0745480781c00754e48ff45004c8d59104883c1204d8b134d39d0742f4d39d1742a4889de4c89d7e89cfeffff85c0741b4d39621875154983ba1003000000750b4d898a1003000048ff45084983c3084c39d975c0eb234883c170eb924983c2184d39d30f8559ffffff4981c3a80000004c39df0f8545ffffff5b5e5f5d415c415d415ec36690111111111111111122222222222222223333333333333333";

    bool g_applied = false;
    uintptr_t g_cave = 0;
    uint8_t g_hookPatched[kHookSize]{};

    int HexNibble(char c) {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      return -1;
    }

    bool DecodePayload(std::vector<uint8_t>& out) {
      out.clear();
      const size_t len = std::strlen(kPayloadHex);
      if ((len & 1) != 0)
        return false;

      out.reserve(len / 2);
      for (size_t i = 0; i < len; i += 2) {
        const int hi = HexNibble(kPayloadHex[i]);
        const int lo = HexNibble(kPayloadHex[i + 1]);
        if (hi < 0 || lo < 0)
          return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
      }

      return out.size() == kPayloadSize;
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

    bool BytesEqual(uintptr_t addr, const uint8_t* expected, size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytes(addr, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    bool WriteBytes(uintptr_t addr, const uint8_t* bytes, size_t size) {
      if (!bytes || !size || !IsValidPtr(addr, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool ok = false;
      __try {
        std::memcpy(reinterpret_cast<void*>(addr), bytes, size);
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(addr), size,
                     oldProtect, &ignored);

      if (ok) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(addr),
                              size);
      }
      return ok;
    }

    bool PutQword(std::vector<uint8_t>& payload,
                  size_t offset,
                  uintptr_t value) {
      if (offset + sizeof(uint64_t) > payload.size())
        return false;

      const uint64_t v = static_cast<uint64_t>(value);
      std::memcpy(payload.data() + offset, &v, sizeof(v));
      return true;
    }

    bool BuildAbsoluteJump(uintptr_t destination,
                           uint8_t out[kHookSize]) {
      if (!destination || !out)
        return false;

      out[0] = 0xFF;
      out[1] = 0x25;
      out[2] = 0x00;
      out[3] = 0x00;
      out[4] = 0x00;
      out[5] = 0x00;

      const uint64_t dst = static_cast<uint64_t>(destination);
      std::memcpy(out + 6, &dst, sizeof(dst));
      return true;
    }
  } // namespace

  bool IsDeputyCaptureFixApplied() {
    return g_applied;
  }

  bool InstallDeputyCaptureFix() {
    if (g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[포로개선/부장] 적용 보류: SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hookAddr = exeBase + kHookOffset;

    if (!BytesEqual(hookAddr, kOriginal, sizeof(kOriginal))) {
      AddLog(u8"[포로개선/부장] 적용 보류: 원본 바이트 불일치 (+%llX)",
             static_cast<unsigned long long>(kHookOffset));
      return false;
    }

    std::vector<uint8_t> payload;
    if (!DecodePayload(payload)) {
      AddLog(u8"[포로개선/부장] payload 해석 실패");
      return false;
    }

    g_cave = reinterpret_cast<uintptr_t>(
        VirtualAlloc(nullptr,
                     kCaveSize,
                     MEM_COMMIT | MEM_RESERVE,
                     PAGE_EXECUTE_READWRITE));
    if (!g_cave) {
      AddLog(u8"[포로개선/부장] 코드케이브 할당 실패");
      return false;
    }

    std::memset(reinterpret_cast<void*>(g_cave), 0, kCaveSize);

    const uintptr_t statsAddr = g_cave + kStatsOffset;

    if (!PutQword(payload, kBaseQwordOffset, exeBase) ||
        !PutQword(payload, kStatsQwordOffset, statsAddr) ||
        !PutQword(payload, kResumeQwordOffset, exeBase + kResumeOffset)) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      AddLog(u8"[포로개선/부장] payload 주소 보정 실패");
      return false;
    }

    std::memcpy(reinterpret_cast<void*>(g_cave),
                payload.data(),
                payload.size());
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_cave),
                          payload.size());

    if (!BuildAbsoluteJump(g_cave, g_hookPatched)) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      AddLog(u8"[포로개선/부장] hook jump 생성 실패");
      return false;
    }

    if (!WriteBytes(hookAddr, g_hookPatched, sizeof(g_hookPatched))) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      std::memset(g_hookPatched, 0, sizeof(g_hookPatched));
      AddLog(u8"[포로개선/부장] hook 적용 실패");
      return false;
    }

    if (!BytesEqual(hookAddr, g_hookPatched, sizeof(g_hookPatched))) {
      WriteBytes(hookAddr, kOriginal, sizeof(kOriginal));
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      std::memset(g_hookPatched, 0, sizeof(g_hookPatched));
      AddLog(u8"[포로개선/부장] hook 검증 실패 - 원본 복구");
      return false;
    }

    g_applied = true;

    AddLog(u8"[포로개선/부장] 기본 적용 완료 (+%llX)",
           static_cast<unsigned long long>(kHookOffset));
    AddLog(u8"[포로개선/부장] 총대장 포획 성공 시 같은 부대 부장 최대 2명도 포획 처리됩니다.");
    return true;
  }

} // namespace DX11Base
