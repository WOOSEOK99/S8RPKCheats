#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"

#include <psapi.h>
#include <atomic>
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
    static bool g_fifthUiId7Registered = false;

    // Dialog::Open still skips UI work for loop index >=4 even after ID7 is
    // registered. Keep the fixed m_pButtons[4] array untouched; a narrow hook
    // allows index 4 through and substitutes the external sidecar pointer at
    // the one direct [layout + index*8 + 0x1E0] load.
    static uintptr_t g_fifthUiOpenCmpAddr = 0;
    static uintptr_t g_fifthUiOpenLoadAddr = 0;
    static uintptr_t g_fifthUiOpenSkipTarget = 0;
    static uintptr_t g_fifthUiOpenCaveAddr = 0;
    static uint8_t g_fifthUiOpenOriginalLoad[8] = {};
    static bool g_fifthUiOpenDisplayHookApplied = false;

    static uintptr_t g_fifthUiGetButtonHookAddr = 0;
    static uintptr_t g_fifthUiGetButtonCaveAddr = 0;
    static uint8_t g_fifthUiGetButtonOriginal[5] = {};
    static bool g_fifthUiGetButtonHookApplied = false;

    // Installed before Dialog::Initialize ever reaches its callback loop.
    // The first hook creates/registers the sidecar after Layout::Initialize
    // returns; the second lets the original callback body execute once more
    // with edi==4 while substituting the external sidecar for +0x200.
    static uintptr_t g_fifthUiPreCallbackHookAddr = 0;
    static uintptr_t g_fifthUiPreCallbackCaveAddr = 0;
    static uint8_t g_fifthUiPreCallbackOriginal[8] = {};
    static bool g_fifthUiPreCallbackHookApplied = false;
    static uintptr_t g_fifthUiCallbackLoadHookAddr = 0;
    static uintptr_t g_fifthUiCallbackLoadCaveAddr = 0;
    static uint8_t g_fifthUiCallbackLoadOriginal[8] = {};
    static uintptr_t g_fifthUiCallbackBoundAddr = 0;
    static bool g_fifthUiCallbackLoopHookApplied = false;

    // Pre-initialization bridge: expand only TrickCommandDialogLayout's original
    // CUIMaker::InitLayouts call from 7 descriptors to 8, so the game itself
    // constructs a matching one-shot helper for UI ID7. The input tag and the
    // later RegisterLayout type are separate values (see RE notes section 30).
    static uintptr_t g_trickInitLayoutsCallAddr = 0;
    static uintptr_t g_trickInitLayoutsCaveAddr = 0;
    static uint8_t g_trickInitLayoutsOriginalCall[5] = {};
    static bool g_trickInitLayoutsHookApplied = false;
    static std::atomic<uintptr_t> g_originalInitLayoutsAddr{0};
    static ULONGLONG g_trickInitLayoutsInstallTick = 0;
    static std::atomic<unsigned> g_trickInitLayoutsHits{0};
    static std::atomic<unsigned> g_trickInitLayoutsExpanded{0};
    static std::atomic<uintptr_t> g_lastInitLayoutsOwner{0};

    // AddLog drops early messages while both logging options are off. Retain
    // value copies, never dereference the original stack descriptor later.
    struct TrickUiInitTrace {
      unsigned hit;
      ULONGLONG tick;
      DWORD thread;
      const char *reason;
      uintptr_t maker, descriptors, owner;
      int count;
      bool inputCopied;
      uint32_t inputHead[7][8]; // first 0x20 bytes at each observed 0x60 stride
      bool resultReturned;
      bool resultRead;
      uint32_t resultCount;
      uintptr_t resultOwner, resultTable, resultDescriptors, helper7;
      uintptr_t helperVtables[5]; // IDs 2,3,4,5,7, before original registration
      uint32_t resultHead[5][5];
      bool helperClassMatched;
      DWORD exceptionCode;
    };
    static SRWLOCK g_trickUiInitTraceLock = SRWLOCK_INIT;
    static TrickUiInitTrace g_trickUiInitTrace{};

    static void SaveTrickUiInitTrace(const TrickUiInitTrace &trace) {
      AcquireSRWLockExclusive(&g_trickUiInitTraceLock);
      if (trace.hit >= g_trickUiInitTrace.hit)
        g_trickUiInitTrace = trace;
      ReleaseSRWLockExclusive(&g_trickUiInitTraceLock);
    }

    static TrickUiInitTrace ReadTrickUiInitTrace() {
      TrickUiInitTrace trace{};
      AcquireSRWLockShared(&g_trickUiInitTraceLock);
      trace = g_trickUiInitTrace;
      ReleaseSRWLockShared(&g_trickUiInitTraceLock);
      return trace;
    }

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

    static void LogUiProbeRange(uintptr_t exeBase,
                                uintptr_t imageEnd,
                                const char *name,
                                uintptr_t rva,
                                size_t offset,
                                size_t length);


    static bool CreateFifthUiSidecarDisplayOnlySeh(uintptr_t layout);
    static bool ExpandMakerAndRegisterFifthSidecarSeh(uintptr_t layout);
    static bool ValidatePreparedFifthUiHelper(
        uintptr_t layout, uint32_t *descriptorTag = nullptr);


    // Read only PE headers and the bounded CodeView directory, never scan memory.
    // The timestamp alone is insufficient for these build-specific RVAs.
    static bool IsSupportedTrickUiBuild(uintptr_t exeBase) {
      MODULEINFO module{};
      IMAGE_DOS_HEADER dos{};
      IMAGE_NT_HEADERS64 nt{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &module, sizeof(module)) ||
          !SafeCopySeh(exeBase, &dos, sizeof(dos)) ||
          dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
          module.SizeOfImage < sizeof(nt) ||
          static_cast<size_t>(dos.e_lfanew) > module.SizeOfImage - sizeof(nt) ||
          !SafeCopySeh(exeBase + dos.e_lfanew, &nt, sizeof(nt)) ||
          nt.Signature != IMAGE_NT_SIGNATURE ||
          nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
          nt.FileHeader.TimeDateStamp != 0x69A67ED1 ||
          nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
          nt.OptionalHeader.SizeOfImage != module.SizeOfImage ||
          nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG ||
          module.SizeOfImage <= 0x01E7FF40 + 0x100)
        return false;

      const auto &debug = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
      if (!debug.VirtualAddress || !debug.Size ||
          debug.Size % sizeof(IMAGE_DEBUG_DIRECTORY) != 0 ||
          debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY) > 64 ||
          debug.VirtualAddress >= module.SizeOfImage ||
          debug.Size > module.SizeOfImage - debug.VirtualAddress)
        return false;

      struct CodeViewId { DWORD signature; GUID guid; DWORD age; };
      static_assert(sizeof(CodeViewId) == 24);
      const GUID expected = {0xB18A027E, 0x19F4, 0x4C35,
                             {0x87, 0x49, 0x5B, 0x6F, 0x0F, 0xF8, 0x14, 0xD8}};
      for (DWORD offset = 0; offset < debug.Size; offset += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        IMAGE_DEBUG_DIRECTORY entry{};
        CodeViewId id{};
        if (!SafeCopySeh(exeBase + debug.VirtualAddress + offset, &entry, sizeof(entry)))
          return false;
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW)
          continue;
        if (!entry.AddressOfRawData || entry.SizeOfData < sizeof(id) ||
            entry.AddressOfRawData >= module.SizeOfImage ||
            entry.SizeOfData > module.SizeOfImage - entry.AddressOfRawData ||
            !SafeCopySeh(exeBase + entry.AddressOfRawData, &id, sizeof(id)))
          return false;
        if (id.signature == 0x53445352 && id.age == 1 &&
            std::memcmp(&id.guid, &expected, sizeof(expected)) == 0)
          return true;
      }
      return false;
    }

    static void LogTrickUiBridgeStatus(const char *context) {
      AddLog(u8"[책략5UIHELPER] 상태(%s): installed=%d installTick=%llu hits=%u expanded=%u lastOwner=%p",
             context, g_trickInitLayoutsHookApplied ? 1 : 0,
             g_trickInitLayoutsInstallTick, g_trickInitLayoutsHits.load(),
             g_trickInitLayoutsExpanded.load(),
             reinterpret_cast<void *>(g_lastInitLayoutsOwner.load()));

      const TrickUiInitTrace trace = ReadTrickUiInitTrace();
      if (!trace.hit)
        return;
      AddLog(u8"[책략5UIINIT] 저장된 초기 호출(%s): hit=%u reason=%s tick=%llu thread=%lu maker=%p owner=%p owner+140일치=%d descriptors=%p count=%d",
             context, trace.hit, trace.reason, trace.tick, trace.thread,
             reinterpret_cast<void *>(trace.maker), reinterpret_cast<void *>(trace.owner),
             trace.owner && trace.maker == trace.owner + 0x140 ? 1 : 0,
             reinterpret_cast<void *>(trace.descriptors), trace.count);
      if (trace.inputCopied) {
        for (unsigned row = 0; row < 7; ++row) {
          const uint32_t *v = trace.inputHead[row];
          AddLog(u8"[책략5UIINIT] 초기 입력 +%03X (stride=60, 앞20): %08X %08X %08X %08X %08X %08X %08X %08X",
                 row * 0x60, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        }
      }
      if (trace.resultReturned)
        AddLog(u8"[책략5UIINIT] 초기 확장 반환: readOk=%d count=%u owner=%p table=%p helper7=%p",
               trace.resultRead ? 1 : 0, trace.resultCount, reinterpret_cast<void *>(trace.resultOwner),
               reinterpret_cast<void *>(trace.resultTable), reinterpret_cast<void *>(trace.helper7));
      if (trace.resultReturned) {
        AddLog(u8"[책략5UIINIT] 초기 helper vtbl(ID2/3/4/5/7)=%p,%p,%p,%p,%p match=%d",
               reinterpret_cast<void *>(trace.helperVtables[0]), reinterpret_cast<void *>(trace.helperVtables[1]),
               reinterpret_cast<void *>(trace.helperVtables[2]), reinterpret_cast<void *>(trace.helperVtables[3]),
               reinterpret_cast<void *>(trace.helperVtables[4]), trace.helperClassMatched ? 1 : 0);
        AddLog(u8"[책략5UIINIT] 등록 전 descriptor 첫값(ID2/3/4/5/7)=%u,%u,%u,%u,%u / 등록 인자 type=20",
               trace.resultHead[0][0], trace.resultHead[1][0], trace.resultHead[2][0],
               trace.resultHead[3][0], trace.resultHead[4][0]);
      }
      if (trace.exceptionCode)
        AddLog(u8"[책략5UIINIT] 초기 확장 예외: code=%08X", static_cast<unsigned>(trace.exceptionCode));
    }

    static int LogTrickUiInitException(DWORD code, TrickUiInitTrace *trace) {
      trace->reason = "init-exception";
      trace->exceptionCode = code;
      SaveTrickUiInitTrace(*trace);
      AddLog(u8"[책략5UIHELPER] 확장 InitLayouts 예외: code=%08X. 부분 초기화 maker 재호출 금지; 예외 전파.",
             static_cast<unsigned>(code));
      return EXCEPTION_CONTINUE_SEARCH;
    }

    static void __fastcall TrickUiInitLayoutsBridge(uintptr_t maker,
                                                    const void *descriptors,
                                                    int count,
                                                    uintptr_t owner) {
      using InitLayoutsFn =
          void(__fastcall *)(uintptr_t, const void *, int, uintptr_t);

      const unsigned hit = ++g_trickInitLayoutsHits;
      TrickUiInitTrace trace{};
      trace.hit = hit;
      trace.tick = GetTickCount64();
      trace.thread = GetCurrentThreadId();
      trace.reason = "entered";
      trace.maker = maker;
      trace.descriptors = reinterpret_cast<uintptr_t>(descriptors);
      trace.owner = owner;
      trace.count = count;
      SaveTrickUiInitTrace(trace);
      g_lastInitLayoutsOwner.store(owner);

      const auto original =
          reinterpret_cast<InitLayoutsFn>(g_originalInitLayoutsAddr.load());
      if (!original) {
        trace.reason = "missing-original";
        SaveTrickUiInitTrace(trace);
        return;
      }
      AddLog(u8"[책략5UIHELPER] 브리지 진입: hit=%u tick=%llu thread=%lu maker=%p owner=%p count=%d",
             hit, GetTickCount64(), GetCurrentThreadId(),
             reinterpret_cast<void *>(maker), reinterpret_cast<void *>(owner), count);

      const char *argumentFailure = nullptr;
      if (!owner || !IsValidPtr(owner, 0x2A8))
        argumentFailure = "owner-range";
      else if (maker != owner + 0x140)
        argumentFailure = "maker-owner";
      else if (count != 7)
        argumentFailure = "input-count";
      else if (!descriptors || !IsValidPtr(trace.descriptors, 7 * 0x60))
        argumentFailure = "descriptor-range";
      if (argumentFailure) {
        trace.reason = argumentFailure;
        SaveTrickUiInitTrace(trace);
        AddLog(u8"[책략5UIHELPER] 인자 검증 실패: reason=%s. 원본 인자로 1회 통과.", argumentFailure);
        original(maker, descriptors, count, owner);
        return;
      }

      alignas(16) uint8_t expanded[8 * 0x60] = {};
      if (!SafeCopySeh(reinterpret_cast<uintptr_t>(descriptors),
                       expanded, 7 * 0x60)) {
        trace.reason = "descriptor-copy";
        SaveTrickUiInitTrace(trace);
        AddLog(u8"[책략5UIHELPER] descriptor 복사 실패. 원본 인자로 1회 통과.");
        original(maker, descriptors, count, owner);
        return;
      }
      trace.inputCopied = true;
      for (unsigned row = 0; row < 7; ++row)
        std::memcpy(trace.inputHead[row], expanded + row * 0x60, sizeof(trace.inputHead[row]));

      // Exact input prefixes captured on 2026-09-23. The first dword for the
      // buttons is ZERO, not the later RegisterLayout argument 0x14. Validate
      // all seven tag/rectangle prefixes; bytes after +0x14 include unknowns
      // and are copied unchanged, not treated as stable padding or IDs.
      static const uint32_t expectedInput[7][5] = {
          {12, 260, 314, 1400, 420}, {13, 260, 75, 1400, 659},
          {0, 406, 364, 268, 148}, {0, 686, 364, 268, 148},
          {0, 966, 364, 268, 148}, {0, 1246, 364, 268, 148},
          {0, 480, 536, 960, 152}
      };
      for (unsigned row = 0; row < 7; ++row) {
        if (std::memcmp(trace.inputHead[row], expectedInput[row], sizeof(expectedInput[row])) != 0) {
          trace.reason = "descriptor-input-shape";
          SaveTrickUiInitTrace(trace);
          AddLog(u8"[책략5UIHELPER] 초기 입력 형식 불일치: row=%u tag=%u rect=%u/%u/%u/%u. 원본 7칸 통과.",
                 row, trace.inputHead[row][0], trace.inputHead[row][1], trace.inputHead[row][2],
                 trace.inputHead[row][3], trace.inputHead[row][4]);
          original(maker, descriptors, count, owner);
          return;
        }
      }

      std::memcpy(expanded + 7 * 0x60,
                  expanded + 5 * 0x60,
                  0x60);
      AddLog(u8"[책략5UIHELPER] 초기 입력 형식 검증 성공: 버튼 tag=0, 등록 type=20. ID5 입력을 ID7로 그대로 복제.");

      trace.reason = "calling-expanded";
      SaveTrickUiInitTrace(trace);
      __try {
        original(maker, expanded, 8, owner);
      } __except (LogTrickUiInitException(GetExceptionCode(), &trace)) {
        // Never retry InitLayouts on a possibly partially initialized maker.
      }

      uint32_t newCount = 0;
      uintptr_t pointerTable = 0;
      uintptr_t helper7 = 0;
      trace.resultReturned = true;
      trace.resultRead = SafeCopySeh(maker + 0x10, &newCount, sizeof(newCount));
      trace.resultRead = SafeReadPtrSeh(maker + 0x08, &pointerTable) && trace.resultRead;
      trace.resultRead = SafeReadPtrSeh(maker + 0x18, &trace.resultOwner) && trace.resultRead;
      trace.resultRead = SafeReadPtrSeh(maker + 0x00, &trace.resultDescriptors) && trace.resultRead;
      if (newCount == 8 && pointerTable && IsValidPtr(pointerTable, 8 * sizeof(uintptr_t)))
        SafeReadPtrSeh(pointerTable + 7 * sizeof(uintptr_t), &helper7);

      trace.resultCount = newCount;
      trace.resultTable = pointerTable;
      trace.helper7 = helper7;

      if (!trace.resultRead || trace.resultOwner != owner || newCount != 8 ||
          !helper7 || !IsValidPtr(helper7, sizeof(uintptr_t)) ||
          !IsValidPtr(trace.resultDescriptors, 8 * 0x60)) {
        trace.reason = "expanded-result";
        SaveTrickUiInitTrace(trace);
        AddLog(u8"[책략5UIHELPER] 확장 결과 검증 실패: count=%u helper7=%p. 재초기화하지 않습니다.",
               static_cast<unsigned>(newCount), reinterpret_cast<void *>(helper7));
        return;
      }

      // Observe the helpers while the original IDs 2..5 are still unconsumed.
      // Prove the clone created the same helper class before allowing ID7 use.
      const unsigned ids[5] = {2, 3, 4, 5, 7};
      uintptr_t helpers[5] = {};
      bool helpersMatch = true;
      for (unsigned i = 0; i < 5; ++i) {
        if (!SafeReadPtrSeh(pointerTable + ids[i] * sizeof(uintptr_t), &helpers[i]) ||
            !helpers[i] || !SafeReadPtrSeh(helpers[i], &trace.helperVtables[i]) ||
            !trace.helperVtables[i] || !IsValidPtr(trace.helperVtables[i], sizeof(uintptr_t)) ||
            !SafeCopySeh(trace.resultDescriptors + ids[i] * 0x60, trace.resultHead[i], sizeof(trace.resultHead[i])))
          helpersMatch = false;
      }
      for (unsigned i = 0; i < 4; ++i) {
        if (helpers[i] == helper7 || trace.helperVtables[i] != trace.helperVtables[4])
          helpersMatch = false;
      }
      if (std::memcmp(trace.resultHead[3], trace.resultHead[4], sizeof(trace.resultHead[4])) != 0)
        helpersMatch = false;
      trace.helperClassMatched = helpersMatch;
      if (!helpersMatch) {
        trace.reason = "helper-class-mismatch";
        SaveTrickUiInitTrace(trace);
        LogTrickUiBridgeStatus("helper-check");
        return;
      }
      ++g_trickInitLayoutsExpanded;
      trace.reason = "expanded-ok";
      SaveTrickUiInitTrace(trace);

      AddLog(u8"[책략5UIHELPER] 초기 InitLayouts 7->8 완료: maker=%p owner=%p count=%u helper7=%p",
             reinterpret_cast<void *>(maker),
             reinterpret_cast<void *>(owner),
             (unsigned)newCount,
             reinterpret_cast<void *>(helper7));
      AddLog(u8"[책략5UIHELPER] ID7 helper 클래스 검증 성공: 기존 ID2~5와 vtbl 일치.");
    }

    static bool EnsureTrickUiInitLayoutsBridgeHook(bool reportFailure = true) {
      if (g_trickInitLayoutsHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;
      if (!IsSupportedTrickUiBuild(exeBase)) {
        if (reportFailure)
          AddLog(u8"[책략5UIHELPER] PE/RSDS 빌드 가드 불일치 또는 아직 준비 안 됨. 쓰기 중단.");
        return false;
      }

      constexpr uintptr_t kLayoutInitializeRva = 0x01DAF350;
      constexpr uintptr_t kInitLayoutsRva = 0x01D13E60;
      const uintptr_t originalTarget = exeBase + kInitLayoutsRva;
      const uintptr_t scanStart =
          exeBase + kLayoutInitializeRva + 0xB20;
      const uintptr_t scanEnd =
          exeBase + kLayoutInitializeRva + 0xB80;

      uint8_t callWindow[0x60] = {};
      if (!IsValidPtr(scanStart, sizeof(callWindow)) ||
          !SafeCopySeh(scanStart, callWindow, sizeof(callWindow)))
        return false;

      uintptr_t callSite = 0;
      unsigned matches = 0;
      for (uintptr_t p = scanStart; p + 5 <= scanEnd; ++p) {
        if (callWindow[p - scanStart] != 0xE8)
          continue;

        int32_t rel = 0;
        std::memcpy(&rel,
                    callWindow + (p - scanStart) + 1,
                    sizeof(rel));
        const uintptr_t target =
            static_cast<uintptr_t>(
                static_cast<intptr_t>(p + 5) +
                static_cast<intptr_t>(rel));
        if (target == originalTarget) {
          callSite = p;
          ++matches;
        }
      }

      if (matches != 1) {
        if (reportFailure)
          AddLog(u8"[책략5UIHELPER] Layout::Initialize의 InitLayouts call 검증 실패: matches=%u", matches);
        return false;
      }

      // The original setup immediately before this call must contain
      // "mov r8d,7"; otherwise do not patch this build.
      bool countSevenFound = false;
      const uintptr_t verifyStart =
          (callSite >= scanStart + 24) ? callSite - 24 : scanStart;
      for (uintptr_t p = verifyStart; p + 6 <= callSite; ++p) {
        static const uint8_t movR8d7[6] =
            {0x41,0xB8,0x07,0x00,0x00,0x00};
        if (std::memcmp(callWindow + (p - scanStart),
                        movR8d7, sizeof(movR8d7)) == 0) {
          countSevenFound = true;
          break;
        }
      }
      if (!countSevenFound) {
        if (reportFailure)
          AddLog(u8"[책략5UIHELPER] InitLayouts call 직전 count=7 검증 실패.");
        return false;
      }

      const uintptr_t cave = AllocNear(callSite, 64);
      if (!cave)
        return false;

      uint8_t *c = reinterpret_cast<uint8_t *>(cave);
      int i = 0;
      auto emit8 = [&](uint8_t v) { c[i++] = v; };
      auto emit64 = [&](uintptr_t v) {
        std::memcpy(c + i, &v, sizeof(v));
        i += 8;
      };

      // Tail-jump keeps the game's return address/shadow space and needs no
      // synthetic stack frame (or unwind metadata) in the allocated cave.
      emit8(0x48); emit8(0xB8);                            // mov rax,imm64
      emit64(reinterpret_cast<uintptr_t>(&TrickUiInitLayoutsBridge));
      emit8(0xFF); emit8(0xE0);                            // jmp rax
      FlushInstructionCache(GetCurrentProcess(), c, i);

      const intptr_t callRel =
          static_cast<intptr_t>(cave) -
          static_cast<intptr_t>(callSite + 5);
      if (callRel < INT32_MIN || callRel > INT32_MAX) {
        VirtualFree(reinterpret_cast<LPVOID>(cave), 0, MEM_RELEASE);
        return false;
      }

      uint8_t patch[5] = {0xE8,0,0,0,0};
      const int32_t rel32 = static_cast<int32_t>(callRel);
      std::memcpy(patch + 1, &rel32, sizeof(rel32));

      std::memcpy(g_trickInitLayoutsOriginalCall,
                  callWindow + (callSite - scanStart), 5);

      uint8_t currentCall[5] = {};
      if (!SafeCopySeh(callSite, currentCall, sizeof(currentCall)) ||
          std::memcmp(currentCall, g_trickInitLayoutsOriginalCall, sizeof(currentCall)) != 0) {
        VirtualFree(reinterpret_cast<LPVOID>(cave), 0, MEM_RELEASE);
        return false;
      }

      DWORD oldProtect=0, tmpProtect=0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(callSite), 5,
                          PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(reinterpret_cast<LPVOID>(cave), 0, MEM_RELEASE);
        return false;
      }

      // Existing raw capture hooks also retain DLL addresses. Keep this
      // experimental DLL alive until process exit, including in-flight calls.
      HMODULE pinnedModule = nullptr;
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(&TrickUiInitLayoutsBridge),
                             &pinnedModule)) {
        VirtualProtect(reinterpret_cast<LPVOID>(callSite), 5, oldProtect, &tmpProtect);
        VirtualFree(reinterpret_cast<LPVOID>(cave), 0, MEM_RELEASE);
        return false;
      }

      // Publish the original target BEFORE the call site can reach the bridge.
      g_originalInitLayoutsAddr = originalTarget;
      g_trickInitLayoutsInstallTick = GetTickCount64();
      std::memcpy(reinterpret_cast<void *>(callSite), patch, 5);
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<void *>(callSite), 5);
      VirtualProtect(reinterpret_cast<LPVOID>(callSite), 5,
                     oldProtect, &tmpProtect);

      g_trickInitLayoutsCallAddr = callSite;
      g_trickInitLayoutsCaveAddr = cave;
      g_trickInitLayoutsHookApplied = true;

      AddLog(u8"[책략5UIHELPER] 책략 UI 초기 InitLayouts 7->8 브리지 설치 완료: callRVA=%llX tick=%llu thread=%lu",
             static_cast<unsigned long long>(callSite - exeBase),
             g_trickInitLayoutsInstallTick, GetCurrentThreadId());
      return true;
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


    static bool EnsureFifthUiGetTrickButtonHook() {
      if (g_fifthUiGetButtonHookApplied)
        return true;
      if (!g_fifthUiId7Registered ||
          !g_fifthUiSidecarButton ||
          !IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return false;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kGetTrickButtonRva = 0x01DAF060;
      const uintptr_t addr = exeBase + kGetTrickButtonRva;

      // Entire 0x13-byte function from this exact supported build:
      // cmp edx,4 / jae out / mov eax,edx /
      // mov rax,[rcx+rax*8+1E0] / ret / xor eax,eax / ret
      static const uint8_t expected[0x13] = {
          0x83,0xFA,0x04,
          0x73,0x0B,
          0x8B,0xC2,
          0x48,0x8B,0x84,0xC1,0xE0,0x01,0x00,0x00,
          0xC3,
          0x33,0xC0,
          0xC3
      };
      if (!IsValidPtr(addr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(addr),
                      expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략5UIGET] GetTrickButton 전체 바이트 검증 실패.");
        return false;
      }

      const uintptr_t caveAddr = AllocNear(addr, 96);
      if (!caveAddr)
        return false;

      uint8_t *c = reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){ c[i++]=v; };
      auto e32=[&](int32_t v){ std::memcpy(c+i,&v,4); i+=4; };
      auto e64=[&](uintptr_t v){ std::memcpy(c+i,&v,8); i+=8; };
      auto patchRel32=[&](int at,int target){
        const int64_t rel=(int64_t)target-(int64_t)(at+4);
        if(rel<INT32_MIN||rel>INT32_MAX) return false;
        const int32_t v=(int32_t)rel;
        std::memcpy(c+at,&v,4);
        return true;
      };

      e8(0x83); e8(0xFA); e8(0x04);             // cmp edx,4
      e8(0x0F); e8(0x84);                      // je sidecar
      const int jeSide=i; e32(0);
      e8(0x0F); e8(0x87);                      // ja out
      const int jaOut=i; e32(0);

      // indices 0..3: exact original access
      e8(0x8B); e8(0xC2);                      // mov eax,edx
      const uint8_t loadOrig[8]=
          {0x48,0x8B,0x84,0xC1,0xE0,0x01,0x00,0x00};
      std::memcpy(c+i,loadOrig,sizeof(loadOrig)); i+=(int)sizeof(loadOrig);
      e8(0xC3);                                // ret

      const int sideLabel=i;
      e8(0x48); e8(0xB8);                      // mov rax,&global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiSidecarButton));
      e8(0x48); e8(0x8B); e8(0x00);            // mov rax,[rax]
      e8(0xC3);                                // ret

      const int outLabel=i;
      e8(0x33); e8(0xC0);                      // xor eax,eax
      e8(0xC3);                                // ret

      if(!patchRel32(jeSide,sideLabel) ||
         !patchRel32(jaOut,outLabel)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiGetButtonOriginal,
                  reinterpret_cast<const void *>(addr),
                  sizeof(g_fifthUiGetButtonOriginal));
      if(!ApplyJmp(addr,caveAddr,5)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      g_fifthUiGetButtonHookAddr=addr;
      g_fifthUiGetButtonCaveAddr=caveAddr;
      g_fifthUiGetButtonHookApplied=true;
      AddLog(u8"[책략5UIGET] GetTrickButton 완전 대체 성공: index4 -> sidecar.");
      return true;
    }

    static void LogFifthUiCallbackLoopCandidate() {
      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return;

      constexpr uintptr_t kDialogInitializeRva=0x01DF3F20;
      constexpr size_t kSize=0x4C9;
      const uintptr_t fn=exeBase+kDialogInitializeRva;
      uint8_t bytes[kSize]={};
      if(!SafeCopySeh(fn,bytes,sizeof(bytes)))
        return;

      // Proven loop tail from prior RE:
      // inc edi ; add r14,8 ; cmp edi,4 ; jb loop
      static const uint8_t prefix[]={
          0xFF,0xC7,0x49,0x83,0xC6,0x08,0x83,0xFF,0x04
      };
      unsigned found=0;
      size_t hit=0;
      for(size_t p=0;p+sizeof(prefix)+2<=sizeof(bytes);++p){
        if(std::memcmp(bytes+p,prefix,sizeof(prefix))==0){
          ++found; hit=p;
        }
      }
      AddLog(u8"[책략5UICB] Dialog::Initialize callback-tail candidates=%u",found);
      if(found==1){
        const size_t from=hit>0x50?hit-0x50:0;
        const size_t to=(hit+0x30<sizeof(bytes))?hit+0x30:sizeof(bytes);
        for(size_t p=from;p<to;p+=0x20){
          const size_t chunk=(to-p>0x20)?0x20:(to-p);
          char line[256]={}; int pos=0;
          for(size_t j=0;j<chunk && pos<(int)sizeof(line)-4;++j)
            pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)bytes[p+j]);
          AddLog(u8"[책략5UICB] Dialog::Initialize +%llX : %s",
                 (unsigned long long)p,line);
        }
      }
    }

    static bool EnsureFifthUiOpenDisplayHook() {
      if (g_fifthUiOpenDisplayHookApplied)
        return true;
      if (!g_fifthUiId7Registered ||
          !g_fifthUiSidecarButton ||
          !IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return false;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kDialogOpenRva = 0x01DF3CB0;
      constexpr size_t kDialogOpenSize = 0x241;
      const uintptr_t openAddr = exeBase + kDialogOpenRva;
      uint8_t code[kDialogOpenSize] = {};
      if (!IsValidPtr(openAddr, sizeof(code)) ||
          !SafeCopySeh(openAddr, code, sizeof(code)))
        return false;

      static const uint8_t kButtonLoad[8] = {
          0x48,0x8B,0xB4,0xF0,0xE0,0x01,0x00,0x00
      };

      uintptr_t cmpAddr = 0;
      uintptr_t loadAddr = 0;
      uintptr_t skipTarget = 0;
      unsigned candidates = 0;

      // Find only the proven Open pattern:
      //   cmp edi,4
      //   jae <skip button UI>
      //   ... mov rsi,[rax+rsi*8+1E0]
      // No broad scan and no global "4 -> 5" patch.
      for (size_t i = 0; i + 9 < sizeof(code); ++i) {
        if (code[i] != 0x83 || code[i + 1] != 0xFF || code[i + 2] != 0x04)
          continue;

        size_t branchEnd = 0;
        uintptr_t target = 0;
        if (code[i + 3] == 0x73) {
          const int8_t rel8 = static_cast<int8_t>(code[i + 4]);
          branchEnd = i + 5;
          target = openAddr + branchEnd + rel8;
        } else if (code[i + 3] == 0x0F && code[i + 4] == 0x83) {
          int32_t rel32 = 0;
          std::memcpy(&rel32, code + i + 5, sizeof(rel32));
          branchEnd = i + 9;
          target = openAddr + branchEnd + static_cast<intptr_t>(rel32);
        } else {
          continue;
        }

        const size_t searchEnd =
            (branchEnd + 0x70 < sizeof(code)) ? branchEnd + 0x70 : sizeof(code);
        size_t foundLoad = SIZE_MAX;
        for (size_t j = branchEnd; j + sizeof(kButtonLoad) <= searchEnd; ++j) {
          if (std::memcmp(code + j, kButtonLoad, sizeof(kButtonLoad)) == 0) {
            if (foundLoad != SIZE_MAX) {
              foundLoad = SIZE_MAX;
              break; // ambiguous within this candidate
            }
            foundLoad = j;
          }
        }
        if (foundLoad == SIZE_MAX)
          continue;
        if (target < openAddr || target >= openAddr + sizeof(code))
          continue;

        ++candidates;
        cmpAddr = openAddr + i;
        loadAddr = openAddr + foundLoad;
        skipTarget = target;
      }

      if (candidates != 1 || !cmpAddr || !loadAddr || !skipTarget) {
        AddLog(u8"[책략5UIOPEN] Open index4 후보 검증 실패: candidates=%u",
               candidates);
        return false;
      }

      uint8_t cmpNow[3] = {};
      uint8_t loadNow[8] = {};
      if (!SafeCopySeh(cmpAddr, cmpNow, sizeof(cmpNow)) ||
          !SafeCopySeh(loadAddr, loadNow, sizeof(loadNow)) ||
          cmpNow[0] != 0x83 || cmpNow[1] != 0xFF || cmpNow[2] != 0x04 ||
          std::memcmp(loadNow, kButtonLoad, sizeof(loadNow)) != 0) {
        AddLog(u8"[책략5UIOPEN] Open 패치 직전 바이트 재검증 실패.");
        return false;
      }

      const uintptr_t caveAddr = AllocNear(loadAddr, 128);
      if (!caveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(caveAddr);
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
      auto patchRel32 = [&](int dispIndex, int targetIndex) {
        const int64_t rel =
            static_cast<int64_t>(targetIndex) -
            static_cast<int64_t>(dispIndex + 4);
        if (rel < INT32_MIN || rel > INT32_MAX)
          return false;
        const int32_t v = static_cast<int32_t>(rel);
        std::memcpy(cave + dispIndex, &v, sizeof(v));
        return true;
      };
      auto emitExternalJmp = [&](uintptr_t target) {
        emit8(0xE9);
        const intptr_t rel =
            static_cast<intptr_t>(target) -
            static_cast<intptr_t>(caveAddr + idx + 4);
        if (rel < INT32_MIN || rel > INT32_MAX)
          return false;
        emit32(static_cast<int32_t>(rel));
        return true;
      };

      // Preserve flags because the replaced MOV does not alter them.
      emit8(0x9C);                                      // pushfq
      emit8(0x83); emit8(0xFF); emit8(0x04);            // cmp edi,4
      emit8(0x0F); emit8(0x85);                         // jne original-load
      const int jneOriginalDisp = idx; emit32(0);

      // index==4: substitute the external sidecar instead of reading +0x200.
      emit8(0x48); emit8(0xBE);                         // mov rsi, &global
      emit64(reinterpret_cast<uintptr_t>(&g_fifthUiSidecarButton));
      emit8(0x48); emit8(0x8B); emit8(0x36);            // mov rsi,[rsi]
      emit8(0x48); emit8(0x85); emit8(0xF6);            // test rsi,rsi
      emit8(0x0F); emit8(0x84);                         // jz original-skip
      const int jzSkipDisp = idx; emit32(0);
      emit8(0x9D);                                      // popfq
      if (!emitExternalJmp(loadAddr + sizeof(kButtonLoad))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr), 0, MEM_RELEASE);
        return false;
      }

      const int originalLoadLabel = idx;
      emit8(0x9D);                                      // popfq
      std::memcpy(cave + idx, kButtonLoad, sizeof(kButtonLoad));
      idx += static_cast<int>(sizeof(kButtonLoad));
      if (!emitExternalJmp(loadAddr + sizeof(kButtonLoad))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr), 0, MEM_RELEASE);
        return false;
      }

      const int originalSkipLabel = idx;
      // Drop saved patched flags, recreate the original cmp edi,4 flags, and
      // jump to the game's original JAE target if sidecar vanished.
      emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x08); // add rsp,8
      emit8(0x83); emit8(0xFF); emit8(0x04);              // cmp edi,4
      if (!emitExternalJmp(skipTarget)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr), 0, MEM_RELEASE);
        return false;
      }

      if (!patchRel32(jneOriginalDisp, originalLoadLabel) ||
          !patchRel32(jzSkipDisp, originalSkipLabel)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr), 0, MEM_RELEASE);
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(), cave, idx);

      // Patch the direct load first. While cmp is still 4, index4 still skips,
      // so there is no transient +0x200 access.
      std::memcpy(g_fifthUiOpenOriginalLoad, loadNow, sizeof(loadNow));
      if (!ApplyJmp(loadAddr, caveAddr, sizeof(kButtonLoad))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr), 0, MEM_RELEASE);
        return false;
      }

      // Only after the safe sidecar load is active, widen this one cmp 4 -> 5.
      DWORD oldProtect = 0, tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(cmpAddr + 2), 1,
                          PAGE_EXECUTE_READWRITE, &oldProtect)) {
        // Do not widen the cmp; the installed load cave is behavior-identical
        // for indices 0..3 and harmless while the original cmp remains 4.
        AddLog(u8"[책략5UIOPEN] cmp 4->5 권한 변경 실패. 표시 훅 미완료.");
        return false;
      }
      *reinterpret_cast<uint8_t *>(cmpAddr + 2) = 0x05;
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<void *>(cmpAddr + 2), 1);
      VirtualProtect(reinterpret_cast<LPVOID>(cmpAddr + 2), 1,
                     oldProtect, &tmpProtect);

      uint8_t verifyCmp = 0;
      if (!SafeCopySeh(cmpAddr + 2, &verifyCmp, 1) || verifyCmp != 0x05) {
        AddLog(u8"[책략5UIOPEN] cmp 4->5 쓰기 검증 실패.");
        return false;
      }

      g_fifthUiOpenCmpAddr = cmpAddr;
      g_fifthUiOpenLoadAddr = loadAddr;
      g_fifthUiOpenSkipTarget = skipTarget;
      g_fifthUiOpenCaveAddr = caveAddr;
      g_fifthUiOpenDisplayHookApplied = true;

      AddLog(u8"[책략5UIOPEN] Dialog::Open index4 sidecar 표시 훅 설치 성공: cmpRVA=+%llX loadRVA=+%llX skipRVA=+%llX",
             (unsigned long long)(cmpAddr - exeBase),
             (unsigned long long)(loadAddr - exeBase),
             (unsigned long long)(skipTarget - exeBase));
      return true;
    }


    static bool GetFifthUiLayoutMetricsSeh(uintptr_t layout,
                                           int *outStartX,
                                           int *outY,
                                           int *outStep) {
      if (!layout || !outStartX || !outY || !outStep)
        return false;

      const int liveStep = static_cast<int>(g_trickUiStep);
      if (liveStep > 0) {
        *outStartX = static_cast<int>(g_trickUiStartX);
        *outY = static_cast<int>(g_trickUiY);
        *outStep = liveStep;
        return true;
      }

      __try {
        uintptr_t descBase = 0;
        uint32_t count = 0;
        if (!SafeReadPtrSeh(layout + 0x140, &descBase) ||
            !SafeCopySeh(layout + 0x150, &count, sizeof(count)) ||
            count != 8 || !descBase || !IsValidPtr(descBase, 8 * 0x60))
          return false;

        // ID2..ID5 are descriptor rows 2..5. Runtime logs proved x/y at +4/+8.
        int x0=0,y0=0,x1=0,y1=0,x2=0,y2=0,x3=0,y3=0;
        if (!SafeCopySeh(descBase + 2*0x60 + 4, &x0, 4) ||
            !SafeCopySeh(descBase + 2*0x60 + 8, &y0, 4) ||
            !SafeCopySeh(descBase + 3*0x60 + 4, &x1, 4) ||
            !SafeCopySeh(descBase + 3*0x60 + 8, &y1, 4) ||
            !SafeCopySeh(descBase + 4*0x60 + 4, &x2, 4) ||
            !SafeCopySeh(descBase + 4*0x60 + 8, &y2, 4) ||
            !SafeCopySeh(descBase + 5*0x60 + 4, &x3, 4) ||
            !SafeCopySeh(descBase + 5*0x60 + 8, &y3, 4))
          return false;

        const int step = x1 - x0;
        if (step <= 0 || x2-x1 != step || x3-x2 != step ||
            y0 != y1 || y0 != y2 || y0 != y3 ||
            x0 < -4096 || x3 > 8192 || y0 < -4096 || y0 > 8192)
          return false;

        *outStartX=x0;
        *outY=y0;
        *outStep=step;
        return true;
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    static bool PrepareFifthUiBeforeCallbacksSeh(uintptr_t dialog) {
      __try {
        if (!dialog || !IsValidPtr(dialog, 0x40))
          return false;

        uintptr_t layout = 0;
        if (!SafeReadPtrSeh(dialog + 0x08, &layout) ||
            !layout || !IsValidPtr(layout, 0x2A8))
          return false;

        uint32_t count=0;
        uintptr_t owner=0, helperTable=0, helper7=0;
        const bool helperReady =
            SafeCopySeh(layout + 0x150, &count, sizeof(count)) && count == 8 &&
            SafeReadPtrSeh(layout + 0x158, &owner) && owner == layout &&
            SafeReadPtrSeh(layout + 0x148, &helperTable) &&
            helperTable && IsValidPtr(helperTable, 8*sizeof(uintptr_t)) &&
            SafeReadPtrSeh(helperTable + 7*sizeof(uintptr_t), &helper7) &&
            helper7 && IsValidPtr(helper7, sizeof(uintptr_t)) &&
            ValidatePreparedFifthUiHelper(layout);

        if (!helperReady) {
          AddLog(u8"[책략5UICB] callback 직전 sidecar 준비 거부: layout=%p count=%u helper7=%p",
                 reinterpret_cast<void *>(layout), (unsigned)count,
                 reinterpret_cast<void *>(helper7));
          return false;
        }

        g_trickUiDialog = dialog;
        g_trickUiLayout = layout;

        if (!CreateFifthUiSidecarDisplayOnlySeh(layout)) {
          AddLog(u8"[책략5UICB] callback 직전 sidecar 생성 실패.");
          return false;
        }
        if (!ExpandMakerAndRegisterFifthSidecarSeh(layout)) {
          AddLog(u8"[책략5UICB] callback 직전 ID7 등록 실패.");
          return false;
        }

        AddLog(u8"[책략5UICB] callback 직전 sidecar 준비 완료: dialog=%p layout=%p button=%p",
               reinterpret_cast<void *>(dialog),
               reinterpret_cast<void *>(layout),
               reinterpret_cast<void *>(g_fifthUiSidecarButton));
        return true;
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5UICB] callback 직전 sidecar 준비 중 예외.");
        return false;
      }
    }

    static bool EnsureFifthUiPreCallbackHook() {
      if (g_fifthUiPreCallbackHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kDialogInitializeRva = 0x01DF3F20;
      constexpr uintptr_t kHookOffset = 0x1C4;
      const uintptr_t hookAddr = exeBase + kDialogInitializeRva + kHookOffset;
      static const uint8_t expected[8] = {
          0x41,0x8B,0xFC,                   // mov edi,r12d
          0x48,0x8D,0x44,0x24,0x30         // lea rax,[rsp+30]
      };
      if (!IsValidPtr(hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(hookAddr),
                      expected, sizeof(expected)) != 0)
        return false;

      const uintptr_t caveAddr = AllocNear(hookAddr, 256);
      if (!caveAddr)
        return false;

      uint8_t *c = reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){ c[i++]=v; };
      auto e32=[&](int32_t v){ std::memcpy(c+i,&v,4); i+=4; };
      auto e64=[&](uintptr_t v){ std::memcpy(c+i,&v,8); i+=8; };
      auto emitJmp=[&](uintptr_t target){
        e8(0xE9);
        const intptr_t rel=static_cast<intptr_t>(target)-
                           static_cast<intptr_t>(caveAddr+i+4);
        if(rel<INT32_MIN||rel>INT32_MAX) return false;
        e32(static_cast<int32_t>(rel));
        return true;
      };

      e8(0x9C);                         // pushfq
      e8(0x50); e8(0x51); e8(0x52);   // push rax,rcx,rdx
      e8(0x41); e8(0x50);              // push r8
      e8(0x41); e8(0x51);              // push r9
      e8(0x41); e8(0x52);              // push r10
      e8(0x41); e8(0x53);              // push r11
      e8(0x48); e8(0x81); e8(0xEC); e32(0x80); // sub rsp,80

      const uint8_t xmmStores[][6] = {
        {0xF3,0x0F,0x7F,0x44,0x24,0x20},
        {0xF3,0x0F,0x7F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x7F,0x54,0x24,0x40},
        {0xF3,0x0F,0x7F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x7F,0x64,0x24,0x60},
        {0xF3,0x0F,0x7F,0x6C,0x24,0x70}
      };
      for (const auto &b : xmmStores) {
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      e8(0x48); e8(0x8B); e8(0xCE);    // mov rcx,rsi (dialog)
      e8(0x48); e8(0xB8);
      e64(reinterpret_cast<uintptr_t>(&PrepareFifthUiBeforeCallbacksSeh));
      e8(0xFF); e8(0xD0);              // call rax

      const uint8_t xmmLoads[][6] = {
        {0xF3,0x0F,0x6F,0x44,0x24,0x20},
        {0xF3,0x0F,0x6F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x6F,0x54,0x24,0x40},
        {0xF3,0x0F,0x6F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x6F,0x64,0x24,0x60},
        {0xF3,0x0F,0x6F,0x6C,0x24,0x70}
      };
      for (const auto &b : xmmLoads) {
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      e8(0x48); e8(0x81); e8(0xC4); e32(0x80); // add rsp,80
      e8(0x41); e8(0x5B);              // pop r11
      e8(0x41); e8(0x5A);              // pop r10
      e8(0x41); e8(0x59);              // pop r9
      e8(0x41); e8(0x58);              // pop r8
      e8(0x5A); e8(0x59); e8(0x58);   // pop rdx,rcx,rax
      e8(0x9D);                         // popfq

      std::memcpy(c+i,expected,sizeof(expected));
      i+=(int)sizeof(expected);
      if(!emitJmp(hookAddr+sizeof(expected))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiPreCallbackOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_fifthUiPreCallbackOriginal));
      if(!ApplyJmp(hookAddr,caveAddr,sizeof(expected))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      g_fifthUiPreCallbackHookAddr=hookAddr;
      g_fifthUiPreCallbackCaveAddr=caveAddr;
      g_fifthUiPreCallbackHookApplied=true;
      return true;
    }

    static bool EnsureFifthUiCallbackLoopHook() {
      if (g_fifthUiCallbackLoopHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kDialogInitializeRva = 0x01DF3F20;
      const uintptr_t loadAddr = exeBase + kDialogInitializeRva + 0x203;
      const uintptr_t continueAddr = exeBase + kDialogInitializeRva + 0x20B;
      const uintptr_t exitAddr = exeBase + kDialogInitializeRva + 0x36B;
      const uintptr_t boundAddr = exeBase + kDialogInitializeRva + 0x35B;

      static const uint8_t expectedLoad[8] = {
          0x48,0x8B,0x46,0x08,             // mov rax,[rsi+8]
          0x49,0x8B,0x1C,0x06              // mov rbx,[r14+rax]
      };
      static const uint8_t expectedBound[3] = {0x83,0xFF,0x04};
      if (!IsValidPtr(loadAddr,sizeof(expectedLoad)) ||
          !IsValidPtr(boundAddr,sizeof(expectedBound)) ||
          std::memcmp(reinterpret_cast<const void *>(loadAddr),
                      expectedLoad,sizeof(expectedLoad)) != 0 ||
          std::memcmp(reinterpret_cast<const void *>(boundAddr),
                      expectedBound,sizeof(expectedBound)) != 0)
        return false;

      const uintptr_t caveAddr=AllocNear(loadAddr,128);
      if(!caveAddr) return false;
      uint8_t *c=reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){c[i++]=v;};
      auto e32=[&](int32_t v){std::memcpy(c+i,&v,4);i+=4;};
      auto e64=[&](uintptr_t v){std::memcpy(c+i,&v,8);i+=8;};
      auto rel32=[&](int at,int target){
        const int64_t r=(int64_t)target-(int64_t)(at+4);
        if(r<INT32_MIN||r>INT32_MAX) return false;
        const int32_t v=(int32_t)r; std::memcpy(c+at,&v,4); return true;
      };
      auto jmpExternal=[&](uintptr_t target){
        e8(0xE9);
        const intptr_t r=static_cast<intptr_t>(target)-
                         static_cast<intptr_t>(caveAddr+i+4);
        if(r<INT32_MIN||r>INT32_MAX) return false;
        e32(static_cast<int32_t>(r)); return true;
      };

      e8(0x9C);                         // pushfq
      e8(0x83); e8(0xFF); e8(0x04);   // cmp edi,4
      e8(0x0F); e8(0x84);             // je sidecar
      const int jeSide=i; e32(0);

      e8(0x9D);                         // popfq
      std::memcpy(c+i,expectedLoad,sizeof(expectedLoad));
      i+=(int)sizeof(expectedLoad);
      if(!jmpExternal(continueAddr)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      const int sideLabel=i;
      e8(0x48); e8(0xBB);              // mov rbx,&global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiSidecarButton));
      e8(0x48); e8(0x8B); e8(0x1B);   // mov rbx,[rbx]
      e8(0x48); e8(0x85); e8(0xDB);   // test rbx,rbx
      e8(0x0F); e8(0x84);             // jz no-sidecar
      const int jzExit=i; e32(0);
      e8(0x9D);                         // popfq
      if(!jmpExternal(continueAddr)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      const int noSideLabel=i;
      e8(0x48); e8(0x83); e8(0xC4); e8(0x08); // discard saved flags
      if(!jmpExternal(exitAddr)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      if(!rel32(jeSide,sideLabel) || !rel32(jzExit,noSideLabel)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiCallbackLoadOriginal,
                  reinterpret_cast<const void *>(loadAddr),
                  sizeof(g_fifthUiCallbackLoadOriginal));
      if(!ApplyJmp(loadAddr,caveAddr,sizeof(expectedLoad))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      // The sidecar-safe load must be active before widening the original loop.
      DWORD oldProtect=0,tmpProtect=0;
      if(!VirtualProtect(reinterpret_cast<LPVOID>(boundAddr+2),1,
                         PAGE_EXECUTE_READWRITE,&oldProtect))
        return false;
      *reinterpret_cast<uint8_t *>(boundAddr+2)=0x05;
      FlushInstructionCache(GetCurrentProcess(),
                            reinterpret_cast<void *>(boundAddr+2),1);
      VirtualProtect(reinterpret_cast<LPVOID>(boundAddr+2),1,
                     oldProtect,&tmpProtect);

      uint8_t verify=0;
      if(!SafeCopySeh(boundAddr+2,&verify,1) || verify!=0x05)
        return false;

      g_fifthUiCallbackLoadHookAddr=loadAddr;
      g_fifthUiCallbackLoadCaveAddr=caveAddr;
      g_fifthUiCallbackBoundAddr=boundAddr;
      g_fifthUiCallbackLoopHookApplied=true;
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

    static bool ValidatePreparedFifthUiHelper(uintptr_t layout, uint32_t *descriptorTag) {
      const TrickUiInitTrace trace = ReadTrickUiInitTrace();
      if (!layout || !IsValidPtr(layout, 0x2A8) || !trace.helperClassMatched ||
          trace.owner != layout || trace.maker != layout + 0x140)
        return false;

      uintptr_t descBase = 0, pointerBase = 0, owner = 0, helper7 = 0, vtable = 0;
      uint32_t count = 0, head[5] = {};
      if (!SafeReadPtrSeh(layout + 0x140, &descBase) || descBase != trace.resultDescriptors ||
          !SafeReadPtrSeh(layout + 0x148, &pointerBase) || pointerBase != trace.resultTable ||
          !SafeCopySeh(layout + 0x150, &count, sizeof(count)) || count != 8 ||
          !SafeReadPtrSeh(layout + 0x158, &owner) || owner != layout ||
          !IsValidPtr(pointerBase, 8 * sizeof(uintptr_t)) ||
          !SafeReadPtrSeh(pointerBase + 7 * sizeof(uintptr_t), &helper7) ||
          !helper7 || helper7 != trace.helper7 || !IsValidPtr(helper7, sizeof(uintptr_t)) ||
          !SafeReadPtrSeh(helper7, &vtable) || vtable != trace.helperVtables[4] ||
          !IsValidPtr(descBase, 8 * 0x60) ||
          !SafeCopySeh(descBase + 7 * 0x60, head, sizeof(head)) ||
          std::memcmp(head, trace.resultHead[4], sizeof(head)) != 0)
        return false;
      if (descriptorTag)
        *descriptorTag = head[0];
      return true;
    }

    static bool ExpandMakerAndRegisterFifthSidecarSeh(uintptr_t layout) {
      if (!layout ||
          !g_fifthUiSidecarButton ||
          g_fifthUiSidecarLayout != layout ||
          !IsValidPtr(layout, 0x2A8) ||
          !IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return false;

      if (g_fifthUiId7Registered)
        return true;

      volatile int stage = 0;
      __try {
        const uintptr_t exeBase =
            reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
        if (!exeBase)
          return false;

        constexpr uintptr_t kLookupLayoutRva = 0x01D15440;
        constexpr uintptr_t kRegisterLayoutRva = 0x01D16AA0;

        using LookupLayoutFn = uintptr_t(__fastcall *)(uintptr_t, int);
        using RegisterLayoutFn =
            void(__fastcall *)(uintptr_t, int, uintptr_t, int, int);
        using SetXYFn = void(__fastcall *)(uintptr_t, int, int);

        const auto lookup =
            reinterpret_cast<LookupLayoutFn>(exeBase + kLookupLayoutRva);
        const auto registerLayout =
            reinterpret_cast<RegisterLayoutFn>(exeBase + kRegisterLayoutRva);

        const uintptr_t maker = layout + 0x140;
        uintptr_t descBase=0, pointerBase=0, owner=0;
        uint32_t count=0;

        stage=1;
        if (!SafeReadPtrSeh(maker+0x00,&descBase) ||
            !SafeReadPtrSeh(maker+0x08,&pointerBase) ||
            !SafeCopySeh(maker+0x10,&count,sizeof(count)) ||
            !SafeReadPtrSeh(maker+0x18,&owner) ||
            count != 8 || owner != layout ||
            !descBase || !pointerBase ||
            !IsValidPtr(descBase,8*0x60) ||
            !IsValidPtr(pointerBase,8*sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] 초기 8칸 maker가 준비되지 않음: count=%u owner=%p. 시작 로그와 브리지 진입 로그를 확인하세요. live maker 재초기화 금지.",
                 (unsigned)count,
                 reinterpret_cast<void *>(owner));
          return false;
        }

        uintptr_t helper7=0;
        SafeReadPtrSeh(pointerBase + 7*sizeof(uintptr_t), &helper7);
        if (!helper7 || !IsValidPtr(helper7, sizeof(uintptr_t))) {
          AddLog(u8"[책략5UITEST] ID7 helper가 없음. 초기 InitLayouts 결과를 확인하세요.");
          return false;
        }

        uint32_t descriptorTag = 0;
        if (!ValidatePreparedFifthUiHelper(layout, &descriptorTag)) {
          AddLog(u8"[책략5UITEST] ID7 초기 helper/클래스/descriptor 사본 일치 검증 실패. 등록 중단.");
          return false;
        }

        uintptr_t buttons[4]={};
        if (!SafeCopySeh(layout+0x1E0,buttons,sizeof(buttons)))
          return false;
        for(int i=0;i<4;++i) {
          if (lookup(maker,i+2) != buttons[i]) {
            AddLog(u8"[책략5UITEST] 기존 UI ID%d lookup 불일치.",i+2);
            return false;
          }
        }

        if (lookup(maker,7)) {
          AddLog(u8"[책략5UITEST] ID7이 이미 점유되어 있어 추가 등록을 중단합니다.");
          return false;
        }

        stage=2;
        // This is the independently confirmed R9D value at the original
        // RegisterLayout call, NOT the pre-registration descriptor's first word.
        constexpr int kTrickButtonRegistrationType = 0x14;
        AddLog(u8"[책략5UITEST] 원본이 만든 helper7로 ID7 등록 시작: helper=%p button=%p descriptorTag=%u registerType=%d",
               reinterpret_cast<void *>(helper7),
               reinterpret_cast<void *>(g_fifthUiSidecarButton),
               descriptorTag, kTrickButtonRegistrationType);
        registerLayout(maker,7,g_fifthUiSidecarButton,kTrickButtonRegistrationType,1);

        stage=3;
        const uintptr_t check7=lookup(maker,7);
        uintptr_t remainingHelper7 = 0;
        const bool helperConsumed = SafeReadPtrSeh(pointerBase + 7 * sizeof(uintptr_t), &remainingHelper7) &&
                                    remainingHelper7 == 0;
        uint32_t sidecarId=0,sidecarState=0;
        SafeCopySeh(g_fifthUiSidecarButton+0x88,&sidecarId,sizeof(sidecarId));
        SafeCopySeh(g_fifthUiSidecarButton+0x8C,&sidecarState,sizeof(sidecarState));
        if(!helperConsumed || check7 != g_fifthUiSidecarButton ||
           sidecarId != 7 || sidecarState != 1) {
          AddLog(u8"[책략5UITEST] ID7 등록 검증 실패: lookup=%p id=%u state=%u helper7=%p",
                 reinterpret_cast<void *>(check7),
                 (unsigned)sidecarId,(unsigned)sidecarState, reinterpret_cast<void *>(remainingHelper7));
          return false;
        }

        // Compact the five actual button objects into the existing row.
        // Before ResetBtnPos has ever executed, derive these values from the
        // already-proven ID2..ID5 descriptor coordinates.
        int oldStart=0, oldStep=0, y=0;
        if (!GetFifthUiLayoutMetricsSeh(layout,&oldStart,&y,&oldStep)) {
          AddLog(u8"[책략5UITEST] 5버튼 배치 원본 좌표를 얻지 못했습니다.");
          return false;
        }
        const int compactStep=(oldStep*11)/14;
        const int oldCenter=oldStart+(oldStep*3)/2;
        const int compactStart=oldCenter-compactStep*2;
        const int compactX[5]={
          compactStart,
          compactStart+compactStep,
          compactStart+compactStep*2,
          compactStart+compactStep*3,
          compactStart+compactStep*4
        };

        stage=4;
        uintptr_t allButtons[5]={
          buttons[0],buttons[1],buttons[2],buttons[3],g_fifthUiSidecarButton
        };
        for(int i=0;i<5;++i) {
          const uintptr_t button=allButtons[i];
          if(!button || !IsValidPtr(button,sizeof(uintptr_t)))
            continue;
          const uintptr_t vt=*reinterpret_cast<const uintptr_t *>(button);
          if(!vt || !IsValidPtr(vt+0x90,sizeof(uintptr_t)))
            continue;
          const uintptr_t setPos=*reinterpret_cast<const uintptr_t *>(vt+0x90);
          if(setPos && IsValidPtr(setPos,1))
            reinterpret_cast<SetXYFn>(setPos)(button,compactX[i],y);
        }

        g_fifthUiId7Registered=true;

        // RegisterLayout may apply the descriptor's initial visibility. Reassert
        // visible after registration before the next Dialog::Open test.
        const uintptr_t sidecarVt =
            *reinterpret_cast<const uintptr_t *>(g_fifthUiSidecarButton);
        if (sidecarVt && IsValidPtr(sidecarVt + 0x108, sizeof(uintptr_t))) {
          const uintptr_t setVisible =
              *reinterpret_cast<const uintptr_t *>(sidecarVt + 0x108);
          if (setVisible && IsValidPtr(setVisible, 1)) {
            using SetBoolFn = void(__fastcall *)(uintptr_t, bool);
            reinterpret_cast<SetBoolFn>(setVisible)(
                g_fifthUiSidecarButton, true);
          }
        }

        const bool getButtonHookReady = EnsureFifthUiGetTrickButtonHook();
        const bool openHookReady = EnsureFifthUiOpenDisplayHook();
        LogFifthUiCallbackLoopCandidate();
        AddLog(u8"[책략5UITEST] ID7 정식 등록 성공. helper7 소모 및 5버튼 압축 배치 완료.");
        AddLog(u8"[책략5UITEST] GetTrickButton index4 sidecar 훅=%s.",
               getButtonHookReady ? "READY" : "FAILED");
        AddLog(u8"[책략5UITEST] 5버튼 위치: %d,%d,%d,%d,%d / y=%d",
               compactX[0],compactX[1],compactX[2],compactX[3],compactX[4],y);
        AddLog(u8"[책략5UITEST] Dialog::Open index4 sidecar 표시 훅=%s. 책략창을 닫았다가 다시 여세요.",
               openHookReady ? "READY" : "FAILED");
        AddLog(u8"[책략5UITEST] callback 5회 루프는 조기 설치 훅이 담당합니다. 5번째 hover만 먼저 확인하세요.");
        return true;
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5UITEST] ID7 등록 중 예외: stage=%d",(int)stage);
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
        g_fifthUiId7Registered = false;
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

        int oldStart=0, oldStep=0, y=0;
        if (!GetFifthUiLayoutMetricsSeh(layout,&oldStart,&y,&oldStep)) {
          AddLog(u8"[책략5UITEST] sidecar 좌표 원본을 얻지 못했습니다.");
          return false;
        }
        const int compactStep = (oldStep * 11) / 14;
        const int oldCenter = oldStart + (oldStep * 3) / 2;
        const int compactStart = oldCenter - compactStep * 2;
        const int x = compactStart + compactStep * 4;
        if (x < -4096 || x > 8192 || y < -4096 || y > 8192) {
          AddLog(u8"[책략5UITEST] sidecar 좌표 비정상: x=%d y=%d step=%d",
                 x, y, oldStep);
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

  bool PrepareStratagemFiveUiBridge(bool reportFailure) {
    const bool initReady = EnsureTrickUiInitLayoutsBridgeHook(reportFailure);
    const bool preCallbackReady = EnsureFifthUiPreCallbackHook();
    const bool callbackLoopReady = EnsureFifthUiCallbackLoopHook();
    const bool ready = initReady && preCallbackReady && callbackLoopReady;
    if (!ready && reportFailure) {
      AddLog(u8"[책략5UIHELPER] 조기 UI 훅 준비 실패: init=%d preCallback=%d callbackLoop=%d",
             initReady?1:0, preCallbackReady?1:0, callbackLoopReady?1:0);
    }
    return ready;
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
      g_fifthUiId7Registered = false;
      AddLog(u8"[책략5메타DBG] 5번 내부 등록 해제.");
      return true;
    }

    // The DLL worker normally installed this already. This is only a guarded
    // fallback/status report, never an attempt to reinitialize a live maker.
    if (!PrepareStratagemFiveUiBridge())
      return false;
    LogTrickUiBridgeStatus("metadata-enable");

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

    LogTrickUiBridgeStatus("live-layout");

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

    // Refuse before allocating a sidecar if this layout predates the bridge or
    // its one-shot helper is absent. No count edits, helper synthesis or retries.
    uintptr_t helperTable = 0, helper7 = 0, makerOwner = 0;
    const bool helperReady = controlCount == 8 &&
        SafeReadPtrSeh(layout + 0x158, &makerOwner) && makerOwner == layout &&
        SafeReadPtrSeh(layout + 0x148, &helperTable) &&
        IsValidPtr(helperTable, 8 * sizeof(uintptr_t)) &&
        SafeReadPtrSeh(helperTable + 7 * sizeof(uintptr_t), &helper7) &&
        helper7 && IsValidPtr(helper7, sizeof(uintptr_t)) &&
        ValidatePreparedFifthUiHelper(layout);
    if (!helperReady) {
      AddLog(u8"[책략5UIHELPER] 표시 실험 중단: layout=%p count=%u helper7=%p hits=%u expanded=%u. 생성/등록/재초기화 없음.",
             reinterpret_cast<void *>(layout), static_cast<unsigned>(controlCount),
             reinterpret_cast<void *>(helper7), g_trickInitLayoutsHits.load(),
             g_trickInitLayoutsExpanded.load());
    } else if (CreateFifthUiSidecarDisplayOnlySeh(layout))
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
