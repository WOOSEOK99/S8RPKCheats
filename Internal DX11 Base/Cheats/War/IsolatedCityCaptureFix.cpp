#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "IsolatedCityCaptureFix.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kHookOffset = 0x01E57250;
    constexpr uintptr_t kNativeAppendOffset = 0x0001A0C0;

    constexpr size_t kHookSize = 17;
    constexpr size_t kTemplateSize = 0x4F0;
    constexpr size_t kTrampolineOffset = 0x600;
    constexpr size_t kStatsOffset = 0x1000;
    constexpr size_t kCaveSize = 0x3000;

    constexpr size_t kBaseQwordOffset = 0x4D0;
    constexpr size_t kAppendQwordOffset = 0x4D8;
    constexpr size_t kTrampolineQwordOffset = 0x4E0;
    constexpr size_t kStatsQwordOffset = 0x4E8;

    const uint8_t kOriginal[kHookSize] = {
        0x48, 0x89, 0x4C, 0x24, 0x08,
        0x55,
        0x53,
        0x56,
        0x57,
        0x41, 0x54,
        0x41, 0x55,
        0x41, 0x56,
        0x41, 0x57};

    // SAN8RPK_AI_V2.0.exe isolated_profile TEMPLATE.
    // 0x4D0~0x4E8의 4개 qword는 런타임에
    // game base / native append / trampoline / stats 주소로 치환됩니다.
    const char kTemplateHex[] =
        "f30f1efa4157415641554154555756534881ec180100004c8b2db2040000488b1dc304000048894c24284d8ba5c88be9020f297424700f29bc2480000000440f29842490000000440f298c24a0000000440f299424b0000000440f299c24c0000000440f29a424d0000000440f29ac24e0000000440f29b424f0000000440f29bc24000100004d85e40f84720300004885c90f84690300004180bc2430e01e00000f855a0300004d8bb424b0df1e004d8bbc24b8df1e004d8b8c2410df1e004d85f60f8439030000498d8d38966802493b0e0f85290300004d85ff0f8420030000498d85988c68024939070f85100300004d85c90f84070300004939010f85fe0200004d39cf0f84f502000041f68788020000040f85e7020000498b86900000004885c00f84d70200004c3b78100f85cd020000498d46204d8d4650488b104885d2741f483b0a0f85b4020000488b92900000004885d2740a4c3b7a100f849e0200004883c0084939c075d0498b8c24c0de1e004d8b8424c8de1e004939c80f827c0200004d89c241bb7000000031d24929ca4c89d049f7f34885d20f855f0200004981fa003800000f87520200004c39c10f834902000080792a0274064883c170ebeb488b410848894424204885c00f842b020000498d8548856802498dbd488568024889442440488b442420483b380f850a0200004c3948180f8500020000488b40104885c00f84f3010000498db5287d68024889742448483b300f85de0100008a4008ffc83c060f87d1010000488b542420488b7424284c89efe83502000085c00f88b701000031c048ff0348894310410fb6460848894318498d8424f8bd21004889442430498d8424786022004889442450498d842458e01e004889442458488b442430488b284885ed0f8457010000488b442440483b45000f85480100004c397d180f853e0100004c3975200f8534010000488b45104885c00f8427010000488b742448483b300f85190100008a4008ffc83c060f870c0100004d8d9c2408df1e004531c941b870000000bf010000004d8d53b8498b0a498b72084839ce72644889f031d24829c8488944243849f7f04885d2754f48817c24380038000077444839f1734548396908740c483969107406483969187527488b41304885c0741880781c000fb651280f95c00fb6c031d0ffc8440f45cfeb0641b9010000004883c170ebbc41b9010000004983c2184d39d375814981c3a80000004c395c24580f856bffffff4585c9755c488b7424284889ea4c89efe8d800000085c0782e7532488b052c010000488b4c2428488d5424684c89ef48896c2468ffd0488b7424284889eae8aa000000ffc8740648ff4320eb2a488b4424204889851003000048ff430848ff4310488344243008488b7c243048397c24500f8582feffff488b05d8000000488b4c24280f287424700f28bc2480000000440f28842490000000440f288c24a0000000440f289424b0000000440f289c24c0000000440f28a424d0000000440f28ac24e0000000440f28b424f0000000440f28bc24000100004881c4180100005b5e5f5d415c415d415e415fffe0488b4608488bb730864c034885c0743d4885f674388b0883c8ff3b8f60864c037334488b0cce31f64885c90f95c081fe50140000771184c0740d4839117412488b4908ffc6ebe10fb6c0f7d8c383c8ffc3b801000000c366901111111111111111222222222222222233333333333333334444444444444444";

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
                          &oldProtect))
        return false;

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

    bool PutQword(std::vector<uint8_t>& blob,
                  size_t offset,
                  uintptr_t value) {
      if (offset + sizeof(uint64_t) > blob.size())
        return false;

      const uint64_t v = static_cast<uint64_t>(value);
      std::memcpy(blob.data() + offset, &v, sizeof(v));
      return true;
    }

    void BuildAbsoluteJump(uintptr_t destination,
                           uint8_t* out,
                           size_t size) {
      std::memset(out, 0x90, size);

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
                      uintptr_t cave,
                      std::vector<uint8_t>& out) {
      std::vector<uint8_t> code;
      if (!DecodeTemplate(code))
        return false;

      const uintptr_t trampoline = cave + kTrampolineOffset;
      const uintptr_t stats = cave + kStatsOffset;

      if (!PutQword(code, kBaseQwordOffset, exeBase) ||
          !PutQword(code,
                    kAppendQwordOffset,
                    exeBase + kNativeAppendOffset) ||
          !PutQword(code,
                    kTrampolineQwordOffset,
                    trampoline) ||
          !PutQword(code, kStatsQwordOffset, stats)) {
        return false;
      }

      out.assign(kCaveSize, 0x00);
      std::fill(out.begin(), out.begin() + kTrampolineOffset, 0xCC);

      std::memcpy(out.data(),
                  code.data(),
                  code.size());

      // trampoline:
      //   원래 17-byte prologue
      //   absolute jump -> hook+17
      std::memcpy(out.data() + kTrampolineOffset,
                  kOriginal,
                  sizeof(kOriginal));

      uint8_t returnJump[14]{};
      BuildAbsoluteJump(exeBase + kHookOffset + kHookSize,
                        returnJump,
                        sizeof(returnJump));
      std::memcpy(out.data() + kTrampolineOffset + kHookSize,
                  returnJump,
                  sizeof(returnJump));

      // stats 영역은 0으로 시작해야 합니다.
      std::memset(out.data() + kStatsOffset, 0,
                  kCaveSize - kStatsOffset);

      return true;
    }
  } // namespace

  bool IsIsolatedCityCaptureFixApplied() {
    return g_applied;
  }

  bool InstallIsolatedCityCaptureFix() {
    if (g_applied)
      return true;

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[포로개선/고립도시] 적용 보류: SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hookAddr = exeBase + kHookOffset;

    if (!BytesEqual(hookAddr, kOriginal, sizeof(kOriginal))) {
      AddLog(u8"[포로개선/고립도시] 적용 보류: 원본 바이트 불일치 (+%llX)",
             static_cast<unsigned long long>(kHookOffset));
      return false;
    }

    g_cave = reinterpret_cast<uintptr_t>(
        VirtualAlloc(nullptr,
                     kCaveSize,
                     MEM_COMMIT | MEM_RESERVE,
                     PAGE_EXECUTE_READWRITE));
    if (!g_cave) {
      AddLog(u8"[포로개선/고립도시] 코드케이브 할당 실패");
      return false;
    }

    std::vector<uint8_t> payload;
    if (!BuildPayload(exeBase, g_cave, payload)) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      AddLog(u8"[포로개선/고립도시] payload 구성 실패");
      return false;
    }

    std::memcpy(reinterpret_cast<void*>(g_cave),
                payload.data(),
                payload.size());
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(g_cave),
                          payload.size());

    BuildAbsoluteJump(g_cave,
                      g_hookPatched,
                      sizeof(g_hookPatched));

    if (!WriteBytes(hookAddr,
                    g_hookPatched,
                    sizeof(g_hookPatched))) {
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      std::memset(g_hookPatched, 0, sizeof(g_hookPatched));
      AddLog(u8"[포로개선/고립도시] hook 적용 실패");
      return false;
    }

    if (!BytesEqual(hookAddr,
                    g_hookPatched,
                    sizeof(g_hookPatched))) {
      WriteBytes(hookAddr, kOriginal, sizeof(kOriginal));
      VirtualFree(reinterpret_cast<LPVOID>(g_cave), 0, MEM_RELEASE);
      g_cave = 0;
      std::memset(g_hookPatched, 0, sizeof(g_hookPatched));
      AddLog(u8"[포로개선/고립도시] hook 검증 실패 - 원본 복구");
      return false;
    }

    g_applied = true;

    AddLog(u8"[포로개선/고립도시] 기본 적용 완료 (+%llX)",
           static_cast<unsigned long long>(kHookOffset));
    AddLog(u8"[포로개선/고립도시] 패배 세력의 인접 도시가 없으면 함락 도시 잔류 장수를 포로 목록에 추가합니다.");
    return true;
  }

} // namespace DX11Base
