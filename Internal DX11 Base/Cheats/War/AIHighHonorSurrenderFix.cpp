#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "AIHighHonorSurrenderFix.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kHookOffset = 0x0193CA30;
    constexpr uintptr_t kResolveForceOffset = 0x017121D0;
    constexpr uintptr_t kRandomOffset = 0x00125AA0;
    constexpr uintptr_t kResumeOffset = 0x0193CA40;

    constexpr size_t kHookSize = 16;
    constexpr size_t kCaveSize = 0x1000;
    constexpr size_t kTemplateSize = 0x15B;

    constexpr uint64_t kMarkerResolveForce = 0x1111111111111111ull;
    constexpr uint64_t kMarkerRandom = 0x2222222222222222ull;
    constexpr uint64_t kMarkerResume = 0x3333333333333333ull;

    const uint8_t kOriginal[kHookSize] = {
        0x48, 0x89, 0x7D, 0xC0,
        0x48, 0x89, 0x5D, 0xC8,
        0x48, 0x89, 0x75, 0xE8,
        0x48, 0x8B, 0x46, 0x20};

    // SAN8RPK_AI_V2.0.exe ai_profile SURRENDER_TEMPLATE.
    // 의리 11~15일 때 내부 threshold table(30/20/10/5/1)을 사용해
    // 항복권고 수락 결과를 거부 쪽으로 보정합니다.
    const char kTemplateHex[] =
        "80f9010f852f0100004881ecd0000000488944242048894c242848895424304c894424384c894c24404c895424484c895c2450f30f7f442460f30f7f4c2470f30f7f942480000000f30f7f9c2490000000f30f7fa424a0000000f30f7fac24b00000004885f67476488b4e204885c9746d48b81111111111111111ffd04885c0745c488b80c00000004885c074500fb6405e83e00f83f80b724483e80b488d15b20000000fb60402898424c0000000b96400000031d24531c048b82222222222222222ffd03b8424c00000007210c644242802c64424301966c745a00219488b442420488b4c2428488b5424304c8b4424384c8b4c24404c8b5424484c8b5c2450f30f6f442460f30f6f4c2470f30f6f942480000000f30f6f9c2490000000f30f6fa424a0000000f30f6fac24b00000004881c4d000000048897dc048895dc8488975e8488b4620ff250000000033333333333333331e140a0501";

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

    bool DecodeTemplate(std::vector<uint8_t>& out) {
      out.clear();
      const size_t len = std::strlen(kTemplateHex);
      if ((len & 1) != 0)
        return false;

      out.reserve(len / 2);
      for (size_t i = 0; i < len; i += 2) {
        const int hi = HexNibble(kTemplateHex[i]);
        const int lo = HexNibble(kTemplateHex[i + 1]);
        if (hi < 0 || lo < 0)
          return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
      }
      return out.size() == kTemplateSize;
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

    bool ReplaceMarker(std::vector<uint8_t>& blob,
                       uint64_t marker,
                       uintptr_t value) {
      const uint64_t replacement = static_cast<uint64_t>(value);

      for (size_t i = 0; i + sizeof(uint64_t) <= blob.size(); ++i) {
        uint64_t current = 0;
        std::memcpy(&current, blob.data() + i, sizeof(current));
        if (current != marker)
          continue;

        std::memcpy(blob.data() + i,
                    &replacement,
                    sizeof(replacement));
        return true;
      }
      return false;
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

    bool BuildPayload(uintptr_t exeBase,
                      std::vector<uint8_t>& payload) {
      if (!DecodeTemplate(payload))
        return false;

      return ReplaceMarker(payload,
                           kMarkerResolveForce,
                           exeBase + kResolveForceOffset) &&
             ReplaceMarker(payload,
                           kMarkerRandom,
                           exeBase + kRandomOffset) &&
             ReplaceMarker(payload,
                           kMarkerResume,
                           exeBase + kResumeOffset);
    }
  } // namespace

  bool IsAIHighHonorSurrenderFixApplied() {
    return g_applied;
  }

  bool SetAIHighHonorSurrenderFix(bool enable) {
    if (enable == g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[AI항복권고] 적용 보류: SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hookAddr = exeBase + kHookOffset;

    if (enable) {
      if (!BytesEqual(hookAddr, kOriginal, sizeof(kOriginal))) {
        AddLog(u8"[AI항복권고] 적용 보류: 원본 바이트 불일치 (+%llX)",
               static_cast<unsigned long long>(kHookOffset));
        return false;
      }

      std::vector<uint8_t> payload;
      if (!BuildPayload(exeBase, payload)) {
        AddLog(u8"[AI항복권고] V2.0 payload 구성 실패");
        return false;
      }

      if (!g_cave) {
        g_cave = reinterpret_cast<uintptr_t>(
            VirtualAlloc(nullptr,
                         kCaveSize,
                         MEM_COMMIT | MEM_RESERVE,
                         PAGE_EXECUTE_READWRITE));
        if (!g_cave) {
          AddLog(u8"[AI항복권고] 코드케이브 할당 실패");
          return false;
        }
      }

      std::memset(reinterpret_cast<void*>(g_cave), 0, kCaveSize);
      std::memcpy(reinterpret_cast<void*>(g_cave),
                  payload.data(),
                  payload.size());
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(g_cave),
                            payload.size());

      BuildAbsoluteJump(g_cave, g_hookPatched);

      if (!WriteBytes(hookAddr,
                      g_hookPatched,
                      sizeof(g_hookPatched))) {
        AddLog(u8"[AI항복권고] hook 적용 실패");
        return false;
      }

      if (!BytesEqual(hookAddr,
                      g_hookPatched,
                      sizeof(g_hookPatched))) {
        WriteBytes(hookAddr, kOriginal, sizeof(kOriginal));
        AddLog(u8"[AI항복권고] hook 검증 실패 - 원본 복구");
        return false;
      }

      g_applied = true;
      AddLog(u8"[AI항복권고] 고의리 군주 항복 억제 활성화 (+%llX)",
             static_cast<unsigned long long>(kHookOffset));
      return true;
    }

    if (!BytesEqual(hookAddr,
                    g_hookPatched,
                    sizeof(g_hookPatched))) {
      AddLog(u8"[AI항복권고] 해제 보류: hook 지점에 외부 변경 감지");
      return false;
    }

    if (!WriteBytes(hookAddr, kOriginal, sizeof(kOriginal))) {
      AddLog(u8"[AI항복권고] 원본 복구 실패");
      return false;
    }

    // 실행 중인 cave가 있을 수 있으므로 메모리는 프로세스 종료까지 유지합니다.
    g_applied = false;
    AddLog(u8"[AI항복권고] 고의리 군주 항복 억제 비활성화");
    return true;
  }

} // namespace DX11Base
