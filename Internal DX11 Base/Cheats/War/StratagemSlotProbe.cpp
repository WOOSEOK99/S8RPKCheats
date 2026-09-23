#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"

#include <psapi.h>
#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kSideOffset = 0x18;
    constexpr uintptr_t kGaugeOffset = 0x154;
    constexpr uintptr_t kFirstStratagemCountOffset = 0x10C;
    constexpr uintptr_t kStratagemCountStride = 0x10;
    constexpr int kStratagemCountSlots = 10;

    static uintptr_t g_hookAddr = 0;
    static uintptr_t g_caveAddr = 0;
    static uint8_t g_original[5] = {};
    static bool g_hookApplied = false;

    static volatile uintptr_t g_attackInfo = 0;
    static volatile uintptr_t g_defenseInfo = 0;

    static bool g_id5CountApplied = false;
    static uintptr_t g_id5CountAddr = 0;
    static uint8_t g_id5CountOriginal = 0;

    static bool g_fiveLoopApplied = false;
    static uintptr_t g_fiveLoopImmAddr = 0;
    static uint8_t g_fiveLoopOriginal = 0;

    static bool g_fiveMetadataApplied = false;
    static uintptr_t g_fiveMetadataAddr = 0;
    static uint8_t g_fiveMetadataOriginal[0x20] = {};

    static bool BuildCaptureCave(uintptr_t hookAddr) {
      g_caveAddr = AllocNear(hookAddr, 128);
      if (!g_caveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_caveAddr);
      int idx = 0;

      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit64 = [&](uintptr_t v) {
        *reinterpret_cast<uintptr_t *>(&cave[idx]) = v;
        idx += 8;
      };

      emit8(0x52); // push rdx

      // cmp byte ptr [rax+18],0
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x00);
      emit8(0x75); const int jneDefense = idx++;

      // attack -> g_attackInfo
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_attackInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);
      emit8(0xEB); const int jmpDoneAttack = idx++;

      const int checkDefense = idx;

      // cmp byte ptr [rax+18],1
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x01);
      emit8(0x75); const int jneDone = idx++;

      // defense -> g_defenseInfo
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_defenseInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);

      const int done = idx;

      emit8(0x5A); // pop rdx
      emit8(0x0F); emit8(0xB6); emit8(0x40); emit8(0x18); // original movzx eax,[rax+18]
      emit8(0xC3); // original ret

      auto patchRel8 = [&](int dispIndex, int target) -> bool {
        const int rel = target - (dispIndex + 1);
        if (rel < -128 || rel > 127)
          return false;
        cave[dispIndex] = static_cast<uint8_t>(static_cast<int8_t>(rel));
        return true;
      };

      if (!patchRel8(jneDefense, checkDefense) ||
          !patchRel8(jmpDoneAttack, done) ||
          !patchRel8(jneDone, done)) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);
        g_caveAddr = 0;
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      return ApplyJmp(hookAddr, g_caveAddr, 5);
    }

    static bool EnsureCaptureHook() {
      if (g_hookApplied)
        return true;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &mi, sizeof(mi)))
        return false;

      const uintptr_t imageEnd = exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // CT battle-info getter:
      // 48 8B 41 08
      // 48 8B 48 30
      // 48 8B 01
      // 0F B6 40 18
      // C3
      const char *pattern =
          "48 8B 41 08 48 8B 48 30 48 8B 01 0F B6 40 18 C3";
      const uintptr_t found = FindPattern(exeBase, imageEnd, pattern);
      if (!found) {
        AddLog(u8"[책략5슬롯DBG] 전장 정보 getter 패턴을 찾지 못했습니다.");
        return false;
      }

      g_hookAddr = found + 0x0B;
      static const uint8_t expected[5] = {
          0x0F, 0xB6, 0x40, 0x18, 0xC3
      };

      if (!IsValidPtr(g_hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(g_hookAddr),
                      expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략5슬롯DBG] getter 검증 실패: %p",
               reinterpret_cast<void *>(g_hookAddr));
        g_hookAddr = 0;
        return false;
      }

      std::memcpy(g_original,
                  reinterpret_cast<const void *>(g_hookAddr),
                  sizeof(g_original));

      if (!BuildCaptureCave(g_hookAddr)) {
        AddLog(u8"[책략5슬롯DBG] 전장 정보 캡처 훅 설치 실패.");
        g_hookAddr = 0;
        g_caveAddr = 0;
        return false;
      }

      g_hookApplied = true;
      g_attackInfo = 0;
      g_defenseInfo = 0;

      AddLog(u8"[책략5슬롯DBG] 전장 정보 캡처 훅 설치 완료.");
      AddLog(u8"[책략5슬롯DBG] 책략 선택 화면을 한 번 열거나 수량을 변경한 뒤 이 버튼을 다시 누르세요.");
      return true;
    }

    static bool ValidateInfo(uintptr_t ptr, uint8_t expectedSide) {
      if (!ptr)
        return false;

      const uintptr_t lastCount =
          ptr + kFirstStratagemCountOffset +
          (kStratagemCountSlots - 1) * kStratagemCountStride;

      if (!IsValidPtr(ptr + kSideOffset, 1) ||
          !IsValidPtr(ptr + kGaugeOffset, sizeof(uint16_t)) ||
          !IsValidPtr(lastCount, 1))
        return false;

      return *reinterpret_cast<const uint8_t *>(ptr + kSideOffset) ==
             expectedSide;
    }

    static void DumpSide(const char *name, uintptr_t ptr, uint8_t side) {
      if (!ValidateInfo(ptr, side))
        return;

      uint8_t counts[kStratagemCountSlots] = {};
      for (int i = 0; i < kStratagemCountSlots; ++i) {
        counts[i] = *reinterpret_cast<const uint8_t *>(
            ptr + kFirstStratagemCountOffset +
            (uintptr_t)i * kStratagemCountStride);
      }

      const uint16_t gauge =
          *reinterpret_cast<const uint16_t *>(ptr + kGaugeOffset);

      AddLog(
          u8"[책략5슬롯DBG] %s ptr=%p side=%u gauge=%u / "
          "책략수량 ID1~10 = %u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
          name, reinterpret_cast<void *>(ptr), (unsigned)side,
          (unsigned)gauge,
          (unsigned)counts[0], (unsigned)counts[1],
          (unsigned)counts[2], (unsigned)counts[3],
          (unsigned)counts[4], (unsigned)counts[5],
          (unsigned)counts[6], (unsigned)counts[7],
          (unsigned)counts[8], (unsigned)counts[9]);

      AddLog(
          u8"[책략5슬롯DBG] offsets: "
          "ID1=+10C ID2=+11C ID3=+12C ID4=+13C "
          "ID5후보=+14C ID6=+15C ... ID10=+19C");
    }
  }

  bool SetStratagemFiveCountTest(bool enable) {
    if (enable) {
      if (g_id5CountApplied)
        return true;

      if (!EnsureCaptureHook())
        return false;

      struct Candidate {
        uintptr_t ptr;
        uint8_t side;
        const char *name;
      };

      const Candidate candidates[] = {
          {g_attackInfo, 0, u8"공격측"},
          {g_defenseInfo, 1, u8"수비측"},
      };

      uintptr_t chosen = 0;
      const char *chosenName = nullptr;

      for (const auto &candidate : candidates) {
        if (!ValidateInfo(candidate.ptr, candidate.side))
          continue;

        const uint8_t c1 = *reinterpret_cast<const uint8_t *>(
            candidate.ptr + kFirstStratagemCountOffset + 0 * kStratagemCountStride);
        const uint8_t c2 = *reinterpret_cast<const uint8_t *>(
            candidate.ptr + kFirstStratagemCountOffset + 1 * kStratagemCountStride);
        const uint8_t c3 = *reinterpret_cast<const uint8_t *>(
            candidate.ptr + kFirstStratagemCountOffset + 2 * kStratagemCountStride);
        const uint8_t c4 = *reinterpret_cast<const uint8_t *>(
            candidate.ptr + kFirstStratagemCountOffset + 3 * kStratagemCountStride);
        const uint8_t c5 = *reinterpret_cast<const uint8_t *>(
            candidate.ptr + kFirstStratagemCountOffset + 4 * kStratagemCountStride);

        if (c1 == 1 && c2 == 2 && c3 == 1 && c4 == 1 && c5 == 0) {
          if (chosen) {
            AddLog(u8"[책략5슬롯DBG] 공격/수비 양쪽이 모두 1/2/1/1이라 자동 선택할 수 없습니다.");
            return false;
          }
          chosen = candidate.ptr;
          chosenName = candidate.name;
        }
      }

      if (!chosen) {
        AddLog(u8"[책략5슬롯DBG] ID1~4=1/2/1/1, ID5=0인 플레이어측 후보를 찾지 못했습니다.");
        AddLog(u8"[책략5슬롯DBG] 전투 시작 직후 기존 책략을 쓰기 전에 다시 시도하세요.");
        return false;
      }

      const uintptr_t id5Addr =
          chosen + kFirstStratagemCountOffset + 4 * kStratagemCountStride;

      if (!IsValidPtr(id5Addr, 1))
        return false;

      g_id5CountOriginal = *reinterpret_cast<const uint8_t *>(id5Addr);

      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(id5Addr), 1, PAGE_READWRITE, &oldProtect)) {
        AddLog(u8"[책략5슬롯DBG] ID5 수량 슬롯 쓰기 권한 변경 실패: %p",
               reinterpret_cast<void *>(id5Addr));
        return false;
      }

      *reinterpret_cast<uint8_t *>(id5Addr) = 1;
      VirtualProtect(reinterpret_cast<LPVOID>(id5Addr), 1, oldProtect, &tmpProtect);

      if (*reinterpret_cast<const uint8_t *>(id5Addr) != 1) {
        AddLog(u8"[책략5슬롯DBG] ID5 수량 1 쓰기 검증 실패.");
        return false;
      }

      g_id5CountAddr = id5Addr;
      g_id5CountApplied = true;

      AddLog(u8"[책략5슬롯DBG] %s ID5 후보(+14C) 수량 0 -> 1 적용 성공: %p",
             chosenName, reinterpret_cast<void *>(id5Addr));
      AddLog(u8"[책략5슬롯DBG] 이제 전투 책략 UI에 기존 4개와 별도로 5번이 나타나는지 확인하세요.");
      return true;
    }

    if (!g_id5CountApplied)
      return true;

    if (g_id5CountAddr && IsValidPtr(g_id5CountAddr, 1)) {
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (VirtualProtect(reinterpret_cast<LPVOID>(g_id5CountAddr), 1, PAGE_READWRITE, &oldProtect)) {
        *reinterpret_cast<uint8_t *>(g_id5CountAddr) = g_id5CountOriginal;
        VirtualProtect(reinterpret_cast<LPVOID>(g_id5CountAddr), 1, oldProtect, &tmpProtect);
      }
    }

    AddLog(u8"[책략5슬롯DBG] ID5 수량 테스트 원복.");
    g_id5CountApplied = false;
    g_id5CountAddr = 0;
    g_id5CountOriginal = 0;
    return true;
  }

  void ScanStratagemFourLimitCodeCandidates() {
    const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (!exeBase) {
      AddLog(u8"[책략4제한DBG] EXE base를 찾지 못했습니다.");
      return;
    }

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(),
                              reinterpret_cast<HMODULE>(exeBase),
                              &mi, sizeof(mi))) {
      AddLog(u8"[책략4제한DBG] 모듈 정보 읽기 실패.");
      return;
    }

    const uintptr_t imageEnd = exeBase + static_cast<uintptr_t>(mi.SizeOfImage);
    int logged = 0;
    constexpr int kMaxLogs = 40;

    auto hasCountDisp = [](const uint8_t *p) -> bool {
      const uint32_t v = *reinterpret_cast<const uint32_t *>(p);
      return v == 0x10C || v == 0x11C || v == 0x12C ||
             v == 0x13C || v == 0x14C;
    };

    auto hasCmp4 = [](const uint8_t *p, size_t n) -> bool {
      for (size_t i = 0; i + 2 < n; ++i) {
        // cmp r/m32, 4  => 83 /7 04
        if (p[i] == 0x83 && (p[i + 1] & 0x38) == 0x38 && p[i + 2] == 0x04)
          return true;
        // cmp r/m8, 4 => 80 /7 04
        if (p[i] == 0x80 && (p[i + 1] & 0x38) == 0x38 && p[i + 2] == 0x04)
          return true;
        // cmp al,4
        if (p[i] == 0x3C && p[i + 1] == 0x04)
          return true;
        // cmp eax,4
        if (i + 4 < n && p[i] == 0x3D &&
            p[i + 1] == 0x04 && p[i + 2] == 0x00 &&
            p[i + 3] == 0x00 && p[i + 4] == 0x00)
          return true;
      }
      return false;
    };

    AddLog(u8"[책략4제한DBG] EXE 코드 진단 시작: +10C~+14C 참조 근처의 하드코딩된 비교값 4를 찾습니다.");

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t cur = exeBase;

    while (cur < imageEnd && logged < kMaxLogs) {
      if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) != sizeof(mbi))
        break;

      const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
      uintptr_t regionEnd = regionStart + mbi.RegionSize;
      if (regionEnd > imageEnd)
        regionEnd = imageEnd;

      const DWORD prot = mbi.Protect & 0xFF;
      const bool executable =
          mbi.State == MEM_COMMIT &&
          !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
          (prot == PAGE_EXECUTE ||
           prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE ||
           prot == PAGE_EXECUTE_WRITECOPY);

      if (executable && regionEnd > regionStart + 8) {
        __try {
          const uint8_t *b = reinterpret_cast<const uint8_t *>(regionStart);
          const size_t n = (size_t)(regionEnd - regionStart);

          for (size_t i = 0; i + 4 <= n && logged < kMaxLogs; ++i) {
            if (!hasCountDisp(b + i))
              continue;

            const size_t from = (i > 0x60) ? i - 0x60 : 0;
            const size_t to = ((i + 0x60) < n) ? i + 0x60 : n;
            if (!hasCmp4(b + from, to - from))
              continue;

            const uintptr_t addr = regionStart + i;
            const uint32_t disp = *reinterpret_cast<const uint32_t *>(b + i);

            AddLog(u8"[책략4제한DBG] 후보 #%d code=%p disp=+%X",
                   logged + 1, reinterpret_cast<void *>(addr), (unsigned)disp);

            const uintptr_t dumpStart =
                (addr > regionStart + 0x20) ? addr - 0x20 : regionStart;
            const size_t remain = (size_t)(regionEnd - dumpStart);
            const size_t bytes = remain >= 0x60 ? 0x60 : remain;

            char line[1024] = {};
            int pos = 0;
            for (size_t j = 0; j < bytes && pos < (int)sizeof(line) - 4; ++j) {
              pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ",
                               *reinterpret_cast<const uint8_t *>(dumpStart + j));
            }
            AddLog(u8"[책략4제한DBG] bytes @ %p : %s",
                   reinterpret_cast<void *>(dumpStart), line);
            ++logged;
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
      }

      if (regionEnd <= cur)
        break;
      cur = regionEnd;
    }

    AddLog(u8"[책략4제한DBG] 진단 완료: 후보 %d개.", logged);
    if (logged == 0) {
      AddLog(u8"[책략4제한DBG] +10C~+14C 직접 참조와 cmp 4 조합은 없음. 다음은 UI 목록 생성 함수 쪽에서 독립적으로 4 제한을 찾습니다.");
    }
  }

  bool SetStratagemFiveLoopTest(bool enable) {
    if (enable) {
      if (g_fiveLoopApplied)
        return true;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &mi, sizeof(mi)))
        return false;

      const uintptr_t imageEnd =
          exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // Runtime candidate #34:
      // ... mov rax,[rsi+50]
      //     call qword ptr [rax+20]
      //     mov r9,[rbp+18]
      //     lea r14,[r14+20]
      //     inc r12d
      //     add r15,20
      //     cmp r12d,04
      //     jb  <loop>
      //
      // Both iterators advance by 0x20, which matches the confirmed
      // SpellRecord stride. Patch only the immediate 04 -> 05.
      const char *pat =
          "48 8B 46 50 FF 50 20 4C 8B 4D 18 "
          "4D 8D 76 20 41 FF C4 49 83 C7 20 "
          "41 83 FC 04 72 ?";

      const uintptr_t found = FindPattern(exeBase, imageEnd, pat);
      if (!found) {
        AddLog(u8"[책략5루프DBG] 후보 #34 시그니처를 찾지 못했습니다.");
        return false;
      }

      // "41 83 FC 04" starts at found+22, immediate byte is +25.
      const uintptr_t immAddr = found + 25;
      if (!IsValidPtr(immAddr, 1) ||
          *reinterpret_cast<const uint8_t *>(immAddr) != 0x04) {
        AddLog(u8"[책략5루프DBG] 비교값 검증 실패: found=%p imm=%p value=%02X",
               reinterpret_cast<void *>(found),
               reinterpret_cast<void *>(immAddr),
               IsValidPtr(immAddr, 1)
                   ? (unsigned)*reinterpret_cast<const uint8_t *>(immAddr)
                   : 0xFFu);
        return false;
      }

      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(immAddr), 1,
                          PAGE_EXECUTE_READWRITE, &oldProtect)) {
        AddLog(u8"[책략5루프DBG] VirtualProtect 실패: %p",
               reinterpret_cast<void *>(immAddr));
        return false;
      }

      g_fiveLoopOriginal = *reinterpret_cast<const uint8_t *>(immAddr);
      *reinterpret_cast<uint8_t *>(immAddr) = 0x05;
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<LPCVOID>(immAddr), 1);
      VirtualProtect(reinterpret_cast<LPVOID>(immAddr), 1,
                     oldProtect, &tmpProtect);

      if (*reinterpret_cast<const uint8_t *>(immAddr) != 0x05) {
        AddLog(u8"[책략5루프DBG] 4 -> 5 쓰기 검증 실패.");
        return false;
      }

      g_fiveLoopImmAddr = immAddr;
      g_fiveLoopApplied = true;

      AddLog(u8"[책략5루프DBG] 후보 #34 루프 제한 4 -> 5 적용 성공: code=%p imm=%p",
             reinterpret_cast<void *>(found),
             reinterpret_cast<void *>(immAddr));
      AddLog(u8"[책략5루프DBG] 5번 데이터/횟수 테스트를 켠 상태에서 책략 UI를 다시 열어 5개가 보이는지 확인하세요.");
      return true;
    }

    if (!g_fiveLoopApplied)
      return true;

    if (g_fiveLoopImmAddr && IsValidPtr(g_fiveLoopImmAddr, 1)) {
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (VirtualProtect(reinterpret_cast<LPVOID>(g_fiveLoopImmAddr), 1,
                         PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *reinterpret_cast<uint8_t *>(g_fiveLoopImmAddr) =
            g_fiveLoopOriginal;
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(g_fiveLoopImmAddr), 1);
        VirtualProtect(reinterpret_cast<LPVOID>(g_fiveLoopImmAddr), 1,
                       oldProtect, &tmpProtect);
      }
    }

    AddLog(u8"[책략5루프DBG] 후보 #34 루프 제한 원복 완료.");
    g_fiveLoopApplied = false;
    g_fiveLoopImmAddr = 0;
    g_fiveLoopOriginal = 0;
    return true;
  }

  bool SetStratagemFiveMetadataTest(bool enable) {
    constexpr uintptr_t kStratagemMetadataOffset = 0x9D30;
    constexpr uintptr_t kRecordStride = 0x20;
    constexpr uintptr_t kId1 = 0x08;
    constexpr uintptr_t kId2 = 0x0A;
    constexpr uintptr_t kId3 = 0x0C;
    constexpr uintptr_t kFlag = 0x0E;

    if (enable) {
      if (g_fiveMetadataApplied)
        return true;

      const uintptr_t gameBase = GetGameBase();
      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!gameBase || !exeBase) {
        AddLog(u8"[책략5메타DBG] gameBase/EXE base를 찾지 못했습니다.");
        return false;
      }

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &mi, sizeof(mi))) {
        AddLog(u8"[책략5메타DBG] 모듈 정보 읽기 실패.");
        return false;
      }

      const uintptr_t imageEnd =
          exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // Old CT's bytes8 copied byte-for-byte. Our FindPattern accepts
      // compact CE-style ?? wildcards, so do not manually expand it.
      // CT:
      // 488B????????????E8????????488B??4883????5BC333??488B??E8????????488B??4883????5BC3E8??????????????????????4053
      // and reads the dynamic offset from found+4.
      const char *troopTypePat =
          "488B????????????E8????????488B??4883????5BC333??488B??E8????????488B??4883????5BC3E8??????????????????????4053";

      uintptr_t scanStart = exeBase + 0x58E000;
      uintptr_t scanEnd = exeBase + 0x59E000;
      if (scanStart >= imageEnd)
        scanStart = exeBase;
      if (scanEnd > imageEnd)
        scanEnd = imageEnd;

      uintptr_t found = FindPattern(scanStart, scanEnd, troopTypePat);
      if (!found) {
        // Version drift fallback: same exact signature across the full image.
        found = FindPattern(exeBase, imageEnd, troopTypePat);
      }

      if (!found || !IsValidPtr(found + 4, sizeof(uint32_t))) {
        AddLog(u8"[책략5메타DBG] CT 원본 troopTypePointerOffset 패턴도 찾지 못했습니다.");
        return false;
      }

      AddLog(u8"[책략5메타DBG] troopType 패턴 발견: %p / +4=%02X %02X %02X %02X",
             reinterpret_cast<void *>(found),
             (unsigned)*reinterpret_cast<const uint8_t *>(found + 4),
             (unsigned)*reinterpret_cast<const uint8_t *>(found + 5),
             (unsigned)*reinterpret_cast<const uint8_t *>(found + 6),
             (unsigned)*reinterpret_cast<const uint8_t *>(found + 7));

      const uint32_t troopTypePointerOffset =
          *reinterpret_cast<const uint32_t *>(found + 4);
      const uintptr_t pointerAddr =
          gameBase + static_cast<uintptr_t>(troopTypePointerOffset);

      if (!IsValidPtr(pointerAddr, sizeof(uintptr_t))) {
        AddLog(u8"[책략5메타DBG] troopType 포인터 주소 무효: offset=0x%X addr=%p",
               (unsigned)troopTypePointerOffset,
               reinterpret_cast<void *>(pointerAddr));
        return false;
      }

      const uintptr_t troopTypeBase =
          *reinterpret_cast<const uintptr_t *>(pointerAddr);
      if (!troopTypeBase || troopTypeBase == UINTPTR_MAX ||
          !IsValidPtr(troopTypeBase + kStratagemMetadataOffset,
                      kRecordStride * 5)) {
        AddLog(u8"[책략5메타DBG] troopType base/책략 메타 범위 무효: offset=0x%X base=%p",
               (unsigned)troopTypePointerOffset,
               reinterpret_cast<void *>(troopTypeBase));
        return false;
      }

      const uintptr_t table = troopTypeBase + kStratagemMetadataOffset;
      AddLog(u8"[책략5메타DBG] 동적 troopTypeOffset=0x%X base=%p table=%p",
             (unsigned)troopTypePointerOffset,
             reinterpret_cast<void *>(troopTypeBase),
             reinterpret_cast<void *>(table));
      if (!IsValidPtr(table, kRecordStride * 5)) {
        AddLog(u8"[책략5메타DBG] 책략 메타 테이블 범위가 유효하지 않습니다: %p",
               reinterpret_cast<void *>(table));
        return false;
      }

      // Strong validation from the CT layout: rows 1..4 must carry
      // ID/name/description IDs 1,2,3,4 at +08/+0A/+0C.
      for (int i = 0; i < 4; ++i) {
        const uintptr_t row = table + (uintptr_t)i * kRecordStride;
        const uint8_t expected = (uint8_t)(i + 1);
        const uint8_t a = *reinterpret_cast<const uint8_t *>(row + kId1);
        const uint8_t b = *reinterpret_cast<const uint8_t *>(row + kId2);
        const uint8_t d = *reinterpret_cast<const uint8_t *>(row + kId3);
        if (a != expected || b != expected || d != expected) {
          AddLog(u8"[책략5메타DBG] 메타 테이블 검증 실패 row=%d IDs=%u/%u/%u",
                 i + 1, (unsigned)a, (unsigned)b, (unsigned)d);
          return false;
        }
      }

      const uintptr_t source = table + 3 * kRecordStride; // valid row 4
      const uintptr_t row5 = table + 4 * kRecordStride;

      std::memcpy(g_fiveMetadataOriginal,
                  reinterpret_cast<const void *>(row5),
                  sizeof(g_fiveMetadataOriginal));

      uint8_t clone[0x20] = {};
      std::memcpy(clone, reinterpret_cast<const void *>(source), sizeof(clone));
      clone[kId1] = 5;
      clone[kId2] = 5;
      clone[kId3] = 5;

      const uint8_t sourceFlag =
          *reinterpret_cast<const uint8_t *>(source + kFlag);
      clone[kFlag] = sourceFlag;

      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(row5), sizeof(clone),
                          PAGE_READWRITE, &oldProtect)) {
        AddLog(u8"[책략5메타DBG] 5번 메타 행 쓰기 권한 변경 실패.");
        return false;
      }

      std::memcpy(reinterpret_cast<void *>(row5), clone, sizeof(clone));
      VirtualProtect(reinterpret_cast<LPVOID>(row5), sizeof(clone),
                     oldProtect, &tmpProtect);

      if (*reinterpret_cast<const uint8_t *>(row5 + kId1) != 5 ||
          *reinterpret_cast<const uint8_t *>(row5 + kId2) != 5 ||
          *reinterpret_cast<const uint8_t *>(row5 + kId3) != 5 ||
          *reinterpret_cast<const uint8_t *>(row5 + kFlag) != sourceFlag) {
        AddLog(u8"[책략5메타DBG] 5번 메타 행 쓰기 검증 실패.");
        return false;
      }

      g_fiveMetadataAddr = row5;
      g_fiveMetadataApplied = true;

      AddLog(u8"[책략5메타DBG] 적용 성공 table=%p row5=%p IDs=5/5/5 flag=%u",
             reinterpret_cast<void *>(table),
             reinterpret_cast<void *>(row5),
             (unsigned)sourceFlag);
      AddLog(u8"[책략5메타DBG] 4번 메타 행을 복제하고 ID/이름/설명 ID만 5로 변경했습니다.");
      return true;
    }

    if (!g_fiveMetadataApplied)
      return true;

    if (g_fiveMetadataAddr &&
        IsValidPtr(g_fiveMetadataAddr, sizeof(g_fiveMetadataOriginal))) {
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (VirtualProtect(reinterpret_cast<LPVOID>(g_fiveMetadataAddr),
                         sizeof(g_fiveMetadataOriginal),
                         PAGE_READWRITE, &oldProtect)) {
        std::memcpy(reinterpret_cast<void *>(g_fiveMetadataAddr),
                    g_fiveMetadataOriginal,
                    sizeof(g_fiveMetadataOriginal));
        VirtualProtect(reinterpret_cast<LPVOID>(g_fiveMetadataAddr),
                       sizeof(g_fiveMetadataOriginal),
                       oldProtect, &tmpProtect);
      }
    }

    AddLog(u8"[책략5메타DBG] 5번 메타 행 원복 완료.");
    g_fiveMetadataApplied = false;
    g_fiveMetadataAddr = 0;
    std::memset(g_fiveMetadataOriginal, 0, sizeof(g_fiveMetadataOriginal));
    return true;
  }

  void ScanStratagemFiveSlotCandidates() {
    if (!EnsureCaptureHook())
      return;

    bool dumped = false;

    const uintptr_t attack = g_attackInfo;
    if (ValidateInfo(attack, 0)) {
      DumpSide(u8"공격측", attack, 0);
      dumped = true;
    }

    const uintptr_t defense = g_defenseInfo;
    if (ValidateInfo(defense, 1)) {
      DumpSide(u8"수비측", defense, 1);
      dumped = true;
    }

    if (!dumped) {
      AddLog(u8"[책략5슬롯DBG] 아직 전장 정보 포인터가 잡히지 않았습니다.");
      AddLog(u8"[책략5슬롯DBG] 책략 선택 화면을 열고 수량을 한 번 변경한 뒤 다시 누르세요.");
    }
  }
} // namespace DX11Base
