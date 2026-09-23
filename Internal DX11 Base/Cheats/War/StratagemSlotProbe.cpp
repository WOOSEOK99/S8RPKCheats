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
    static uintptr_t g_id5CountOwner = 0;
    static uint8_t g_id5CountOriginal = 0;

    static bool g_fiveLoopApplied = false;
    static uintptr_t g_fiveLoopImmAddr = 0;
    static uint8_t g_fiveLoopOriginal = 0;

    static bool g_fiveMetadataApplied = false;
    static uintptr_t g_fiveMetadataAddr = 0;
    static uintptr_t g_fiveMetadataTable = 0;

    static bool g_fiveRuntimeSlotApplied = false;
    static uintptr_t g_fiveRuntimeSlotAddr = 0;
    static uintptr_t g_fiveRuntimeSlotOriginal = 0;

    // Runtime UI instance capture. The hook is build-guarded and only records
    // TrickCommandDialogLayout* when ResetBtnPos runs; it does not change UI data.
    static uintptr_t g_uiLayoutHookAddr = 0;
    static uintptr_t g_uiLayoutCaveAddr = 0;
    static uint8_t g_uiLayoutOriginal[7] = {};
    static bool g_uiLayoutHookApplied = false;
    static volatile uintptr_t g_trickUiLayout = 0;
    static volatile int32_t g_trickUiStartX = 0;
    static volatile int32_t g_trickUiY = 0;
    static volatile int32_t g_trickUiStep = 0;
    static uintptr_t g_lastLoggedUiLayout = 0;
    static uintptr_t g_uiDialogHookAddr = 0;
    static uintptr_t g_uiDialogCaveAddr = 0;
    static uint8_t g_uiDialogOriginal[9] = {};
    static bool g_uiDialogHookApplied = false;
    static volatile uintptr_t g_trickUiDialog = 0;
    static uintptr_t g_fifthUiSidecarLayout = 0;
    static uintptr_t g_fifthUiSidecarButton = 0;
    static bool g_fifthUiSidecarAttempted = false;
    static bool g_fifthUiMakerExpanded = false;
    static uintptr_t g_fifthUiMakerAddr = 0;
    static uint8_t g_fifthUiMakerOriginal[0x28] = {};

    static bool SafeReadPtrSeh(uintptr_t addr, uintptr_t *outValue) {
      if (!addr || !outValue)
        return false;

      __try {
        *outValue = *reinterpret_cast<const uintptr_t *>(addr);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outValue = 0;
        return false;
      }
    }

    static bool SafeCopySeh(uintptr_t addr, void *dst, size_t bytes) {
      if (!addr || !dst || bytes == 0)
        return false;

      __try {
        std::memcpy(dst, reinterpret_cast<const void *>(addr), bytes);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }


    static bool BuildTrickUiLayoutCaptureCave(uintptr_t hookAddr) {
      g_uiLayoutCaveAddr = AllocNear(hookAddr, 128);
      if (!g_uiLayoutCaveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_uiLayoutCaveAddr);
      int idx = 0;

      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit32 = [&](int32_t v) {
        std::memcpy(cave + idx, &v, sizeof(v));
        idx += 4;
      };
      auto emit64 = [&](uintptr_t v) {
        std::memcpy(cave + idx, &v, sizeof(v));
        idx += 8;
      };

      // Preserve RAX, store the live TrickCommandDialogLayout* held in RSI,
      // then execute the original "add rsi, 0x1E0".
      emit8(0x50);                         // push rax
      emit8(0x48); emit8(0xB8);            // mov rax, imm64
      emit64(reinterpret_cast<uintptr_t>(&g_trickUiLayout));
      emit8(0x48); emit8(0x89); emit8(0x30); // mov [rax], rsi

      emit8(0x48); emit8(0xB8);
      emit64(reinterpret_cast<uintptr_t>(&g_trickUiStartX));
      emit8(0x89); emit8(0x38);            // mov [rax], edi

      emit8(0x48); emit8(0xB8);
      emit64(reinterpret_cast<uintptr_t>(&g_trickUiY));
      emit8(0x44); emit8(0x89); emit8(0x30); // mov [rax], r14d

      emit8(0x48); emit8(0xB8);
      emit64(reinterpret_cast<uintptr_t>(&g_trickUiStep));
      emit8(0x89); emit8(0x18);            // mov [rax], ebx
      emit8(0x58);                         // pop rax

      static const uint8_t originalAdd[7] = {
          0x48, 0x81, 0xC6, 0xE0, 0x01, 0x00, 0x00
      };
      std::memcpy(cave + idx, originalAdd, sizeof(originalAdd));
      idx += (int)sizeof(originalAdd);

      // jmp back to the instruction after the 7-byte patch.
      emit8(0xE9);
      const intptr_t rel =
          static_cast<intptr_t>(hookAddr + 7) -
          static_cast<intptr_t>(g_uiLayoutCaveAddr + idx + 4);
      if (rel < INT32_MIN || rel > INT32_MAX) {
        VirtualFree(reinterpret_cast<LPVOID>(g_uiLayoutCaveAddr), 0, MEM_RELEASE);
        g_uiLayoutCaveAddr = 0;
        return false;
      }
      emit32(static_cast<int32_t>(rel));

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      if (!ApplyJmp(hookAddr, g_uiLayoutCaveAddr, 7)) {
        VirtualFree(reinterpret_cast<LPVOID>(g_uiLayoutCaveAddr), 0, MEM_RELEASE);
        g_uiLayoutCaveAddr = 0;
        return false;
      }
      return true;
    }

    static bool EnsureTrickUiLayoutCaptureHook() {
      if (g_uiLayoutHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      // PDB/runtime-confirmed:
      // TrickCommandDialogLayout::ResetBtnPos RVA 0x01DAE9F0
      // +0x1C8 = "add rsi, 0x1E0", where RSI is the live layout object.
      constexpr uintptr_t kResetBtnPosRva = 0x01DAE9F0;
      constexpr uintptr_t kCaptureOffset = 0x1C8;
      const uintptr_t hookAddr = exeBase + kResetBtnPosRva + kCaptureOffset;

      static const uint8_t expected[7] = {
          0x48, 0x81, 0xC6, 0xE0, 0x01, 0x00, 0x00
      };
      if (!IsValidPtr(hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(hookAddr),
                      expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략5UICAP] ResetBtnPos 캡처 지점 검증 실패: %p",
               reinterpret_cast<void *>(hookAddr));
        return false;
      }

      std::memcpy(g_uiLayoutOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_uiLayoutOriginal));

      if (!BuildTrickUiLayoutCaptureCave(hookAddr)) {
        AddLog(u8"[책략5UICAP] layout 캡처 훅 설치 실패.");
        return false;
      }

      g_uiLayoutHookAddr = hookAddr;
      g_uiLayoutHookApplied = true;
      g_trickUiLayout = 0;
      g_trickUiStartX = 0;
      g_trickUiY = 0;
      g_trickUiStep = 0;
      g_trickUiDialog = 0;
      g_lastLoggedUiLayout = 0;
      AddLog(u8"[책략5UICAP] layout 캡처 훅 설치 완료. 책략창을 한 번 여세요.");
      return true;
    }


    static bool BuildTrickUiDialogCaptureCave(uintptr_t hookAddr) {
      g_uiDialogCaveAddr = AllocNear(hookAddr, 64);
      if (!g_uiDialogCaveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_uiDialogCaveAddr);
      int idx = 0;
      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit32 = [&](int32_t v) {
        std::memcpy(cave + idx, &v, sizeof(v));
        idx += 4;
      };
      auto emit64 = [&](uintptr_t v) {
        std::memcpy(cave + idx, &v, sizeof(v));
        idx += 8;
      };

      // Dialog::Open +0x130: R15 is the live TrickCommandDialog*.
      emit8(0x50);                         // push rax
      emit8(0x48); emit8(0xB8);            // mov rax, imm64
      emit64(reinterpret_cast<uintptr_t>(&g_trickUiDialog));
      emit8(0x4C); emit8(0x89); emit8(0x38); // mov [rax], r15
      emit8(0x58);                         // pop rax

      static const uint8_t original[9] = {
          0x49, 0x8B, 0x4F, 0x10,
          0x4C, 0x8B, 0x64, 0x24, 0x28
      };
      std::memcpy(cave + idx, original, sizeof(original));
      idx += (int)sizeof(original);

      emit8(0xE9);
      const intptr_t rel =
          static_cast<intptr_t>(hookAddr + sizeof(original)) -
          static_cast<intptr_t>(g_uiDialogCaveAddr + idx + 4);
      if (rel < INT32_MIN || rel > INT32_MAX) {
        VirtualFree(reinterpret_cast<LPVOID>(g_uiDialogCaveAddr), 0, MEM_RELEASE);
        g_uiDialogCaveAddr = 0;
        return false;
      }
      emit32(static_cast<int32_t>(rel));

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      if (!ApplyJmp(hookAddr, g_uiDialogCaveAddr, sizeof(original))) {
        VirtualFree(reinterpret_cast<LPVOID>(g_uiDialogCaveAddr), 0, MEM_RELEASE);
        g_uiDialogCaveAddr = 0;
        return false;
      }
      return true;
    }

    static bool EnsureTrickUiDialogCaptureHook() {
      if (g_uiDialogHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      // Runtime-confirmed Dialog::Open RVA and stable interior instruction.
      constexpr uintptr_t kDialogOpenRva = 0x01DF3CB0;
      constexpr uintptr_t kCaptureOffset = 0x130;
      const uintptr_t hookAddr = exeBase + kDialogOpenRva + kCaptureOffset;

      static const uint8_t expected[9] = {
          0x49, 0x8B, 0x4F, 0x10,
          0x4C, 0x8B, 0x64, 0x24, 0x28
      };
      if (!IsValidPtr(hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(hookAddr),
                      expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략5UICAP] Dialog::Open 캡처 지점 검증 실패: %p",
               reinterpret_cast<void *>(hookAddr));
        return false;
      }

      std::memcpy(g_uiDialogOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_uiDialogOriginal));

      if (!BuildTrickUiDialogCaptureCave(hookAddr)) {
        AddLog(u8"[책략5UICAP] dialog 캡처 훅 설치 실패.");
        return false;
      }

      g_uiDialogHookAddr = hookAddr;
      g_uiDialogHookApplied = true;
      g_trickUiDialog = 0;
      AddLog(u8"[책략5UICAP] dialog 캡처 훅 설치 완료.");
      return true;
    }

    static void RestoreFifthUiMakerTestSeh() {
      __try {
        if (g_fifthUiMakerExpanded &&
            g_fifthUiMakerAddr &&
            IsValidPtr(g_fifthUiMakerAddr, sizeof(g_fifthUiMakerOriginal))) {
          std::memcpy(reinterpret_cast<void *>(g_fifthUiMakerAddr),
                      g_fifthUiMakerOriginal,
                      sizeof(g_fifthUiMakerOriginal));
          AddLog(u8"[책략5UITEST] UI registry를 기존 7칸 상태로 원복.");
        }

        if (g_fifthUiSidecarButton &&
            IsValidPtr(g_fifthUiSidecarButton, sizeof(uintptr_t))) {
          const uintptr_t vt =
              *reinterpret_cast<const uintptr_t *>(g_fifthUiSidecarButton);
          if (vt && IsValidPtr(vt + 0x108, sizeof(uintptr_t))) {
            const uintptr_t setVisible =
                *reinterpret_cast<const uintptr_t *>(vt + 0x108);
            if (setVisible && IsValidPtr(setVisible, 1)) {
              using SetBoolFn = void(__fastcall *)(uintptr_t, bool);
              reinterpret_cast<SetBoolFn>(setVisible)(
                  g_fifthUiSidecarButton, false);
            }
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }

      g_fifthUiMakerExpanded = false;
      g_fifthUiMakerAddr = 0;
      std::memset(g_fifthUiMakerOriginal, 0,
                  sizeof(g_fifthUiMakerOriginal));
    }

    static bool ExpandMakerAndRegisterFifthSidecarSeh(uintptr_t layout) {
      if (!layout ||
          !g_fifthUiSidecarButton ||
          g_fifthUiSidecarLayout != layout ||
          !IsValidPtr(layout, 0x2A8) ||
          !IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return false;

      if (g_fifthUiMakerExpanded &&
          g_fifthUiMakerAddr == layout + 0x140)
        return true;

      volatile int stage = 0;

      __try {
        const uintptr_t exeBase =
            reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
        if (!exeBase)
          return false;

        constexpr uintptr_t kInitLayoutsRva = 0x01D13E60;
        constexpr uintptr_t kLookupLayoutRva = 0x01D15440;
        constexpr uintptr_t kRegisterLayoutRva = 0x01D16AA0;

        static const uint8_t initExpected[] =
            {0x4C,0x89,0x4C,0x24,0x20,0x55};
        static const uint8_t lookupExpected[] =
            {0x48,0x89,0x5C,0x24,0x08};
        static const uint8_t regExpected[] =
            {0x48,0x89,0x6C,0x24,0x18,0x56};

        stage = 1;
        if (!IsValidPtr(exeBase + kInitLayoutsRva, sizeof(initExpected)) ||
            !IsValidPtr(exeBase + kLookupLayoutRva, sizeof(lookupExpected)) ||
            !IsValidPtr(exeBase + kRegisterLayoutRva, sizeof(regExpected)) ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kInitLayoutsRva),
                        initExpected, sizeof(initExpected)) != 0 ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kLookupLayoutRva),
                        lookupExpected, sizeof(lookupExpected)) != 0 ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kRegisterLayoutRva),
                        regExpected, sizeof(regExpected)) != 0) {
          AddLog(u8"[책략5UITEST] CUIMaker 함수 빌드 가드 불일치. registry 확장 중단.");
          return false;
        }

        const uintptr_t maker = layout + 0x140;
        uintptr_t descBase = 0;
        uintptr_t pointerBase = 0;
        uint32_t count = 0;
        uintptr_t owner = 0;

        stage = 2;
        if (!SafeReadPtrSeh(maker + 0x00, &descBase) ||
            !SafeReadPtrSeh(maker + 0x08, &pointerBase) ||
            !SafeCopySeh(maker + 0x10, &count, sizeof(count)) ||
            !SafeReadPtrSeh(maker + 0x18, &owner) ||
            count != 7 ||
            owner != layout ||
            !descBase || !pointerBase ||
            !IsValidPtr(descBase, 7 * 0x60) ||
            !IsValidPtr(pointerBase, 7 * sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] 기존 CUIMaker 구조 검증 실패: count=%u owner=%p",
                 (unsigned)count,
                 reinterpret_cast<void *>(owner));
          return false;
        }

        uintptr_t buttons[4] = {};
        if (!SafeCopySeh(layout + 0x1E0, buttons, sizeof(buttons)))
          return false;

        using LookupLayoutFn = uintptr_t(__fastcall *)(uintptr_t, int);
        using InitLayoutsFn = void(__fastcall *)(uintptr_t, const void *, int, uintptr_t);
        using RegisterLayoutFn = void(__fastcall *)(uintptr_t, int, uintptr_t, int);

        const auto lookup =
            reinterpret_cast<LookupLayoutFn>(exeBase + kLookupLayoutRva);
        const auto initLayouts =
            reinterpret_cast<InitLayoutsFn>(exeBase + kInitLayoutsRva);
        const auto registerLayout =
            reinterpret_cast<RegisterLayoutFn>(exeBase + kRegisterLayoutRva);

        uintptr_t registered[7] = {};
        stage = 3;
        for (int id = 0; id < 7; ++id)
          registered[id] = lookup(maker, id);

        bool buttonMapOk = true;
        for (int i = 0; i < 4; ++i) {
          if (registered[i + 2] != buttons[i])
            buttonMapOk = false;
        }

        uintptr_t id6Control = 0;
        SafeReadPtrSeh(layout + 0x290, &id6Control);
        if (!buttonMapOk ||
            !id6Control ||
            registered[6] != id6Control) {
          AddLog(u8"[책략5UITEST] registry lookup 검증 실패: id2..5/button 또는 id6 불일치.");
          AddLog(u8"[책략5UITEST] lookup=%p,%p,%p,%p,%p,%p,%p",
                 reinterpret_cast<void *>(registered[0]),
                 reinterpret_cast<void *>(registered[1]),
                 reinterpret_cast<void *>(registered[2]),
                 reinterpret_cast<void *>(registered[3]),
                 reinterpret_cast<void *>(registered[4]),
                 reinterpret_cast<void *>(registered[5]),
                 reinterpret_cast<void *>(registered[6]));
          return false;
        }

        alignas(16) uint8_t descriptors[8 * 0x60] = {};
        stage = 4;
        std::memcpy(descriptors,
                    reinterpret_cast<const void *>(descBase),
                    7 * 0x60);

        // ID7 starts from the proven button descriptor (UI ID5).
        std::memcpy(descriptors + 7 * 0x60,
                    descriptors + 5 * 0x60,
                    0x60);

        const int oldStart = (int)g_trickUiStartX;
        const int oldStep = (int)g_trickUiStep;
        const int y = (int)g_trickUiY;
        if (oldStep <= 0)
          return false;

        // Keep the old four-button row center, but compress five buttons into
        // almost the same horizontal span. 280 -> 220 on the confirmed build.
        const int compactStep = (oldStep * 11) / 14;
        const int oldCenter = oldStart + (oldStep * 3) / 2;
        const int compactStart = oldCenter - compactStep * 2;
        const int compactX[5] = {
            compactStart,
            compactStart + compactStep,
            compactStart + compactStep * 2,
            compactStart + compactStep * 3,
            compactStart + compactStep * 4
        };

        const int oldX[4] = {
            oldStart,
            oldStart + oldStep,
            oldStart + oldStep * 2,
            oldStart + oldStep * 3
        };

        int xField = -1;
        int yField = -1;
        const int candidateFields[] = {0x04, 0x08, 0x0C, 0x10};
        for (int field : candidateFields) {
          bool xMatch = true;
          bool yMatch = true;
          for (int i = 0; i < 4; ++i) {
            int32_t v = 0;
            std::memcpy(&v,
                        descriptors + (i + 2) * 0x60 + field,
                        sizeof(v));
            if (v != oldX[i])
              xMatch = false;
            if (v != y)
              yMatch = false;
          }
          if (xMatch)
            xField = field;
          if (yMatch)
            yField = field;
        }

        if (xField < 0 || yField < 0) {
          AddLog(u8"[책략5UITEST] descriptor 좌표 필드 자동 검증 실패. registry 쓰기 중단.");
          for (int id = 2; id <= 5; ++id) {
            int32_t a=0,b=0,c=0,d=0;
            std::memcpy(&a, descriptors + id*0x60 + 0x04, 4);
            std::memcpy(&b, descriptors + id*0x60 + 0x08, 4);
            std::memcpy(&c, descriptors + id*0x60 + 0x0C, 4);
            std::memcpy(&d, descriptors + id*0x60 + 0x10, 4);
            AddLog(u8"[책략5UITEST] desc%d +4/+8/+C/+10 = %d/%d/%d/%d",
                   id, a,b,c,d);
          }
          return false;
        }

        // Repack existing four buttons and the new ID7 descriptor.
        for (int i = 0; i < 4; ++i) {
          std::memcpy(descriptors + (i + 2) * 0x60 + xField,
                      &compactX[i], sizeof(compactX[i]));
          std::memcpy(descriptors + (i + 2) * 0x60 + yField,
                      &y, sizeof(y));
        }
        std::memcpy(descriptors + 7 * 0x60 + xField,
                    &compactX[4], sizeof(compactX[4]));
        std::memcpy(descriptors + 7 * 0x60 + yField,
                    &y, sizeof(y));

        int32_t types[8] = {};
        for (int id = 0; id < 8; ++id)
          std::memcpy(&types[id],
                      descriptors + id * 0x60,
                      sizeof(types[id]));

        if (types[2] != 0x14 ||
            types[3] != 0x14 ||
            types[4] != 0x14 ||
            types[5] != 0x14 ||
            types[7] != 0x14) {
          AddLog(u8"[책략5UITEST] 버튼 descriptor type 검증 실패: %d/%d/%d/%d/%d",
                 types[2], types[3], types[4], types[5], types[7]);
          return false;
        }

        // IMPORTANT: do not re-initialize the live maker in place.
        // Build a fresh 8-slot maker off to the side, fully validate it,
        // then atomically swap only its 0x28-byte state into layout+0x140.
        alignas(16) uint8_t tempMakerStorage[0x40] = {};
        const uintptr_t tempMaker =
            reinterpret_cast<uintptr_t>(tempMakerStorage);

        stage = 5;
        AddLog(u8"[책략5UITEST] registry 임시 8칸 생성 시작: compact=%d,%d,%d,%d,%d y=%d",
               compactX[0], compactX[1], compactX[2],
               compactX[3], compactX[4], y);
        initLayouts(tempMaker, descriptors, 8, layout);

        uint32_t tempCount = 0;
        uintptr_t tempDesc = 0;
        uintptr_t tempPointers = 0;
        uintptr_t tempOwner = 0;
        if (!SafeReadPtrSeh(tempMaker + 0x00, &tempDesc) ||
            !SafeReadPtrSeh(tempMaker + 0x08, &tempPointers) ||
            !SafeCopySeh(tempMaker + 0x10, &tempCount, sizeof(tempCount)) ||
            !SafeReadPtrSeh(tempMaker + 0x18, &tempOwner) ||
            tempCount != 8 ||
            tempOwner != layout ||
            !tempDesc || !tempPointers ||
            !IsValidPtr(tempDesc, 8 * 0x60) ||
            !IsValidPtr(tempPointers, 8 * sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] 임시 CUIMaker 8칸 생성 검증 실패.");
          return false;
        }

        stage = 6;
        for (int id = 0; id < 7; ++id) {
          if (!registered[id])
            continue;
          registerLayout(tempMaker, id, registered[id], types[id]);
        }

        stage = 7;
        registerLayout(tempMaker, 7, g_fifthUiSidecarButton, types[7]);

        stage = 8;
        const uintptr_t check7 = lookup(tempMaker, 7);
        uint32_t sidecarId = 0;
        uint32_t sidecarState = 0;
        SafeCopySeh(g_fifthUiSidecarButton + 0x88,
                    &sidecarId, sizeof(sidecarId));
        SafeCopySeh(g_fifthUiSidecarButton + 0x8C,
                    &sidecarState, sizeof(sidecarState));

        if (check7 != g_fifthUiSidecarButton ||
            sidecarId != 7 ||
            sidecarState != 1) {
          AddLog(u8"[책략5UITEST] 임시 maker ID7 등록 검증 실패: lookup=%p id=%u state=%u",
                 reinterpret_cast<void *>(check7),
                 (unsigned)sidecarId,
                 (unsigned)sidecarState);
          return false;
        }

        stage = 9;
        std::memcpy(g_fifthUiMakerOriginal,
                    reinterpret_cast<const void *>(maker),
                    sizeof(g_fifthUiMakerOriginal));
        g_fifthUiMakerAddr = maker;

        std::memcpy(reinterpret_cast<void *>(maker),
                    tempMakerStorage,
                    sizeof(g_fifthUiMakerOriginal));

        uint32_t finalCount = 0;
        uintptr_t finalOwner = 0;
        SafeCopySeh(maker + 0x10, &finalCount, sizeof(finalCount));
        SafeReadPtrSeh(maker + 0x18, &finalOwner);
        if (finalCount != 8 || finalOwner != layout) {
          std::memcpy(reinterpret_cast<void *>(maker),
                      g_fifthUiMakerOriginal,
                      sizeof(g_fifthUiMakerOriginal));
          g_fifthUiMakerAddr = 0;
          AddLog(u8"[책략5UITEST] live maker 교체 검증 실패. 원복.");
          return false;
        }

        g_fifthUiMakerExpanded = true;
        AddLog(u8"[책략5UITEST] CUIMaker 7->8 교체 및 ID7 등록 성공: button=%p",
               reinterpret_cast<void *>(g_fifthUiSidecarButton));
        AddLog(u8"[책략5UITEST] 5버튼 압축 배치 적용: x=%d,%d,%d,%d,%d y=%d",
               compactX[0], compactX[1], compactX[2],
               compactX[3], compactX[4], y);
        AddLog(u8"[책략5UITEST] 아직 5번 callback은 미연결입니다. 버튼이 보여도 클릭하지 마세요.");
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_fifthUiMakerAddr &&
            IsValidPtr(g_fifthUiMakerAddr,
                       sizeof(g_fifthUiMakerOriginal))) {
          std::memcpy(reinterpret_cast<void *>(g_fifthUiMakerAddr),
                      g_fifthUiMakerOriginal,
                      sizeof(g_fifthUiMakerOriginal));
        }
        g_fifthUiMakerExpanded = false;
        g_fifthUiMakerAddr = 0;
        AddLog(u8"[책략5UITEST] registry 확장 중 예외 발생: stage=%d. live maker는 기존 상태 유지/복귀.",
               (int)stage);
        return false;
      }
    }

    static bool CreateFifthUiSidecarDisplayOnlySeh(uintptr_t layout) {
      if (!layout || !IsValidPtr(layout, 0x2A8))
        return false;

      if (g_fifthUiSidecarLayout == layout &&
          g_fifthUiSidecarButton &&
          IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return true;

      if (g_fifthUiSidecarLayout != layout) {
        // Previous layout lifetime has ended; do not carry its maker snapshot forward.
        g_fifthUiMakerExpanded = false;
        g_fifthUiMakerAddr = 0;
        std::memset(g_fifthUiMakerOriginal, 0,
                    sizeof(g_fifthUiMakerOriginal));
        g_fifthUiSidecarLayout = layout;
        g_fifthUiSidecarButton = 0;
        g_fifthUiSidecarAttempted = false;
      }

      if (g_fifthUiSidecarAttempted)
        return false;
      g_fifthUiSidecarAttempted = true;

      __try {
        uintptr_t existing0 = 0;
        if (!SafeReadPtrSeh(layout + 0x1E0, &existing0) ||
            !existing0 || !IsValidPtr(existing0, 0x1D8)) {
          AddLog(u8"[책략5UITEST] 기존 1번 버튼 검증 실패. sidecar 생성 중단.");
          return false;
        }

        const uintptr_t exeBase =
            reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
        if (!exeBase)
          return false;

        constexpr uintptr_t kMemoryManagerGetterRva = 0x00014ED0;
        constexpr uintptr_t kTrickButtonCtorRva = 0x01E7FEC0;
        constexpr uintptr_t kTrickButtonInitializeRva = 0x01E7FF40;
        constexpr uintptr_t kSetTrickIdRva = 0x01E7FD90;

        static const uint8_t ctorExpected[] =
            {0x48,0x89,0x4C,0x24,0x08,0x53};
        static const uint8_t initExpected[] =
            {0x48,0x89,0x5C,0x24,0x10,0x48};
        static const uint8_t setExpected[] =
            {0x48,0x89,0x5C,0x24,0x08,0x57};

        if (!IsValidPtr(exeBase + kTrickButtonCtorRva, sizeof(ctorExpected)) ||
            !IsValidPtr(exeBase + kTrickButtonInitializeRva, sizeof(initExpected)) ||
            !IsValidPtr(exeBase + kSetTrickIdRva, sizeof(setExpected)) ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kTrickButtonCtorRva),
                        ctorExpected, sizeof(ctorExpected)) != 0 ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kTrickButtonInitializeRva),
                        initExpected, sizeof(initExpected)) != 0 ||
            std::memcmp(reinterpret_cast<const void *>(exeBase + kSetTrickIdRva),
                        setExpected, sizeof(setExpected)) != 0) {
          AddLog(u8"[책략5UITEST] TrickSelectButton 함수 빌드 가드 불일치. 생성 중단.");
          return false;
        }

        using GetMemoryManagerFn = uintptr_t(__fastcall *)();
        using GameAllocFn = uintptr_t(__fastcall *)(uintptr_t, size_t, void *);
        using ButtonCtorFn = uintptr_t(__fastcall *)(uintptr_t);
        using ButtonInitFn = void(__fastcall *)(uintptr_t, int, int, uintptr_t);
        using SetTrickIdFn = void(__fastcall *)(uintptr_t, uint8_t);
        using SetXYFn = void(__fastcall *)(uintptr_t, int, int);
        using SetBoolFn = void(__fastcall *)(uintptr_t, bool);
        using SetU32Fn = void(__fastcall *)(uintptr_t, uint32_t);
        using SetPairFn = void(__fastcall *)(uintptr_t, uint64_t);

        const auto getMemoryManager =
            reinterpret_cast<GetMemoryManagerFn>(exeBase + kMemoryManagerGetterRva);
        const uintptr_t memoryManager = getMemoryManager();
        if (!memoryManager || !IsValidPtr(memoryManager + 0x118, sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] 게임 메모리 관리자 획득 실패.");
          return false;
        }

        const uintptr_t allocator =
            *reinterpret_cast<const uintptr_t *>(memoryManager + 0x118);
        if (!allocator || !IsValidPtr(allocator, sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] 버튼 allocator 포인터 무효.");
          return false;
        }

        const uintptr_t allocatorVtable =
            *reinterpret_cast<const uintptr_t *>(allocator);
        if (!allocatorVtable ||
            !IsValidPtr(allocatorVtable + 0x28, sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] allocator vtable 무효.");
          return false;
        }

        const uintptr_t allocAddr =
            *reinterpret_cast<const uintptr_t *>(allocatorVtable + 0x28);
        if (!allocAddr || !IsValidPtr(allocAddr, 1)) {
          AddLog(u8"[책략5UITEST] allocator 함수 무효.");
          return false;
        }

        struct AllocTag {
          uint32_t tag;
          uint32_t reserved;
          uintptr_t context;
        };
        AllocTag allocTag{0x35u, 0u, 0u};

        const auto allocFn = reinterpret_cast<GameAllocFn>(allocAddr);
        const uintptr_t raw = allocFn(allocator, 0x1D8, &allocTag);
        if (!raw || !IsValidPtr(raw, 0x1D8)) {
          AddLog(u8"[책략5UITEST] 0x1D8 버튼 메모리 할당 실패.");
          return false;
        }

        const auto ctor =
            reinterpret_cast<ButtonCtorFn>(exeBase + kTrickButtonCtorRva);
        uintptr_t button = ctor(raw);
        if (!button || !IsValidPtr(button, 0x1D8)) {
          AddLog(u8"[책략5UITEST] TrickSelectButton 생성자 실패.");
          return false;
        }

        const int oldStart = static_cast<int>(g_trickUiStartX);
        const int oldStep = static_cast<int>(g_trickUiStep);
        const int compactStep = (oldStep * 11) / 14;
        const int oldCenter = oldStart + (oldStep * 3) / 2;
        const int compactStart = oldCenter - compactStep * 2;
        const int x = compactStart + compactStep * 4;
        const int y = static_cast<int>(g_trickUiY);
        if (g_trickUiStep <= 0 || x < -4096 || x > 8192 ||
            y < -4096 || y > 8192) {
          AddLog(u8"[책략5UITEST] live 좌표 비정상: x=%d y=%d step=%d",
                 x, y, (int)g_trickUiStep);
          return false;
        }

        const auto initialize =
            reinterpret_cast<ButtonInitFn>(exeBase + kTrickButtonInitializeRva);
        initialize(button, x, y, layout);

        // Mirror the proven post-Initialize setup used by Layout::Initialize,
        // but deliberately DO NOT register UI ID 7 yet.
        *reinterpret_cast<uint8_t *>(button + 0x1D4) = 0;
        *reinterpret_cast<uint8_t *>(button + 0x1D5) = 1;

        const int32_t state1C8 =
            *reinterpret_cast<const int32_t *>(button + 0x1C8);
        if (state1C8 != 1) {
          *reinterpret_cast<uint32_t *>(button + 0x40) |= 0x200u;

          uintptr_t vtable =
              *reinterpret_cast<const uintptr_t *>(button);
          if (vtable && IsValidPtr(vtable + 0x270, sizeof(uintptr_t) * 2)) {
            const uintptr_t fn268 =
                *reinterpret_cast<const uintptr_t *>(vtable + 0x268);
            const uintptr_t fn270 =
                *reinterpret_cast<const uintptr_t *>(vtable + 0x270);

            if (fn268 && IsValidPtr(fn268, 1))
              reinterpret_cast<SetU32Fn>(fn268)(button, 0);

            int32_t pair[2] = {};
            if (SafeCopySeh(exeBase + 0x02C3F808, pair, sizeof(pair)) &&
                fn270 && IsValidPtr(fn270, 1)) {
              uint64_t packed = 0;
              std::memcpy(&packed, pair, sizeof(packed));
              reinterpret_cast<SetPairFn>(fn270)(button, packed);
            }
          }

          uintptr_t copyValue = 0;
          if (SafeReadPtrSeh(button + 0x1A8, &copyValue))
            *reinterpret_cast<uintptr_t *>(button + 0x48) = copyValue;
        }

        // Unregistered sentinel ID: avoids collision with the proven 0..6 registry.
        *reinterpret_cast<uint32_t *>(button + 0x88) = 0xFFFFFFFFu;
        *reinterpret_cast<uint32_t *>(button + 0x8C) = 1u;

        const auto setTrickId =
            reinterpret_cast<SetTrickIdFn>(exeBase + kSetTrickIdRva);
        setTrickId(button, 5);

        uintptr_t buttonVtable =
            *reinterpret_cast<const uintptr_t *>(button);
        uintptr_t existingVtable =
            *reinterpret_cast<const uintptr_t *>(existing0);
        if (!buttonVtable || buttonVtable != existingVtable) {
          AddLog(u8"[책략5UITEST] sidecar vtable 불일치: new=%p existing=%p",
                 reinterpret_cast<void *>(buttonVtable),
                 reinterpret_cast<void *>(existingVtable));
          return false;
        }

        if (IsValidPtr(buttonVtable + 0x108, sizeof(uintptr_t))) {
          const uintptr_t setPos =
              *reinterpret_cast<const uintptr_t *>(buttonVtable + 0x90);
          const uintptr_t setVisible =
              *reinterpret_cast<const uintptr_t *>(buttonVtable + 0x108);

          if (setPos && IsValidPtr(setPos, 1))
            reinterpret_cast<SetXYFn>(setPos)(button, x, y);
          if (setVisible && IsValidPtr(setVisible, 1))
            reinterpret_cast<SetBoolFn>(setVisible)(button, true);
        }

        g_fifthUiSidecarLayout = layout;
        g_fifthUiSidecarButton = button;

        AddLog(u8"[책략5UITEST] 표시 전용 5번째 버튼 생성 성공: layout=%p button=%p x=%d y=%d UI-ID=-1 TrickID=5",
               reinterpret_cast<void *>(layout),
               reinterpret_cast<void *>(button),
               x, y);
        AddLog(u8"[책략5UITEST] 아직 registry/callback 미연결 상태입니다. 보이는지만 확인하고 클릭하지 마세요.");
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5UITEST] sidecar 생성 중 예외 발생. 추가 쓰기 중단.");
        return false;
      }
    }

    static void LogUiProbeWindow(const char *name,
                                 uintptr_t funcAddr,
                                 size_t offset,
                                 const uint8_t *bytes,
                                 size_t size) {
      if (!name || !bytes || offset >= size)
        return;

      const size_t from = offset > 12 ? offset - 12 : 0;
      const size_t to = (offset + 24 < size) ? offset + 24 : size;

      char line[512] = {};
      int pos = 0;
      for (size_t i = from; i < to && pos < (int)sizeof(line) - 4; ++i) {
        pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ",
                         (unsigned)bytes[i]);
      }

      AddLog(u8"[책략5UIDBG] %s +%llX @ %p : %s",
             name,
             (unsigned long long)offset,
             reinterpret_cast<void *>(funcAddr + offset),
             line);
    }

    static void LogUiProbeRange(uintptr_t exeBase,
                                uintptr_t imageEnd,
                                const char *name,
                                uintptr_t rva,
                                size_t offset,
                                size_t length) {
      if (!exeBase || !name || !length)
        return;

      const uintptr_t addr = exeBase + rva + offset;
      if (addr < exeBase || addr + length < addr || addr + length > imageEnd ||
          !IsValidPtr(addr, length)) {
        AddLog(u8"[책략5UIRANGE] %s 범위 무효: +%llX len=0x%llX",
               name,
               (unsigned long long)offset,
               (unsigned long long)length);
        return;
      }

      uint8_t bytes[0x400] = {};
      if (length > sizeof(bytes) || !SafeCopySeh(addr, bytes, length)) {
        AddLog(u8"[책략5UIRANGE] %s 읽기 실패: +%llX len=0x%llX",
               name,
               (unsigned long long)offset,
               (unsigned long long)length);
        return;
      }

      AddLog(u8"[책략5UIRANGE] %s 상세범위 +%llX..+%llX",
             name,
             (unsigned long long)offset,
             (unsigned long long)(offset + length));

      for (size_t i = 0; i < length; i += 32) {
        const size_t chunk = ((length - i) > 32) ? 32 : (length - i);
        char line[256] = {};
        int pos = 0;
        for (size_t j = 0; j < chunk && pos < (int)sizeof(line) - 4; ++j) {
          pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ",
                           (unsigned)bytes[i + j]);
        }
        AddLog(u8"[책략5UIRANGE] %s +%llX : %s",
               name,
               (unsigned long long)(offset + i),
               line);
      }
    }

    static void ProbeUiFunction(uintptr_t exeBase,
                                uintptr_t imageEnd,
                                const char *name,
                                uintptr_t rva,
                                size_t size,
                                uintptr_t getTrickButtonAddr,
                                bool dumpWhole) {
      if (!exeBase || !name || size == 0 || size > 0x1100)
        return;

      const uintptr_t addr = exeBase + rva;
      if (addr < exeBase || addr + size < addr || addr + size > imageEnd ||
          !IsValidPtr(addr, size)) {
        AddLog(u8"[책략5UIDBG] %s 범위 무효: RVA=+%llX size=%llu",
               name,
               (unsigned long long)rva,
               (unsigned long long)size);
        return;
      }

      uint8_t bytes[0x1100] = {};
      if (!SafeCopySeh(addr, bytes, size)) {
        AddLog(u8"[책략5UIDBG] %s 코드 읽기 실패: %p",
               name, reinterpret_cast<void *>(addr));
        return;
      }

      AddLog(u8"[책략5UIDBG] %s RVA=+%llX addr=%p size=0x%llX",
             name,
             (unsigned long long)rva,
             reinterpret_cast<void *>(addr),
             (unsigned long long)size);

      if (dumpWhole) {
        for (size_t i = 0; i < size; i += 32) {
          const size_t chunk = ((size - i) > 32) ? 32 : (size - i);
          char line[256] = {};
          int pos = 0;
          for (size_t j = 0; j < chunk && pos < (int)sizeof(line) - 4; ++j) {
            pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ",
                             (unsigned)bytes[i + j]);
          }
          AddLog(u8"[책략5UIDBG] %s bytes +%llX : %s",
                 name, (unsigned long long)i, line);
        }
      }

      int cmpHits = 0;
      int buttonDispHits = 0;
      int getButtonCalls = 0;
      constexpr int kMaxHitsPerKind = 16;

      for (size_t i = 0; i < size; ++i) {
        bool cmpCandidate = false;

        if (i + 2 < size &&
            (bytes[i] == 0x83 || bytes[i] == 0x80) &&
            (bytes[i + 1] & 0x38) == 0x38 &&
            (bytes[i + 2] == 0x03 ||
             bytes[i + 2] == 0x04 ||
             bytes[i + 2] == 0x05)) {
          cmpCandidate = true;
        } else if (i + 1 < size &&
                   bytes[i] == 0x3C &&
                   (bytes[i + 1] == 0x03 ||
                    bytes[i + 1] == 0x04 ||
                    bytes[i + 1] == 0x05)) {
          cmpCandidate = true;
        } else if (i + 4 < size &&
                   bytes[i] == 0x3D &&
                   (bytes[i + 1] == 0x03 ||
                    bytes[i + 1] == 0x04 ||
                    bytes[i + 1] == 0x05) &&
                   bytes[i + 2] == 0x00 &&
                   bytes[i + 3] == 0x00 &&
                   bytes[i + 4] == 0x00) {
          cmpCandidate = true;
        }

        if (cmpCandidate && cmpHits < kMaxHitsPerKind) {
          LogUiProbeWindow(name, addr, i, bytes, size);
          ++cmpHits;
        }

        if (i + 3 < size &&
            bytes[i] == 0xE0 &&
            bytes[i + 1] == 0x01 &&
            bytes[i + 2] == 0x00 &&
            bytes[i + 3] == 0x00 &&
            buttonDispHits < kMaxHitsPerKind) {
          LogUiProbeWindow(name, addr, i, bytes, size);
          ++buttonDispHits;
        }

        if (i + 4 < size && bytes[i] == 0xE8) {
          int32_t rel = 0;
          std::memcpy(&rel, bytes + i + 1, sizeof(rel));
          const uintptr_t target =
              addr + i + 5 + static_cast<intptr_t>(rel);
          if (target == getTrickButtonAddr &&
              getButtonCalls < kMaxHitsPerKind) {
            LogUiProbeWindow(name, addr, i, bytes, size);
            ++getButtonCalls;
          }
        }
      }

      AddLog(u8"[책략5UIDBG] %s 후보 요약: cmp(3/4/5)=%d / +1E0참조=%d / GetTrickButton호출=%d",
             name, cmpHits, buttonDispHits, getButtonCalls);
    }

    static void DumpTrickUiPdbProbe() {
      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return;

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &mi, sizeof(mi))) {
        AddLog(u8"[책략5UIDBG] 모듈 정보 읽기 실패.");
        return;
      }

      const uintptr_t imageEnd =
          exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // 업로드된 SAN8RPK.exe + SAN8RPK.pdb는 RSDS GUID/age가 정확히 일치.
      // PDB:
      // TrickCommandDialogLayout size=0x2A8
      //   +0x1E0 std::array<TrickSelectButton*, 4> m_pButtons
      //   +0x200 다음 멤버(std::function) 시작
      // Initialize local ButtonLayouts = UIMaker::SLayout[4] (0x40 bytes)
      // 반면 WarMeetingStrategyTrickLayout::m_aData는 StrategyTrickData[10].
      constexpr uintptr_t kGetTrickButtonRva = 0x01DAF060;

      AddLog(u8"[책략5UIDBG] PDB 전투 UI 진단 시작: Layout size=0x2A8 / m_pButtons=+1E0 array[4] / next=+200.");
      AddLog(u8"[책략5UIDBG] 준비 UI WarMeetingStrategyTrickLayout은 m_aData[10] 구조. 전투 UI와 별개입니다.");

      struct Probe {
        const char *name;
        uintptr_t rva;
        size_t size;
        bool dumpWhole;
      };

      const Probe probes[] = {
          {u8"Layout::GetTrickButton", 0x01DAF060, 0x13, true},
          {u8"Layout::ResetBtnPos", 0x01DAE9F0, 0x20D, false},
          {u8"Layout::AddControl", 0x01DAF300, 0x3E, true},
          {u8"Layout::DelControl", 0x01DAF2B0, 0x41, true},
          {u8"Layout::Initialize", 0x01DAF350, 0x1019, false},
          {u8"Dialog::Open", 0x01DF3CB0, 0x241, false},
          {u8"Dialog::Initialize", 0x01DF3F20, 0x4C9, false},
          {u8"Dialog::OnTrickSelect", 0x01DF37B0, 0x4C, true},
      };

      const uintptr_t getTrickButtonAddr =
          exeBase + kGetTrickButtonRva;

      for (const auto &probe : probes) {
        ProbeUiFunction(exeBase, imageEnd,
                        probe.name, probe.rva, probe.size,
                        getTrickButtonAddr, probe.dumpWhole);
      }

      // 1차 실게임 로그에서 실제 4제한이 잡힌 구간만 넓게 읽는다.
      // 쓰기 없음. 5번째 버튼 생성/저장 경로를 역추적하기 위한 상세 덤프.
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Layout::ResetBtnPos",
                      0x01DAE9F0, 0x180, 0x8D);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Layout::Initialize",
                      0x01DAF350, 0xB40, 0x320);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Dialog::Open",
                      0x01DF3CB0, 0x70, 0x1D0);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Dialog::Initialize",
                      0x01DF3F20, 0x1D0, 0x200);

      // 2차 로그에서 버튼 생성 루프 자체가 확정됨.
      // 다음은 기존 4개 ButtonLayouts 인자와 TrickSelectButton 생성/초기화 코드를
      // 정확히 확인한다. 모두 read-only 진단.
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Layout::Initialize pre-button setup",
                      0x01DAF350, 0x8E0, 0x260);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"Dialog::Initialize pre-callback setup",
                      0x01DF3F20, 0x000, 0x1D0);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"TrickSelectButton::SetTrickID",
                      0x01E7FD90, 0x000, 0x130);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"TrickSelectButton::ctor",
                      0x01E7FEC0, 0x000, 0x080);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"TrickSelectButton::Initialize",
                      0x01E7FF40, 0x000, 0x160);

      // 4차: ID 0..6이 모두 점유된 것이 확인되어 ID7 확장 가능성을 보기 위해
      // CUIMaker registry 초기화/등록 함수만 좁게 읽는다. 쓰기 없음.
      LogUiProbeRange(exeBase, imageEnd,
                      u8"CUIMaker::InitLayouts",
                      0x01D13E60, 0x000, 0x240);
      LogUiProbeRange(exeBase, imageEnd,
                      u8"CUIMaker::RegisterLayout",
                      0x01D16AA0, 0x000, 0x1C0);

      // Layout::Initialize의 r13가 가리키는 static dword[4].
      // 같은 빌드 EXE .rdata에서도 2,3,4,5가 확인됨.
      constexpr uintptr_t kButtonStaticIdsRva = 0x0270D198;
      uint32_t buttonStaticIds[4] = {};
      if (IsValidPtr(exeBase + kButtonStaticIdsRva, sizeof(buttonStaticIds)) &&
          SafeCopySeh(exeBase + kButtonStaticIdsRva,
                      buttonStaticIds, sizeof(buttonStaticIds))) {
        AddLog(u8"[책략5UISTATIC] Layout button static IDs RVA=+270D198 : %u,%u,%u,%u",
               (unsigned)buttonStaticIds[0],
               (unsigned)buttonStaticIds[1],
               (unsigned)buttonStaticIds[2],
               (unsigned)buttonStaticIds[3]);
      } else {
        AddLog(u8"[책략5UISTATIC] Layout button static IDs 읽기 실패.");
      }

      AddLog(u8"[책략5UIDBG] PDB 전투 UI 4차 진단 완료. UIRANGE/UIMAKER/UIREG 로그를 보내주세요.");
    }

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

        if (c1 == 1 && c2 == 1 && c3 == 1 && c4 == 1 && c5 == 0) {
          if (chosen) {
            AddLog(u8"[책략5슬롯DBG] 공격/수비 양쪽이 모두 1/1/1/1이라 자동 선택할 수 없습니다.");
            return false;
          }
          chosen = candidate.ptr;
          chosenName = candidate.name;
        }
      }

      if (!chosen) {
        AddLog(u8"[책략5슬롯DBG] ID1~4=1/1/1/1, ID5=0인 플레이어측 후보를 찾지 못했습니다.");
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
      g_id5CountOwner = chosen;
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
    g_id5CountOwner = 0;
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

    auto restoreRuntimeSlot = [&]() {
      if (!g_fiveRuntimeSlotApplied)
        return;

      if (g_fiveRuntimeSlotAddr &&
          IsValidPtr(g_fiveRuntimeSlotAddr, sizeof(uintptr_t))) {
        DWORD oldProtect = 0;
        DWORD tmpProtect = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(g_fiveRuntimeSlotAddr),
                           sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
          *reinterpret_cast<uintptr_t *>(g_fiveRuntimeSlotAddr) =
              g_fiveRuntimeSlotOriginal;
          VirtualProtect(reinterpret_cast<LPVOID>(g_fiveRuntimeSlotAddr),
                         sizeof(uintptr_t), oldProtect, &tmpProtect);
        }
      }

      AddLog(u8"[책략5PDBDBG] 5번째 내부 포인터 원복.");
      g_fiveRuntimeSlotApplied = false;
      g_fiveRuntimeSlotAddr = 0;
      g_fiveRuntimeSlotOriginal = 0;
    };

    if (!enable) {
      RestoreFifthUiMakerTestSeh();
      restoreRuntimeSlot();
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      g_trickUiLayout = 0;
      g_trickUiDialog = 0;
      g_trickUiStartX = 0;
      g_trickUiY = 0;
      g_trickUiStep = 0;
      g_lastLoggedUiLayout = 0;
      g_fifthUiSidecarLayout = 0;
      g_fifthUiSidecarButton = 0;
      g_fifthUiSidecarAttempted = false;
      AddLog(u8"[책략5메타DBG] 5번 내부 등록 해제.");
      return true;
    }

    if (g_fiveMetadataApplied && g_fiveRuntimeSlotApplied)
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

    const char *troopTypePat =
        "488B????????????E8????????488B??4883????5BC333??488B??E8????????488B??4883????5BC3E8??????????????????????4053";

    uintptr_t scanStart = exeBase + 0x58E000;
    uintptr_t scanEnd = exeBase + 0x59E000;
    if (scanStart >= imageEnd)
      scanStart = exeBase;
    if (scanEnd > imageEnd)
      scanEnd = imageEnd;

    uintptr_t found = FindPattern(scanStart, scanEnd, troopTypePat);
    if (!found)
      found = FindPattern(exeBase, imageEnd, troopTypePat);

    if (!found || !IsValidPtr(found + 4, sizeof(uint32_t))) {
      AddLog(u8"[책략5메타DBG] CT 원본 troopTypePointerOffset 패턴을 찾지 못했습니다.");
      return false;
    }

    const uint32_t troopTypePointerOffset =
        *reinterpret_cast<const uint32_t *>(found + 4);
    const uintptr_t pointerAddr =
        gameBase + static_cast<uintptr_t>(troopTypePointerOffset);
    if (!IsValidPtr(pointerAddr, sizeof(uintptr_t))) {
      AddLog(u8"[책략5메타DBG] troopType 포인터 주소 무효.");
      return false;
    }

    const uintptr_t troopTypeBase =
        *reinterpret_cast<const uintptr_t *>(pointerAddr);
    const uintptr_t table =
        troopTypeBase + kStratagemMetadataOffset;

    if (!troopTypeBase || troopTypeBase == UINTPTR_MAX ||
        !IsValidPtr(table, kRecordStride * 5)) {
      AddLog(u8"[책략5메타DBG] 책략 테이블 범위 무효: base=%p table=%p",
             reinterpret_cast<void *>(troopTypeBase),
             reinterpret_cast<void *>(table));
      return false;
    }

    for (int i = 0; i < 5; ++i) {
      const uintptr_t row = table + (uintptr_t)i * kRecordStride;
      const uint8_t expected = (uint8_t)(i + 1);
      const uint8_t a = *reinterpret_cast<const uint8_t *>(row + kId1);
      const uint8_t b = *reinterpret_cast<const uint8_t *>(row + kId2);
      const uint8_t d = *reinterpret_cast<const uint8_t *>(row + kId3);
      if (a != expected || b != expected || d != expected) {
        AddLog(u8"[책략5메타DBG] TrickData 검증 실패 row=%d IDs=%u/%u/%u",
               i + 1, (unsigned)a, (unsigned)b, (unsigned)d);
        return false;
      }
    }

    const uintptr_t row1 = table + 0 * kRecordStride;
    const uintptr_t row2 = table + 1 * kRecordStride;
    const uintptr_t row3 = table + 2 * kRecordStride;
    const uintptr_t row4 = table + 3 * kRecordStride;
    const uintptr_t row5 = table + 4 * kRecordStride;

    g_fiveMetadataTable = table;
    g_fiveMetadataAddr = row5;
    g_fiveMetadataApplied = true;

    AddLog(u8"[책략5메타DBG] native TrickData 확인: table=%p row5=%p",
           reinterpret_cast<void *>(table),
           reinterpret_cast<void *>(row5));

    if (!g_id5CountApplied || !g_id5CountOwner) {
      AddLog(u8"[책략5PDBDBG] 먼저 '5번 책략 횟수 1'을 켜서 플레이어측 전장 객체를 확정하세요.");
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    // PDB로 실제 구조 확인:
    // san8r::war::Camp::Impl
    //   +0x10 m_pCampData
    //   +0x18 m_type
    //
    // san8r::war::CampData
    //   +0x68 m_pTricks  (std::array<const TrickData*, 5>)
    //
    // 따라서 더 이상 추정 객체 스캔을 하지 않고 정확한 필드만 읽습니다.
    constexpr uintptr_t kCampImplCampDataOffset = 0x10;
    constexpr uintptr_t kCampImplTypeOffset = 0x18;
    constexpr uintptr_t kCampDataTricksOffset = 0x68;

    uint8_t campType = 0xFF;
    uintptr_t campData = 0;
    if (!IsValidPtr(g_id5CountOwner + kCampImplTypeOffset, 1) ||
        !IsValidPtr(g_id5CountOwner + kCampImplCampDataOffset,
                    sizeof(uintptr_t))) {
      AddLog(u8"[책략5PDBDBG] Camp::Impl 기본 필드가 유효하지 않습니다: impl=%p",
             reinterpret_cast<void *>(g_id5CountOwner));
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    campType =
        *reinterpret_cast<const uint8_t *>(
            g_id5CountOwner + kCampImplTypeOffset);
    if (!SafeReadPtrSeh(g_id5CountOwner + kCampImplCampDataOffset,
                        &campData) ||
        !campData ||
        !IsValidPtr(campData + kCampDataTricksOffset,
                    5 * sizeof(uintptr_t))) {
      AddLog(u8"[책략5PDBDBG] CampData 포인터/5칸 배열 무효: impl=%p type=%u campData=%p",
             reinterpret_cast<void *>(g_id5CountOwner),
             (unsigned)campType,
             reinterpret_cast<void *>(campData));
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    const uintptr_t tricksAddr =
        campData + kCampDataTricksOffset;
    uintptr_t trickPtrs[5] = {};
    if (!SafeCopySeh(tricksAddr, trickPtrs, sizeof(trickPtrs))) {
      AddLog(u8"[책략5PDBDBG] CampData::m_pTricks 읽기 실패: %p",
             reinterpret_cast<void *>(tricksAddr));
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    AddLog(u8"[책략5PDBDBG] Camp::Impl=%p type=%u -> CampData=%p / m_pTricks(+68)=%p",
           reinterpret_cast<void *>(g_id5CountOwner),
           (unsigned)campType,
           reinterpret_cast<void *>(campData),
           reinterpret_cast<void *>(tricksAddr));
    AddLog(u8"[책략5PDBDBG] tricks[0..4]=%p,%p,%p,%p,%p / native rows=%p,%p,%p,%p,%p",
           reinterpret_cast<void *>(trickPtrs[0]),
           reinterpret_cast<void *>(trickPtrs[1]),
           reinterpret_cast<void *>(trickPtrs[2]),
           reinterpret_cast<void *>(trickPtrs[3]),
           reinterpret_cast<void *>(trickPtrs[4]),
           reinterpret_cast<void *>(row1),
           reinterpret_cast<void *>(row2),
           reinterpret_cast<void *>(row3),
           reinterpret_cast<void *>(row4),
           reinterpret_cast<void *>(row5));

    // 첫 4칸이 실제 1~4번 TrickData와 정확히 일치할 때만 5번째를 건드립니다.
    if (trickPtrs[0] != row1 ||
        trickPtrs[1] != row2 ||
        trickPtrs[2] != row3 ||
        trickPtrs[3] != row4) {
      AddLog(u8"[책략5PDBDBG] 첫 4칸이 native 1~4행과 일치하지 않아 쓰기 중단.");
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    const uintptr_t matchedObject = campData;
    const uintptr_t matchedSlot =
        tricksAddr + 4 * sizeof(uintptr_t);
    const uintptr_t matchedOld5 = trickPtrs[4];

    if (matchedOld5 != 0 && matchedOld5 != row5) {
      AddLog(u8"[책략5PDBDBG] 5번째 칸이 0/row5가 아닙니다: %p. 쓰기 중단.",
             reinterpret_cast<void *>(matchedOld5));
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    if (matchedOld5 == row5) {
      g_fiveRuntimeSlotAddr = matchedSlot;
      g_fiveRuntimeSlotOriginal = row5;
      g_fiveRuntimeSlotApplied = true;
      AddLog(u8"[책략5PDBDBG] 5번째 내부 포인터가 이미 row5입니다: camp=%p slot5=%p",
             reinterpret_cast<void *>(matchedObject),
             reinterpret_cast<void *>(matchedSlot));
      if (!EnsureTrickUiLayoutCaptureHook())
        AddLog(u8"[책략5UICAP] layout 캡처 훅은 설치되지 않았습니다.");
      if (!EnsureTrickUiDialogCaptureHook())
        AddLog(u8"[책략5UICAP] dialog 캡처 훅은 설치되지 않았습니다.");
      AddLog(u8"[책략5UIDBG] 기존 PDB 바이트 덤프는 문서화 완료되어 이번 테스트에서는 생략합니다.");
      return true;
    }

    DWORD oldProtect = 0;
    DWORD tmpProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(matchedSlot),
                        sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
      AddLog(u8"[책략5PDBDBG] 5번째 포인터 쓰기 권한 변경 실패.");
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    *reinterpret_cast<uintptr_t *>(matchedSlot) = row5;
    VirtualProtect(reinterpret_cast<LPVOID>(matchedSlot),
                   sizeof(uintptr_t), oldProtect, &tmpProtect);

    if (*reinterpret_cast<const uintptr_t *>(matchedSlot) != row5) {
      AddLog(u8"[책략5PDBDBG] 5번째 포인터 쓰기 검증 실패.");
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      return false;
    }

    g_fiveRuntimeSlotAddr = matchedSlot;
    g_fiveRuntimeSlotOriginal = matchedOld5;
    g_fiveRuntimeSlotApplied = true;

    AddLog(u8"[책략5PDBDBG] 5슬롯 연결 성공: object=%p slot5=%p old=%p new=%p",
           reinterpret_cast<void *>(matchedObject),
           reinterpret_cast<void *>(matchedSlot),
           reinterpret_cast<void *>(matchedOld5),
           reinterpret_cast<void *>(row5));
    if (!EnsureTrickUiLayoutCaptureHook())
      AddLog(u8"[책략5UICAP] layout 캡처 훅은 설치되지 않았습니다.");
    if (!EnsureTrickUiDialogCaptureHook())
      AddLog(u8"[책략5UICAP] dialog 캡처 훅은 설치되지 않았습니다.");
    AddLog(u8"[책략5UIDBG] 기존 PDB 바이트 덤프는 문서화 완료되어 이번 테스트에서는 생략합니다.");
    return true;
  }

  void UpdateStratagemFiveUiRuntimeProbe() {
    if (!g_fiveMetadataApplied || !g_uiLayoutHookApplied)
      return;

    const uintptr_t layout = g_trickUiLayout;
    if (!layout || layout == g_lastLoggedUiLayout)
      return;

    // Confirm the full PDB-known object range before reading fixed fields.
    if (!IsValidPtr(layout, 0x2A8))
      return;

    uintptr_t buttons[4] = {};
    uint32_t controlCount = 0;
    if (!SafeCopySeh(layout + 0x1E0, buttons, sizeof(buttons)) ||
        !SafeCopySeh(layout + 0x150, &controlCount, sizeof(controlCount)))
      return;

    AddLog(u8"[책략5UICAP] Layout=%p controlCount(+150)=%u / buttons=%p,%p,%p,%p",
           reinterpret_cast<void *>(layout),
           (unsigned)controlCount,
           reinterpret_cast<void *>(buttons[0]),
           reinterpret_cast<void *>(buttons[1]),
           reinterpret_cast<void *>(buttons[2]),
           reinterpret_cast<void *>(buttons[3]));
    AddLog(u8"[책략5UICAP] ResetBtnPos live regs: startX=%d y=%d step=%d / fifthX=%d",
           (int)g_trickUiStartX,
           (int)g_trickUiY,
           (int)g_trickUiStep,
           (int)(g_trickUiStartX + g_trickUiStep * 4));

    const uintptr_t dialog = g_trickUiDialog;
    if (dialog && IsValidPtr(dialog, 0x40)) {
      uintptr_t dialogLayout = 0;
      SafeReadPtrSeh(dialog + 0x08, &dialogLayout);
      AddLog(u8"[책략5UICAP] Dialog=%p dialog+08(layout)=%p match=%d",
             reinterpret_cast<void *>(dialog),
             reinterpret_cast<void *>(dialogLayout),
             dialogLayout == layout ? 1 : 0);
    } else {
      AddLog(u8"[책략5UICAP] Dialog 아직 미캡처. 책략창을 닫았다가 다시 여세요.");
    }

    // CUIMaker/registration state begins at layout+0x140; +0x150 is the
    // confirmed control count. Dump only this small fixed region.
    uintptr_t registryQ[20] = {};
    if (SafeCopySeh(layout + 0x140, registryQ, sizeof(registryQ))) {
      for (int row = 0; row < 5; ++row) {
        const int i = row * 4;
        AddLog(u8"[책략5UIREG] +%03X: %p %p %p %p",
               0x140 + i * 8,
               reinterpret_cast<void *>(registryQ[i + 0]),
               reinterpret_cast<void *>(registryQ[i + 1]),
               reinterpret_cast<void *>(registryQ[i + 2]),
               reinterpret_cast<void *>(registryQ[i + 3]));
      }
    }

    // If the first two qwords look like table pointers, search only a bounded
    // 0x400-byte region for the known button pointers and the confirmed ID6 control.
    uintptr_t id6Control = 0;
    SafeReadPtrSeh(layout + 0x290, &id6Control);

    for (int rootIndex = 0; rootIndex < 2; ++rootIndex) {
      const uintptr_t root = registryQ[rootIndex];
      if (!root || !IsValidPtr(root, 0x400))
        continue;

      uintptr_t table[128] = {};
      if (!SafeCopySeh(root, table, sizeof(table)))
        continue;

      AddLog(u8"[책략5UIMAKER] root%d=%p firstQ=%p,%p,%p,%p",
             rootIndex,
             reinterpret_cast<void *>(root),
             reinterpret_cast<void *>(table[0]),
             reinterpret_cast<void *>(table[1]),
             reinterpret_cast<void *>(table[2]),
             reinterpret_cast<void *>(table[3]));

      for (int slot = 0; slot < 128; ++slot) {
        for (int b = 0; b < 4; ++b) {
          if (table[slot] == buttons[b]) {
            AddLog(u8"[책략5UIREG] root%d=%p +%03X -> btn%d=%p",
                   rootIndex,
                   reinterpret_cast<void *>(root),
                   slot * 8,
                   b,
                   reinterpret_cast<void *>(buttons[b]));
          }
        }

        if (id6Control && table[slot] == id6Control) {
          AddLog(u8"[책략5UIREG] root%d=%p +%03X -> ID6 control=%p",
                 rootIndex,
                 reinterpret_cast<void *>(root),
                 slot * 8,
                 reinterpret_cast<void *>(id6Control));
        }
      }
    }

    const uintptr_t extraOffsets[] = {0x280, 0x288, 0x290, 0x2A0};
    for (uintptr_t off : extraOffsets) {
      uintptr_t control = 0;
      if (!SafeReadPtrSeh(layout + off, &control) || !control) {
        AddLog(u8"[책략5UICTRL] layout+%03llX = null",
               (unsigned long long)off);
        continue;
      }

      uintptr_t vtable = 0;
      uint32_t id = 0xFFFFFFFFu;
      uint32_t state = 0xFFFFFFFFu;
      if (IsValidPtr(control, 0x90)) {
        SafeReadPtrSeh(control, &vtable);
        SafeCopySeh(control + 0x88, &id, sizeof(id));
        SafeCopySeh(control + 0x8C, &state, sizeof(state));
      }

      AddLog(u8"[책략5UICTRL] layout+%03llX=%p vtbl=%p id(+88)=%u state(+8C)=%u",
             (unsigned long long)off,
             reinterpret_cast<void *>(control),
             reinterpret_cast<void *>(vtable),
             (unsigned)id,
             (unsigned)state);
    }

    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (exeBase) {
      int32_t ax1 = 0, ay = 0, ax2 = 0;
      int32_t bx1 = 0, by = 0, bx2 = 0;
      const bool aOk =
          SafeCopySeh(exeBase + 0x02C3D300, &ax1, 4) &&
          SafeCopySeh(exeBase + 0x02C3D304, &ay, 4) &&
          SafeCopySeh(exeBase + 0x02C3D320, &ax2, 4);
      const bool bOk =
          SafeCopySeh(exeBase + 0x02C37E30, &bx1, 4) &&
          SafeCopySeh(exeBase + 0x02C37E34, &by, 4) &&
          SafeCopySeh(exeBase + 0x02C37E50, &bx2, 4);
      if (aOk)
        AddLog(u8"[책략5UICAP] ResetPos static A: first=%d second=%d alt=%d delta=%d",
               ax1, ay, ax2, ax2 - ax1);
      if (bOk)
        AddLog(u8"[책략5UICAP] ResetPos static B: first=%d second=%d alt=%d delta=%d",
               bx1, by, bx2, bx2 - bx1);
    }

    for (int i = 0; i < 4; ++i) {
      const uintptr_t button = buttons[i];
      if (!button || !IsValidPtr(button, 0x1D8)) {
        AddLog(u8"[책략5UICAP] btn%d=%p 범위 무효",
               i, reinterpret_cast<void *>(button));
        continue;
      }

      uintptr_t vtable = 0;
      uint32_t flags40 = 0;
      uint32_t fields50[16] = {};
      uint32_t state1C8 = 0;
      uint8_t tail[3] = {};
      SafeReadPtrSeh(button, &vtable);
      SafeCopySeh(button + 0x40, &flags40, sizeof(flags40));
      SafeCopySeh(button + 0x50, fields50, sizeof(fields50));
      SafeCopySeh(button + 0x1C8, &state1C8, sizeof(state1C8));
      SafeCopySeh(button + 0x1D4, tail, sizeof(tail));

      AddLog(u8"[책략5UICAP] btn%d=%p vtbl=%p flags40=%08X id(+88)=%u state(+8C)=%u +1C8=%u tailD4/D5/D6=%u/%u/%u",
             i,
             reinterpret_cast<void *>(button),
             reinterpret_cast<void *>(vtable),
             (unsigned)flags40,
             (unsigned)fields50[14],
             (unsigned)fields50[15],
             (unsigned)state1C8,
             (unsigned)tail[0],
             (unsigned)tail[1],
             (unsigned)tail[2]);

      AddLog(u8"[책략5UICAP] btn%d +50..8C = %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
             i,
             (int32_t)fields50[0], (int32_t)fields50[1],
             (int32_t)fields50[2], (int32_t)fields50[3],
             (int32_t)fields50[4], (int32_t)fields50[5],
             (int32_t)fields50[6], (int32_t)fields50[7],
             (int32_t)fields50[8], (int32_t)fields50[9],
             (int32_t)fields50[10], (int32_t)fields50[11],
             (int32_t)fields50[12], (int32_t)fields50[13],
             (int32_t)fields50[14], (int32_t)fields50[15]);
    }

    if (CreateFifthUiSidecarDisplayOnlySeh(layout))
      ExpandMakerAndRegisterFifthSidecarSeh(layout);

    g_lastLoggedUiLayout = layout;
    AddLog(u8"[책략5UICAP] live layout 캡처 완료.");
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
