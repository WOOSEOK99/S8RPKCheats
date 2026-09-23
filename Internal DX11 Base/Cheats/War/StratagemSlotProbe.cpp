#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"
#include "StratagemFiveModel.h"
#include "Spell5HealProbe.h"

#include <psapi.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace DX11Base {
  static bool TryExtendFifthDialogModelCountSeh(uintptr_t dialog);

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
    static std::atomic<bool> g_id5CountRequested{false};
    static uintptr_t g_id5CountAddr = 0;
    static uintptr_t g_id5CountOwner = 0;
    static uint8_t g_id5CountOriginal = 0;
    static uint32_t g_id5CountEntryIndex = UINT32_MAX;

    static bool g_fiveLoopApplied = false;
    static uintptr_t g_fiveLoopImmAddr = 0;
    static uint8_t g_fiveLoopOriginal = 0;

    static bool g_fiveMetadataApplied = false;
    static std::atomic<bool> g_fiveMetadataRequested{false};
    static uintptr_t g_fiveMetadataAddr = 0;
    static uintptr_t g_fiveMetadataTable = 0;

    static bool g_fiveRuntimeSlotApplied = false;
    static uintptr_t g_fiveRuntimeSlotAddr = 0;
    static uintptr_t g_fiveRuntimeSlotOriginal = 0;
    static uintptr_t g_fiveRuntimeOwner = 0;

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
    static unsigned g_fifthUiRegisteredEpoch = 0;
    static uintptr_t g_fifthUiOriginalButtons[4]{};
    // Published separately from the physical button used during callback setup.
    // x64 caves read these aligned atomic pointer values with ordinary MOVs.
    static std::atomic<uintptr_t> g_fifthUiActiveLayout{0};
    static std::atomic<uintptr_t> g_fifthUiActiveButton{0};
    static void BeginFifthUiNativeInitialize(uintptr_t layout);
    static bool ValidateFifthUiRegistrySeh(uintptr_t layout);

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

    static uintptr_t g_fifthUiLayoutPostButtonsHookAddr = 0;
    static uintptr_t g_fifthUiLayoutPostButtonsCaveAddr = 0;
    static uint8_t g_fifthUiLayoutPostButtonsOriginal[5] = {};
    static bool g_fifthUiLayoutPostButtonsHookApplied = false;
    static volatile LONG g_fifthUiCallbackIndex4Hits = 0;

    static uintptr_t g_fifthUiResetCompactHookAddr = 0;
    static uintptr_t g_fifthUiResetCompactCaveAddr = 0;
    static uint8_t g_fifthUiResetCompactOriginal[5] = {};
    static bool g_fifthUiResetCompactHookApplied = false;
    static bool g_fifthUiCompactLogged = false;
    static bool g_fifthUiResetSignalDumped = false;

    static uintptr_t g_fifthUiOnSelectCmpImmAddr = 0;
    static uint8_t g_fifthUiOnSelectCmpOriginal = 0;
    static bool g_fifthUiOnSelectBoundHookApplied = false;
    static bool g_fifthUiOnSelectProbeDone = false;

    static uintptr_t g_fifthUiOnSelectRuntimeHookAddr = 0;
    static uintptr_t g_fifthUiOnSelectRuntimeCaveAddr = 0;
    static uint8_t g_fifthUiOnSelectRuntimeOriginal[7] = {};
    static bool g_fifthUiOnSelectRuntimeHookApplied = false;
    static volatile LONG g_fifthUiOnSelectRuntimeLogged = 0;
    static volatile LONG g_fifthUiOnSelectAnyHits = 0;
    static bool g_fifthUiSignalCallsitesLogged = false;
    static bool g_fifthUiCallbackTargetsLogged = false;
    static bool g_fifthUiCallbackCodeTargetsLogged = false;
    static uintptr_t g_fifthUiModelCountAddr = 0;
    static uint32_t g_fifthUiModelCountOriginal = 0;
    static bool g_fifthUiModelCountApplied = false;
    static uintptr_t g_fifthUiModelEntryAddr = 0;
    static uint64_t g_fifthUiModelEntryOriginal[2] = {};
    static bool g_fifthUiModelEntryApplied = false;
    static uint32_t g_fifthRuntimeOriginalCount = 0;

    // AI capability probe: do not expand the AI list. Temporarily replace one
    // existing AI-side TrickData row with row5 and preserve that slot's native
    // index/available count. This isolates whether the AI selector can consume
    // ID5 without mixing the result with N->N+1 UI work.
    static bool g_fifthAiProbeApplied = false;
    static uintptr_t g_fifthAiProbeOwner = 0;
    static uintptr_t g_fifthAiProbeEntryAddr = 0;
    static uintptr_t g_fifthAiProbeCampSlotAddr = 0;
    static uintptr_t g_fifthAiProbeOriginalData = 0;
    static uintptr_t g_fifthAiProbeOriginalCampRow = 0;
    static uint8_t g_fifthAiProbeInitialAvailable = 0;
    static uint8_t g_fifthAiProbeLastAvailable = 0;
    static uint32_t g_fifthAiProbeIndex = UINT32_MAX;
    static bool g_fifthAiProbeConsumptionLogged = false;

    // Must be defined before the pre-callback hook helpers below reference it.
    enum class FifthRuntimeStage : uint8_t {
      WaitingOwner = 0,
      WaitingTable,
      WaitingData,
      WaitingCount,
      WaitingCamp,
      WaitingModel,
      Ready
    };
    static std::atomic<FifthRuntimeStage> g_fifthRuntimeStage{
        FifthRuntimeStage::WaitingOwner};

    static bool AdvanceFifthRuntimeStateSeh(uintptr_t dialog);
    static void SetFifthUiSidecarVisibleSeh(bool visible);

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
    static bool TryGetDialogCampOwnerSeh(uintptr_t *outOwner);
    static bool TryReadFifthPlanForOwnerSeh(
        uintptr_t owner,
        StratagemFiveModel::Plan *outPlan,
        StratagemFiveModel::Entry (*outEntries)[5],
        uintptr_t (*outCampRows)[5],
        uintptr_t *outModelBase,
        uintptr_t *outTricksAddr);


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
      // A native initialization call is a new UI generation, even at the same
      // address. Retire our published references before the maker is rebuilt.
      BeginFifthUiNativeInitialize(owner);

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

      // PrepareFifthUiBeforeCallbacksSeh may already have captured the live
      // dialog/layout before this optional probe hook is first installed.
      // Never erase that generation here; battle/session reset owns clearing.
      g_lastLoggedUiLayout = 0;
      AddLog(u8"[책략5UICAP] layout 캡처 훅 설치 완료. 기존 live dialog/layout 캡처는 보존.");
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

      // Do not clear a dialog already captured by the pre-callback hook.
      // The capture hook only supplements later Dialog::Open calls.
      AddLog(u8"[책략5UICAP] dialog 캡처 훅 설치 완료. 기존 live dialog 캡처는 보존.");
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
      // index4 is valid only for the exact layout that owns the sidecar.
      e8(0x48); e8(0xB8);                      // mov rax,&layout-global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveLayout));
      e8(0x48); e8(0x8B); e8(0x00);            // mov rax,[rax]
      e8(0x48); e8(0x39); e8(0xC1);            // cmp rcx,rax
      e8(0x0F); e8(0x85);                      // jne out
      const int jneLayoutOut=i; e32(0);
      e8(0x48); e8(0xB8);                      // mov rax,&button-global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveButton));
      e8(0x48); e8(0x8B); e8(0x00);            // mov rax,[rax]
      e8(0xC3);                                // ret

      const int outLabel=i;
      e8(0x33); e8(0xC0);                      // xor eax,eax
      e8(0xC3);                                // ret

      if(!patchRel32(jeSide,sideLabel) ||
         !patchRel32(jaOut,outLabel) ||
         !patchRel32(jneLayoutOut,outLabel)) {
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

    static void RefreshFifthUiAtOpenDisplaySeh() {
      if (!g_id5CountRequested.load() ||
          !g_fiveMetadataRequested.load())
        return;

      const uintptr_t dialog = g_trickUiDialog;
      if (!dialog || !IsValidPtr(dialog, 0x40))
        return;

      const FifthRuntimeStage before = g_fifthRuntimeStage.load();
      if (before == FifthRuntimeStage::Ready)
        return;

      const bool ready = AdvanceFifthRuntimeStateSeh(dialog);
      const FifthRuntimeStage after = g_fifthRuntimeStage.load();
      AddLog(u8"[책략5STATE] Dialog::Open 첫 버튼 진행: %u -> %u ready=%d dialog=%p",
             (unsigned)before, (unsigned)after, ready ? 1 : 0,
             reinterpret_cast<void *>(dialog));
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

      const uintptr_t caveAddr = AllocNear(loadAddr, 512);
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
      // On the first native button only, advance ID5 at the earliest already
      // proven Dialog::Open UI point. ResetBtnPos is later than this and can
      // make the model ready only after the current Open has chosen its buttons.
      emit8(0x9C);                                      // pushfq
      emit8(0x85); emit8(0xFF);                         // test edi,edi
      emit8(0x0F); emit8(0x85);                         // jne skip-open-refresh
      const int jneSkipOpenRefreshDisp = idx; emit32(0);

      emit8(0x50); emit8(0x51); emit8(0x52);            // push rax,rcx,rdx
      emit8(0x41); emit8(0x50);                         // push r8
      emit8(0x41); emit8(0x51);                         // push r9
      emit8(0x41); emit8(0x52);                         // push r10
      emit8(0x41); emit8(0x53);                         // push r11
      emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x80);

      const uint8_t openXmmStores[][6] = {
        {0xF3,0x0F,0x7F,0x44,0x24,0x20},
        {0xF3,0x0F,0x7F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x7F,0x54,0x24,0x40},
        {0xF3,0x0F,0x7F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x7F,0x64,0x24,0x60},
        {0xF3,0x0F,0x7F,0x6C,0x24,0x70}
      };
      for (const auto &b : openXmmStores) {
        std::memcpy(cave + idx, b, sizeof(b));
        idx += (int)sizeof(b);
      }

      emit8(0x48); emit8(0xB8);
      emit64(reinterpret_cast<uintptr_t>(&RefreshFifthUiAtOpenDisplaySeh));
      emit8(0xFF); emit8(0xD0);                         // call rax

      const uint8_t openXmmLoads[][6] = {
        {0xF3,0x0F,0x6F,0x44,0x24,0x20},
        {0xF3,0x0F,0x6F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x6F,0x54,0x24,0x40},
        {0xF3,0x0F,0x6F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x6F,0x64,0x24,0x60},
        {0xF3,0x0F,0x6F,0x6C,0x24,0x70}
      };
      for (const auto &b : openXmmLoads) {
        std::memcpy(cave + idx, b, sizeof(b));
        idx += (int)sizeof(b);
      }

      emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x80);
      emit8(0x41); emit8(0x5B);                         // pop r11
      emit8(0x41); emit8(0x5A);                         // pop r10
      emit8(0x41); emit8(0x59);                         // pop r9
      emit8(0x41); emit8(0x58);                         // pop r8
      emit8(0x5A); emit8(0x59); emit8(0x58);            // pop rdx,rcx,rax

      const int skipOpenRefreshLabel = idx;

      emit8(0x83); emit8(0xFF); emit8(0x04);            // cmp edi,4
      emit8(0x0F); emit8(0x85);                         // jne original-load
      const int jneOriginalDisp = idx; emit32(0);

      // index==4: substitute only when RAX is the exact layout that
      // owns the current sidecar. This blocks stale pointers across save/load.
      emit8(0x48); emit8(0xBE);                         // mov rsi,&layout-global
      emit64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveLayout));
      emit8(0x48); emit8(0x3B); emit8(0x06);            // cmp rax,[rsi]
      emit8(0x0F); emit8(0x85);                         // jne original-skip
      const int jneLayoutSkipDisp = idx; emit32(0);
      emit8(0x48); emit8(0xBE);                         // mov rsi,&button-global
      emit64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveButton));
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

      if (!patchRel32(jneSkipOpenRefreshDisp, skipOpenRefreshLabel) ||
          !patchRel32(jneOriginalDisp, originalLoadLabel) ||
          !patchRel32(jneLayoutSkipDisp, originalSkipLabel) ||
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




    static void ProbeFifthOnTrickSelectRuntimeSeh(uintptr_t self,
                                                  uint32_t index) {
      const LONG hit=InterlockedIncrement(&g_fifthUiOnSelectAnyHits);
      if(hit<=12) {
        AddLog(u8"[책략5UISELRT] OnTrickSelect 호출 #%ld: self=%p index=%u",
               (long)hit,reinterpret_cast<void *>(self),(unsigned)index);
      }

      if(index!=4)
        return;
      if(InterlockedCompareExchange(&g_fifthUiOnSelectRuntimeLogged,1,0)!=0)
        return;

      __try {
        uintptr_t p8=0,holder=0,inner=0,selected=0;
        uint32_t state=0,count=0;
        if(!SafeReadPtrSeh(self+0x08,&p8) ||
           !SafeCopySeh(self+0x18,&state,sizeof(state)) ||
           !SafeReadPtrSeh(self+0x20,&holder) ||
           !SafeReadPtrSeh(self+0x28,&selected) ||
           !holder || !IsValidPtr(holder,sizeof(uintptr_t)) ||
           !SafeReadPtrSeh(holder,&inner) ||
           !inner || !IsValidPtr(inner+0xF0,0x80)) {
          AddLog(u8"[책략5UISELRT] index4 런타임 모델 읽기 실패: self=%p p8=%p holder=%p inner=%p state=%u selected=%p",
                 reinterpret_cast<void *>(self),
                 reinterpret_cast<void *>(p8),
                 reinterpret_cast<void *>(holder),
                 reinterpret_cast<void *>(inner),
                 (unsigned)state,
                 reinterpret_cast<void *>(selected));
          return;
        }

        const uintptr_t base=inner+0xF0;
        SafeCopySeh(base+0x60,&count,sizeof(count));
        AddLog(u8"[책략5UISELRT] index4 click: self=%p p8=%p state=%u holder=%p inner=%p base=%p count=%u selected(before)=%p",
               reinterpret_cast<void *>(self),
               reinterpret_cast<void *>(p8),
               (unsigned)state,
               reinterpret_cast<void *>(holder),
               reinterpret_cast<void *>(inner),
               reinterpret_cast<void *>(base),
               (unsigned)count,
               reinterpret_cast<void *>(selected));

        for(int n=0;n<5;++n){
          uint64_t q0=0,q1=0;
          const uintptr_t e=base+0x10+(uintptr_t)n*0x10;
          SafeCopySeh(e,&q0,sizeof(q0));
          SafeCopySeh(e+8,&q1,sizeof(q1));
          AddLog(u8"[책략5UISELRT] entry%d @%p = %016llX %016llX",
                 n,reinterpret_cast<void *>(e),
                 (unsigned long long)q0,(unsigned long long)q1);
        }

        uint64_t tail[4]={};
        SafeCopySeh(base+0x60,tail,sizeof(tail));
        AddLog(u8"[책략5UISELRT] base+60..7F = %016llX %016llX %016llX %016llX",
               (unsigned long long)tail[0],
               (unsigned long long)tail[1],
               (unsigned long long)tail[2],
               (unsigned long long)tail[3]);
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5UISELRT] index4 런타임 모델 probe 예외.");
      }
    }



    static void LogFifthUiCallbackTargets() {
      if(g_fifthUiCallbackTargetsLogged)
        return;
      g_fifthUiCallbackTargetsLogged=true;

      const uintptr_t exeBase=
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      MODULEINFO mi{};
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase) ||
         !GetModuleInformation(GetCurrentProcess(),
                               reinterpret_cast<HMODULE>(exeBase),
                               &mi,sizeof(mi)))
        return;

      const uintptr_t imageEnd=exeBase+static_cast<uintptr_t>(mi.SizeOfImage);
      constexpr uintptr_t kDialogInitializeRva=0x01DF3F20;
      constexpr size_t kDialogInitializeSize=0x4C9;
      const uintptr_t fn=exeBase+kDialogInitializeRva;

      uint8_t code[kDialogInitializeSize]={};
      if(!SafeCopySeh(fn,code,sizeof(code)))
        return;

      struct TargetInfo {
        uintptr_t addr;
        size_t leaOff;
        uint8_t rex;
        uint8_t modrm;
      };
      TargetInfo targets[8]={};
      unsigned targetCount=0;

      // The callback setup immediately before the 4-button AddSig loop uses
      // RIP-relative LEAs to load the three std::function invoker/manager
      // targets. Resolve only LEA r64,[RIP+disp32] in +0x1D0..+0x210.
      for(size_t i=0x1D0;i+7<=sizeof(code) && i<0x210;++i){
        const uint8_t rex=code[i];
        if((rex!=0x48 && rex!=0x4C) || code[i+1]!=0x8D)
          continue;
        const uint8_t modrm=code[i+2];
        if((modrm & 0xC7)!=0x05)
          continue;

        int32_t disp=0;
        std::memcpy(&disp,code+i+3,sizeof(disp));
        const uintptr_t target=fn+i+7+static_cast<intptr_t>(disp);
        if(target<exeBase || target>=imageEnd)
          continue;

        bool duplicate=false;
        for(unsigned n=0;n<targetCount;++n)
          if(targets[n].addr==target) duplicate=true;
        if(duplicate || targetCount>=8)
          continue;

        targets[targetCount++]={target,i,rex,modrm};
      }

      AddLog(u8"[책략5UICBTGT] callback setup RIP targets=%u",targetCount);

      for(unsigned n=0;n<targetCount;++n){
        const uintptr_t target=targets[n].addr;
        uint8_t head[0x100]={};
        if(!IsValidPtr(target,sizeof(head)) ||
           !SafeCopySeh(target,head,sizeof(head))){
          AddLog(u8"[책략5UICBTGT] #%u LEA+%llX -> RVA=+%llX unreadable",
                 n+1,
                 (unsigned long long)targets[n].leaOff,
                 (unsigned long long)(target-exeBase));
          continue;
        }

        AddLog(u8"[책략5UICBTGT] #%u LEA+%llX -> RVA=+%llX rex=%02X modrm=%02X",
               n+1,
               (unsigned long long)targets[n].leaOff,
               (unsigned long long)(target-exeBase),
               (unsigned)targets[n].rex,(unsigned)targets[n].modrm);

        for(size_t p=0;p<sizeof(head);p+=0x20){
          char line[256]={}; int pos=0;
          for(size_t j=0;j<0x20 && pos<(int)sizeof(line)-4;++j)
            pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)head[p+j]);
          AddLog(u8"[책략5UICBTGT] #%u +%02llX : %s",
                 n+1,(unsigned long long)p,line);
        }

        unsigned calls=0;
        for(size_t p=0;p+5<=sizeof(head);++p){
          if(head[p]!=0xE8)
            continue;
          int32_t rel=0;
          std::memcpy(&rel,head+p+1,sizeof(rel));
          const uintptr_t callee=target+p+5+static_cast<intptr_t>(rel);
          if(callee<exeBase || callee>=imageEnd)
            continue;
          ++calls;
          AddLog(u8"[책략5UICBTGT] #%u direct call +%02llX -> RVA=+%llX",
                 n+1,(unsigned long long)p,
                 (unsigned long long)(callee-exeBase));
        }
        AddLog(u8"[책략5UICBTGT] #%u directCalls=%u",n+1,calls);
      }
    }


    static bool IsExecutableAddress(uintptr_t address) {
      if(!address)
        return false;
      MEMORY_BASIC_INFORMATION mbi{};
      if(!VirtualQuery(reinterpret_cast<LPCVOID>(address),&mbi,sizeof(mbi)))
        return false;
      if(mbi.State!=MEM_COMMIT)
        return false;
      const DWORD p=mbi.Protect & 0xFF;
      return p==PAGE_EXECUTE ||
             p==PAGE_EXECUTE_READ ||
             p==PAGE_EXECUTE_READWRITE ||
             p==PAGE_EXECUTE_WRITECOPY;
    }

    static void LogFifthUiCallbackCodeTargets() {
      if(g_fifthUiCallbackCodeTargetsLogged)
        return;
      g_fifthUiCallbackCodeTargetsLogged=true;

      const uintptr_t exeBase=
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      MODULEINFO mi{};
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase) ||
         !GetModuleInformation(GetCurrentProcess(),
                               reinterpret_cast<HMODULE>(exeBase),
                               &mi,sizeof(mi)))
        return;

      const uintptr_t imageEnd=exeBase+static_cast<uintptr_t>(mi.SizeOfImage);
      constexpr uintptr_t kDialogInitializeRva=0x01DF3F20;
      constexpr size_t kDialogInitializeSize=0x4C9;
      const uintptr_t fn=exeBase+kDialogInitializeRva;

      uint8_t code[kDialogInitializeSize]={};
      if(!SafeCopySeh(fn,code,sizeof(code)))
        return;

      uintptr_t tables[8]={};
      unsigned tableCount=0;

      for(size_t i=0x1D0;i+7<=sizeof(code) && i<0x210;++i){
        const uint8_t rex=code[i];
        if((rex!=0x48 && rex!=0x4C) || code[i+1]!=0x8D)
          continue;
        const uint8_t modrm=code[i+2];
        if((modrm & 0xC7)!=0x05)
          continue;

        int32_t disp=0;
        std::memcpy(&disp,code+i+3,sizeof(disp));
        const uintptr_t target=fn+i+7+static_cast<intptr_t>(disp);
        if(target<exeBase || target>=imageEnd)
          continue;

        bool dup=false;
        for(unsigned n=0;n<tableCount;++n)
          if(tables[n]==target) dup=true;
        if(!dup && tableCount<8)
          tables[tableCount++]=target;
      }

      AddLog(u8"[책략5UICBCODE] callback tables=%u",tableCount);

      uintptr_t seen[32]={};
      unsigned seenCount=0;

      for(unsigned t=0;t<tableCount;++t){
        uint64_t q[16]={};
        if(!SafeCopySeh(tables[t],q,sizeof(q))){
          AddLog(u8"[책략5UICBCODE] table%u unreadable: RVA=+%llX",
                 t+1,(unsigned long long)(tables[t]-exeBase));
          continue;
        }

        for(unsigned s=0;s<8;++s){
          const uintptr_t p=static_cast<uintptr_t>(q[s]);
          if(p<exeBase || p>=imageEnd || !IsExecutableAddress(p))
            continue;

          bool dup=false;
          for(unsigned n=0;n<seenCount;++n)
            if(seen[n]==p) dup=true;
          if(dup)
            continue;
          if(seenCount<32)
            seen[seenCount++]=p;

          uint8_t head[0x80]={};
          if(!SafeCopySeh(p,head,sizeof(head)))
            continue;

          AddLog(u8"[책략5UICBCODE] table%u slot%u -> RVA=+%llX",
                 t+1,s,(unsigned long long)(p-exeBase));

          for(size_t off=0;off<sizeof(head);off+=0x20){
            char line[256]={}; int pos=0;
            for(size_t j=0;j<0x20 && pos<(int)sizeof(line)-4;++j)
              pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)head[off+j]);
            AddLog(u8"[책략5UICBCODE] +%02llX : %s",
                   (unsigned long long)off,line);
          }

          for(size_t off=0;off+5<=sizeof(head);++off){
            if(head[off]!=0xE8 && head[off]!=0xE9)
              continue;
            int32_t rel=0;
            std::memcpy(&rel,head+off+1,sizeof(rel));
            const uintptr_t target=
                p+off+5+static_cast<intptr_t>(rel);
            if(target>=exeBase && target<imageEnd){
              AddLog(u8"[책략5UICBCODE] RVA=+%llX %s +%02llX -> RVA=+%llX",
                     (unsigned long long)(p-exeBase),
                     head[off]==0xE8?"CALL":"JMP",
                     (unsigned long long)off,
                     (unsigned long long)(target-exeBase));
            }
          }
        }
      }

      AddLog(u8"[책략5UICBCODE] unique executable targets=%u",seenCount);
    }

    static void LogFifthUiSignalCallsites() {
      if(g_fifthUiSignalCallsitesLogged)
        return;
      g_fifthUiSignalCallsitesLogged=true;

      const uintptr_t exeBase=
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return;

      constexpr uintptr_t kDialogInitializeRva=0x01DF3F20;
      constexpr size_t kDialogInitializeSize=0x4C9;
      constexpr uintptr_t kAddSigRva=0x01EDDC90;
      const uintptr_t fn=exeBase+kDialogInitializeRva;
      const uintptr_t addSig=exeBase+kAddSigRva;

      uint8_t code[kDialogInitializeSize]={};
      if(!SafeCopySeh(fn,code,sizeof(code)))
        return;

      unsigned found=0;
      for(size_t i=0;i+5<=sizeof(code);++i){
        if(code[i]!=0xE8)
          continue;
        int32_t rel=0;
        std::memcpy(&rel,code+i+1,sizeof(rel));
        const uintptr_t target=fn+i+5+static_cast<intptr_t>(rel);
        if(target!=addSig)
          continue;

        ++found;
        const size_t from=(i>0x50)?i-0x50:0;
        const size_t to=((i+0x30)<sizeof(code))?i+0x30:sizeof(code);
        AddLog(u8"[책략5UISIG] AddSig call #%u at Dialog::Initialize +%llX",
               found,(unsigned long long)i);

        for(size_t p=from;p<to;p+=0x20){
          const size_t chunk=((to-p)>0x20)?0x20:(to-p);
          char line[256]={}; int pos=0;
          for(size_t j=0;j<chunk && pos<(int)sizeof(line)-4;++j)
            pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)code[p+j]);
          AddLog(u8"[책략5UISIG] +%03llX : %s",
                 (unsigned long long)p,line);
        }
      }
      AddLog(u8"[책략5UISIG] Dialog::Initialize AddSig direct-call count=%u",found);
    }

    static bool EnsureFifthUiOnTrickSelectRuntimeHook() {
      if(g_fifthUiOnSelectRuntimeHookApplied)
        return true;

      const uintptr_t exeBase=
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kOnTrickSelectRva=0x01DF37B0;
      const uintptr_t hookAddr=exeBase+kOnTrickSelectRva;
      static const uint8_t expected[7]={
          0x4C,0x8B,0x41,0x20, // mov r8,[rcx+20]
          0x4D,0x85,0xC0       // test r8,r8
      };

      if(!IsValidPtr(hookAddr,sizeof(expected)) ||
         std::memcmp(reinterpret_cast<const void *>(hookAddr),
                     expected,sizeof(expected))!=0){
        AddLog(u8"[책략5UISELRT] OnTrickSelect entry 바이트 검증 실패.");
        return false;
      }

      const uintptr_t caveAddr=AllocNear(hookAddr,256);
      if(!caveAddr)
        return false;

      uint8_t *c=reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){c[i++]=v;};
      auto e32=[&](int32_t v){std::memcpy(c+i,&v,4);i+=4;};
      auto e64=[&](uintptr_t v){std::memcpy(c+i,&v,8);i+=8;};

      // Preserve every caller-saved GPR and XMM0..5 before calling the logger.
      e8(0x9C);                         // pushfq
      e8(0x50); e8(0x51); e8(0x52);   // push rax,rcx,rdx
      e8(0x41); e8(0x50);              // push r8
      e8(0x41); e8(0x51);              // push r9
      e8(0x41); e8(0x52);              // push r10
      e8(0x41); e8(0x53);              // push r11
      e8(0x48); e8(0x81); e8(0xEC); e32(0x80); // sub rsp,80

      const uint8_t xmmStores[][6]={
        {0xF3,0x0F,0x7F,0x44,0x24,0x20},
        {0xF3,0x0F,0x7F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x7F,0x54,0x24,0x40},
        {0xF3,0x0F,0x7F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x7F,0x64,0x24,0x60},
        {0xF3,0x0F,0x7F,0x6C,0x24,0x70}
      };
      for(const auto &b:xmmStores){
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      // RCX/EDX still hold original self/index.
      e8(0x48); e8(0xB8);
      e64(reinterpret_cast<uintptr_t>(&ProbeFifthOnTrickSelectRuntimeSeh));
      e8(0xFF); e8(0xD0);

      const uint8_t xmmLoads[][6]={
        {0xF3,0x0F,0x6F,0x44,0x24,0x20},
        {0xF3,0x0F,0x6F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x6F,0x54,0x24,0x40},
        {0xF3,0x0F,0x6F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x6F,0x64,0x24,0x60},
        {0xF3,0x0F,0x6F,0x6C,0x24,0x70}
      };
      for(const auto &b:xmmLoads){
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      e8(0x48); e8(0x81); e8(0xC4); e32(0x80);
      e8(0x41); e8(0x5B);
      e8(0x41); e8(0x5A);
      e8(0x41); e8(0x59);
      e8(0x41); e8(0x58);
      e8(0x5A); e8(0x59); e8(0x58);
      e8(0x9D);

      std::memcpy(c+i,expected,sizeof(expected));
      i+=(int)sizeof(expected);

      e8(0xE9);
      const intptr_t rel=
          static_cast<intptr_t>(hookAddr+sizeof(expected))-
          static_cast<intptr_t>(caveAddr+i+4);
      if(rel<INT32_MIN||rel>INT32_MAX){
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }
      e32(static_cast<int32_t>(rel));

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiOnSelectRuntimeOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_fifthUiOnSelectRuntimeOriginal));
      if(!ApplyJmp(hookAddr,caveAddr,sizeof(expected))){
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      g_fifthUiOnSelectRuntimeHookAddr=hookAddr;
      g_fifthUiOnSelectRuntimeCaveAddr=caveAddr;
      g_fifthUiOnSelectRuntimeHookApplied=true;
      AddLog(u8"[책략5UISELRT] OnTrickSelect index4 모델 probe 훅 설치 완료.");
      return true;
    }

    static bool EnsureFifthUiOnTrickSelectBoundHook() {
      if (g_fifthUiOnSelectBoundHookApplied || g_fifthUiOnSelectProbeDone)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kOnTrickSelectRva = 0x01DF37B0;
      constexpr size_t kOnTrickSelectSize = 0x4C;
      constexpr uintptr_t kGetTrickButtonRva = 0x01DAF060;
      const uintptr_t fn = exeBase + kOnTrickSelectRva;
      const uintptr_t getButton = exeBase + kGetTrickButtonRva;

      uint8_t code[kOnTrickSelectSize] = {};
      if (!SafeCopySeh(fn, code, sizeof(code)))
        return false;

      // Previous assumption proved wrong in live game: this symbol contains no
      // direct +1E0 access, no GetTrickButton call and no cmp ...,4. Dump this
      // tiny function exactly once so the real downstream selection path can
      // be followed without another broad scan.
      AddLog(u8"[책략5UISELPROBE] OnTrickSelect RVA=+1DF37B0 size=0x4C raw bytes:");
      for (size_t p=0; p<sizeof(code); p+=16) {
        const size_t chunk=((sizeof(code)-p)>16)?16:(sizeof(code)-p);
        char line[128]={};
        int pos=0;
        for(size_t j=0;j<chunk && pos<(int)sizeof(line)-4;++j)
          pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)code[p+j]);
        AddLog(u8"[책략5UISELPROBE] +%02llX : %s",
               (unsigned long long)p,line);
      }

      unsigned directButtonsDisp = 0;
      unsigned getButtonCalls = 0;
      unsigned cmp4Count = 0;
      uintptr_t cmpImmAddr = 0;
      unsigned directCallCount = 0;

      for (size_t i=0; i<sizeof(code); ++i) {
        if (i+4<=sizeof(code) &&
            code[i]==0xE0 && code[i+1]==0x01 &&
            code[i+2]==0x00 && code[i+3]==0x00)
          ++directButtonsDisp;

        if (i+3<=sizeof(code) &&
            code[i]==0x83 && (code[i+1]&0x38)==0x38 &&
            code[i+2]==0x04) {
          ++cmp4Count;
          cmpImmAddr=fn+i+2;
        }

        if (i+5<=sizeof(code) && code[i]==0xE8) {
          int32_t rel=0;
          std::memcpy(&rel,code+i+1,sizeof(rel));
          const uintptr_t target=fn+i+5+static_cast<intptr_t>(rel);
          ++directCallCount;
          if(target==getButton)
            ++getButtonCalls;

          uint8_t head[24]={};
          if (target>=exeBase && IsValidPtr(target,sizeof(head)) &&
              SafeCopySeh(target,head,sizeof(head))) {
            char line[192]={};
            int pos=0;
            for(size_t j=0;j<sizeof(head) && pos<(int)sizeof(line)-4;++j)
              pos+=sprintf_s(line+pos,sizeof(line)-pos,"%02X ",(unsigned)head[j]);
            AddLog(u8"[책략5UISELPROBE] call +%02llX -> RVA=+%llX head: %s",
                   (unsigned long long)i,
                   (unsigned long long)(target-exeBase),line);
          } else {
            AddLog(u8"[책략5UISELPROBE] call +%02llX -> %p",
                   (unsigned long long)i,reinterpret_cast<void *>(target));
          }
        }
      }

      AddLog(u8"[책략5UISELPROBE] summary: direct+1E0=%u getButtonCalls=%u cmp4=%u directCalls=%u",
             directButtonsDisp,getButtonCalls,cmp4Count,directCallCount);

      // Keep the originally planned narrow patch only if the exact safe shape
      // ever appears. Current live build is expected to take the probe-only path.
      if (directButtonsDisp==0 && getButtonCalls==1 && cmp4Count==1) {
        uint8_t current=0;
        if (!SafeCopySeh(cmpImmAddr,&current,1) || current!=0x04)
          return false;

        DWORD oldProtect=0,tmpProtect=0;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(cmpImmAddr),1,
                            PAGE_EXECUTE_READWRITE,&oldProtect))
          return false;
        *reinterpret_cast<uint8_t *>(cmpImmAddr)=0x05;
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<void *>(cmpImmAddr),1);
        VirtualProtect(reinterpret_cast<LPVOID>(cmpImmAddr),1,
                       oldProtect,&tmpProtect);

        uint8_t verify=0;
        if (!SafeCopySeh(cmpImmAddr,&verify,1) || verify!=0x05)
          return false;

        g_fifthUiOnSelectCmpImmAddr=cmpImmAddr;
        g_fifthUiOnSelectCmpOriginal=0x04;
        g_fifthUiOnSelectBoundHookApplied=true;
        AddLog(u8"[책략5UISEL] OnTrickSelect index 범위 4->5 확장 성공: RVA=+%llX",
               (unsigned long long)(cmpImmAddr-exeBase));
        return true;
      }

      // Not a failure of the already-working fifth UI. Mark the diagnostic as
      // complete so startup does not retry/spam this probe hundreds of times.
      g_fifthUiOnSelectProbeDone=true;
      AddLog(u8"[책략5UISEL] OnTrickSelect 직접 패치 보류. 실제 하위 호출 경로를 위 probe로 추적합니다.");
      return true;
    }

    static bool GetFifthUiLayoutMetricsSeh(uintptr_t layout,
                                           int *outStartX,
                                           int *outY,
                                           int *outStep) {
      if (!layout || !outStartX || !outY || !outStep)
        return false;

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

    static void CompactFifthUiButtonsAfterResetSeh(uintptr_t layout) {
      __try {
        if (g_fifthUiActiveLayout.load() != layout ||
            !g_fifthUiActiveButton.load() ||
            !ValidateFifthUiRegistrySeh(layout) ||
            !g_fifthUiId7Registered ||
            !layout || layout != g_fifthUiSidecarLayout ||
            !g_fifthUiSidecarButton ||
            !IsValidPtr(layout,0x2A8) ||
            !IsValidPtr(g_fifthUiSidecarButton,0x1D8))
          return;

        uintptr_t buttons[4]={};
        if(!SafeCopySeh(layout+0x1E0,buttons,sizeof(buttons)))
          return;

        int oldStart=0,oldStep=0,y=0;
        if(!GetFifthUiLayoutMetricsSeh(layout,&oldStart,&y,&oldStep))
          return;

        const int compactStep=(oldStep*7)/8; // 280 -> 245
        const int oldCenter=oldStart+(oldStep*3)/2;
        const int compactStart=oldCenter-compactStep*2;
        const int compactX[5]={
          compactStart,
          compactStart+compactStep,
          compactStart+compactStep*2,
          compactStart+compactStep*3,
          compactStart+compactStep*4
        };

        using SetXYFn=void(__fastcall *)(uintptr_t,int,int);
        uintptr_t all[5]={
          buttons[0],buttons[1],buttons[2],buttons[3],g_fifthUiSidecarButton
        };
        for(int n=0;n<5;++n){
          const uintptr_t b=all[n];
          if(!b || !IsValidPtr(b,sizeof(uintptr_t)))
            continue;
          const uintptr_t vt=*reinterpret_cast<const uintptr_t *>(b);
          if(!vt || !IsValidPtr(vt+0x90,sizeof(uintptr_t)))
            continue;
          const uintptr_t setPos=*reinterpret_cast<const uintptr_t *>(vt+0x90);
          if(setPos && IsValidPtr(setPos,1))
            reinterpret_cast<SetXYFn>(setPos)(b,compactX[n],y);
        }

        if(!g_fifthUiCompactLogged){
          g_fifthUiCompactLogged=true;
          AddLog(u8"[책략5UIRESET] ResetBtnPos 후 5버튼 재배치 완료: %d,%d,%d,%d,%d / y=%d",
                 compactX[0],compactX[1],compactX[2],compactX[3],compactX[4],y);
        }

        // Automatic read-only comparison after the proven ResetBtnPos hook.
        // This avoids asking the user to re-toggle the three experiment boxes
        // merely to reach UpdateStratagemFiveUiRuntimeProbe().
        if(!g_fifthUiResetSignalDumped){
          g_fifthUiResetSignalDumped=true;
          const uintptr_t sample[3]={buttons[0],buttons[3],g_fifthUiSidecarButton};
          const char *name[3]={"btn0","btn3","sidecar"};
          for(int s=0;s<3;++s){
            const uintptr_t b=sample[s];
            if(!b || !IsValidPtr(b,0x1D8)){
              AddLog(u8"[책략5UIAUTO] %s button invalid: %p",
                     name[s],reinterpret_cast<void *>(b));
              continue;
            }

            uint64_t sig[12]={};
            uint64_t tail[6]={};
            const bool sigOk=SafeCopySeh(b+0x78,sig,sizeof(sig));
            const bool tailOk=SafeCopySeh(b+0x1A8,tail,sizeof(tail));

            uint32_t uiId=0xFFFFFFFFu,state=0xFFFFFFFFu;
            SafeCopySeh(b+0x88,&uiId,sizeof(uiId));
            SafeCopySeh(b+0x8C,&state,sizeof(state));

            AddLog(u8"[책략5UIAUTO] %s button=%p uiId=%u state=%u sigOk=%d tailOk=%d",
                   name[s],reinterpret_cast<void *>(b),
                   (unsigned)uiId,(unsigned)state,sigOk?1:0,tailOk?1:0);

            if(sigOk){
              AddLog(u8"[책략5UIAUTO] %s +78..A7 = %016llX %016llX %016llX %016llX %016llX %016llX",
                     name[s],
                     (unsigned long long)sig[0],(unsigned long long)sig[1],
                     (unsigned long long)sig[2],(unsigned long long)sig[3],
                     (unsigned long long)sig[4],(unsigned long long)sig[5]);
              AddLog(u8"[책략5UIAUTO] %s +A8..D7 = %016llX %016llX %016llX %016llX %016llX %016llX",
                     name[s],
                     (unsigned long long)sig[6],(unsigned long long)sig[7],
                     (unsigned long long)sig[8],(unsigned long long)sig[9],
                     (unsigned long long)sig[10],(unsigned long long)sig[11]);
            }

            if(tailOk){
              AddLog(u8"[책략5UIAUTO] %s +1A8..1D7 = %016llX %016llX %016llX %016llX %016llX %016llX",
                     name[s],
                     (unsigned long long)tail[0],(unsigned long long)tail[1],
                     (unsigned long long)tail[2],(unsigned long long)tail[3],
                     (unsigned long long)tail[4],(unsigned long long)tail[5]);
            }
          }
          AddLog(u8"[책략5UIAUTO] callback index4Hits=%ld / 자동 비교 완료",
                 (long)g_fifthUiCallbackIndex4Hits);
        }
      } __except(EXCEPTION_EXECUTE_HANDLER) {
      }
    }


    static void RefreshFifthUiAtResetSeh(uintptr_t layout) {
      const uintptr_t dialog = g_trickUiDialog;
      if (dialog && layout &&
          IsValidPtr(dialog, 0x40) &&
          IsValidPtr(layout, 0x2A8)) {
        uintptr_t dialogLayout = 0;
        const bool layoutMatches =
            SafeReadPtrSeh(dialog + 0x08, &dialogLayout) &&
            dialogLayout == layout;

        if (layoutMatches &&
            g_id5CountRequested.load() &&
            g_fiveMetadataRequested.load()) {
          const FifthRuntimeStage before = g_fifthRuntimeStage.load();
          const bool ready = AdvanceFifthRuntimeStateSeh(dialog);
          const FifthRuntimeStage after = g_fifthRuntimeStage.load();

          AddLog(u8"[책략5STATE] ResetBtnPos 첫 오픈 진행: %u -> %u ready=%d dialog=%p layout=%p",
                 (unsigned)before, (unsigned)after, ready ? 1 : 0,
                 reinterpret_cast<void *>(dialog),
                 reinterpret_cast<void *>(layout));
        }
      }

      // Preserve the already-proven positioning/diagnostic behavior regardless
      // of whether the battle model was ready at this exact Open.
      CompactFifthUiButtonsAfterResetSeh(layout);
    }

    static bool EnsureFifthUiResetCompactHook() {
      if(g_fifthUiResetCompactHookApplied)
        return true;

      const uintptr_t exeBase=
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if(!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kResetBtnPosRva=0x01DAE9F0;
      constexpr uintptr_t kHookOffset=0x1F2;
      const uintptr_t hookAddr=exeBase+kResetBtnPosRva+kHookOffset;
      static const uint8_t expected[5]={0x48,0x8B,0x5C,0x24,0x30};

      if(!IsValidPtr(hookAddr,sizeof(expected)) ||
         std::memcmp(reinterpret_cast<const void *>(hookAddr),
                     expected,sizeof(expected))!=0){
        AddLog(u8"[책략5UIRESET] ResetBtnPos 종료 훅 바이트 검증 실패: %p",
               reinterpret_cast<void *>(hookAddr));
        return false;
      }

      const uintptr_t caveAddr=AllocNear(hookAddr,96);
      if(!caveAddr)
        return false;

      uint8_t *c=reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){c[i++]=v;};
      auto e32=[&](int32_t v){std::memcpy(c+i,&v,4);i+=4;};
      auto e64=[&](uintptr_t v){std::memcpy(c+i,&v,8);i+=8;};

      // At this point RSI == layout+0x200 because the original 4-button loop
      // started at layout+0x1E0 and advanced four qwords. This event is also
      // the first proven user-visible Open boundary, so retry the ordered ID5
      // state here before preserving the existing five-button compaction.
      e8(0x48); e8(0x8D); e8(0x8E); e32(-0x200); // lea rcx,[rsi-200]
      e8(0x48); e8(0xB8); e64(reinterpret_cast<uintptr_t>(&RefreshFifthUiAtResetSeh));
      e8(0xFF); e8(0xD0);

      std::memcpy(c+i,expected,sizeof(expected));
      i+=(int)sizeof(expected);

      e8(0xE9);
      const intptr_t rel=static_cast<intptr_t>(hookAddr+sizeof(expected))-
                         static_cast<intptr_t>(caveAddr+i+4);
      if(rel<INT32_MIN||rel>INT32_MAX){
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }
      e32(static_cast<int32_t>(rel));

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiResetCompactOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_fifthUiResetCompactOriginal));
      if(!ApplyJmp(hookAddr,caveAddr,sizeof(expected))){
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      g_fifthUiResetCompactHookAddr=hookAddr;
      g_fifthUiResetCompactCaveAddr=caveAddr;
      g_fifthUiResetCompactHookApplied=true;
      AddLog(u8"[책략5UIRESET] ResetBtnPos 종료 5버튼 재배치 훅 설치 완료.");
      return true;
    }


    static bool PrepareFifthUiDuringLayoutInitializeSeh(uintptr_t layout) {
      __try {
        if (!layout || !IsValidPtr(layout, 0x2A8))
          return false;

        if (ValidateFifthUiRegistrySeh(layout))
          return true;

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
          AddLog(u8"[책략5UILAYOUT] 4버튼 생성 직후 sidecar 준비 거부: layout=%p count=%u helper7=%p",
                 reinterpret_cast<void *>(layout), (unsigned)count,
                 reinterpret_cast<void *>(helper7));
          return false;
        }

        g_trickUiLayout = layout;

        if (!CreateFifthUiSidecarDisplayOnlySeh(layout)) {
          AddLog(u8"[책략5UILAYOUT] 4버튼 생성 직후 sidecar 생성 실패.");
          return false;
        }
        if (!ExpandMakerAndRegisterFifthSidecarSeh(layout)) {
          AddLog(u8"[책략5UILAYOUT] 4버튼 생성 직후 ID7 등록 실패.");
          return false;
        }

        AddLog(u8"[책략5UILAYOUT] Layout::Initialize 내부에서 5번째 생성/ID7 등록 완료: layout=%p button=%p",
               reinterpret_cast<void *>(layout),
               reinterpret_cast<void *>(g_fifthUiSidecarButton));
        return true;
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5UILAYOUT] Layout::Initialize 내부 sidecar 준비 중 예외.");
        return false;
      }
    }

    static bool EnsureFifthUiLayoutPostButtonsHook() {
      if (g_fifthUiLayoutPostButtonsHookApplied)
        return true;

      const uintptr_t exeBase =
          reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase || !IsSupportedTrickUiBuild(exeBase))
        return false;

      constexpr uintptr_t kLayoutInitializeRva = 0x01DAF350;
      // Runtime dump: +CEB = 0F 85 A5 FE FF FF, therefore the
      // following direct CALL begins at +CF1 (not +CF3).
      constexpr uintptr_t kHookOffset = 0x0CF1;
      const uintptr_t hookAddr = exeBase + kLayoutInitializeRva + kHookOffset;
      static const uint8_t expected[5] = {0xE8,0x8A,0x4E,0x26,0xFE};

      if (!IsValidPtr(hookAddr,sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(hookAddr),
                      expected,sizeof(expected)) != 0) {
        AddLog(u8"[책략5UILAYOUT] post-buttons hook 바이트 검증 실패: %p",
               reinterpret_cast<void *>(hookAddr));
        return false;
      }

      int32_t rel=0;
      std::memcpy(&rel,expected+1,sizeof(rel));
      const uintptr_t originalTarget =
          hookAddr + 5 + static_cast<intptr_t>(rel);

      const uintptr_t caveAddr=AllocNear(hookAddr,96);
      if(!caveAddr)
        return false;

      uint8_t *c=reinterpret_cast<uint8_t *>(caveAddr);
      int i=0;
      auto e8=[&](uint8_t v){c[i++]=v;};
      auto e32=[&](int32_t v){std::memcpy(c+i,&v,4);i+=4;};
      auto e64=[&](uintptr_t v){std::memcpy(c+i,&v,8);i+=8;};
      auto emitJmp=[&](uintptr_t target){
        e8(0xE9);
        const intptr_t r=static_cast<intptr_t>(target)-
                         static_cast<intptr_t>(caveAddr+i+4);
        if(r<INT32_MIN||r>INT32_MAX) return false;
        e32(static_cast<int32_t>(r));
        return true;
      };

      // RSI is the live layout here. The displaced instruction is itself a
      // direct call, so caller-saved registers are not live across this point.
      e8(0x48); e8(0x8B); e8(0xCE); // mov rcx,rsi
      e8(0x48); e8(0xB8); e64(reinterpret_cast<uintptr_t>(&PrepareFifthUiDuringLayoutInitializeSeh));
      e8(0xFF); e8(0xD0);           // call rax

      e8(0x48); e8(0xB8); e64(originalTarget);
      e8(0xFF); e8(0xD0);           // replay original call

      if(!emitJmp(hookAddr+5)) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(),c,i);
      std::memcpy(g_fifthUiLayoutPostButtonsOriginal,
                  reinterpret_cast<const void *>(hookAddr),
                  sizeof(g_fifthUiLayoutPostButtonsOriginal));
      if(!ApplyJmp(hookAddr,caveAddr,sizeof(expected))) {
        VirtualFree(reinterpret_cast<LPVOID>(caveAddr),0,MEM_RELEASE);
        return false;
      }

      g_fifthUiLayoutPostButtonsHookAddr=hookAddr;
      g_fifthUiLayoutPostButtonsCaveAddr=caveAddr;
      g_fifthUiLayoutPostButtonsHookApplied=true;
      AddLog(u8"[책략5UILAYOUT] Layout::Initialize 4버튼 생성 직후 훅 설치 완료.");
      return true;
    }

    static bool PrepareFifthUiBeforeCallbacksSeh(uintptr_t dialog) {
      __try {
        if (!dialog || !IsValidPtr(dialog, 0x40))
          return false;

        uintptr_t layout = 0;
        if (!SafeReadPtrSeh(dialog + 0x08, &layout) ||
            !layout || !IsValidPtr(layout, 0x2A8))
          return false;

        g_trickUiDialog = dialog;
        g_trickUiLayout = layout;

        // First advance DATA -> OWNER -> COUNT -> CAMP -> MODEL strictly from
        // verified state. This is event-driven and independent of wall-clock
        // timing or machine speed.
        const bool modelReady = AdvanceFifthRuntimeStateSeh(dialog);
        if (!modelReady) {
          AddLog(u8"[책략5STATE] callback 직전 단계 대기: stage=%u dialog=%p layout=%p",
                 (unsigned)g_fifthRuntimeStage.load(),
                 reinterpret_cast<void *>(dialog),
                 reinterpret_cast<void *>(layout));
          return true; // native callback continues; a later event can advance.
        }

        // N+1 <= 4 uses the game's existing physical button array. No sidecar
        // registration or index4 publication is needed in that case.
        if (g_fifthRuntimeOriginalCount < 4) {
          g_fifthUiActiveLayout.store(0);
          g_fifthUiActiveButton.store(0);
          SetFifthUiSidecarVisibleSeh(false);
          AddLog(u8"[책략5STATE] callback 직전 READY: N=%u total=%u native button index=%u",
                 (unsigned)g_fifthRuntimeOriginalCount,
                 (unsigned)(g_fifthRuntimeOriginalCount + 1),
                 (unsigned)g_fifthRuntimeOriginalCount);
          return true;
        }

        // Only N==4 needs the external fifth physical button. Register it only
        // after the model is already a verified five-entry model.
        if (g_fifthRuntimeOriginalCount != 4)
          return true;

        if (!ValidateFifthUiRegistrySeh(layout)) {
          uint32_t count = 0;
          uintptr_t owner = 0, helperTable = 0, helper7 = 0;
          const bool helperReady =
              SafeCopySeh(layout + 0x150, &count, sizeof(count)) &&
              count == 8 &&
              SafeReadPtrSeh(layout + 0x158, &owner) && owner == layout &&
              SafeReadPtrSeh(layout + 0x148, &helperTable) &&
              helperTable &&
              IsValidPtr(helperTable, 8 * sizeof(uintptr_t)) &&
              SafeReadPtrSeh(helperTable + 7 * sizeof(uintptr_t), &helper7) &&
              helper7 && IsValidPtr(helper7, sizeof(uintptr_t)) &&
              ValidatePreparedFifthUiHelper(layout);

          if (!helperReady) {
            AddLog(u8"[책략5STATE] N=4 model READY, sidecar helper 대기: layout=%p count=%u helper7=%p",
                   reinterpret_cast<void *>(layout), (unsigned)count,
                   reinterpret_cast<void *>(helper7));
            return true;
          }

          if (!CreateFifthUiSidecarDisplayOnlySeh(layout) ||
              !ExpandMakerAndRegisterFifthSidecarSeh(layout)) {
            AddLog(u8"[책략5STATE] N=4 sidecar 생성/등록 실패.");
            return true;
          }
        }

        // Re-publish now that registry is valid; this activates the index4
        // bridge and positions the five buttons.
        TryExtendFifthDialogModelCountSeh(dialog);

        AddLog(u8"[책략5STATE] callback 직전 READY: N=4 total=5 sidecar=%p active=%p",
               reinterpret_cast<void *>(g_fifthUiSidecarButton),
               reinterpret_cast<void *>(g_fifthUiActiveButton.load()));
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5STATE] callback 직전 상태 진행 중 예외.");
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

      const uintptr_t caveAddr=AllocNear(loadAddr,384);
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

      e8(0x9C);                         // pushfq - preserve original flags

      // The older +1C4 pre-callback point can still be too early: live logs
      // show the dialog owner/model becoming readable only by the time the
      // actual button callback loop starts here at +203. On the first loop
      // iteration, advance the ordered ID5 state immediately before native
      // callback binding. This is event-driven, not time-based.
      e8(0x85); e8(0xFF);             // test edi,edi
      e8(0x0F); e8(0x85);             // jne skip-state-call
      const int jneSkipState=i; e32(0);

      e8(0x50); e8(0x51); e8(0x52);   // push rax,rcx,rdx
      e8(0x41); e8(0x50);             // push r8
      e8(0x41); e8(0x51);             // push r9
      e8(0x41); e8(0x52);             // push r10
      e8(0x41); e8(0x53);             // push r11
      e8(0x48); e8(0x81); e8(0xEC); e32(0x80);

      const uint8_t stateXmmStores[][6] = {
        {0xF3,0x0F,0x7F,0x44,0x24,0x20},
        {0xF3,0x0F,0x7F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x7F,0x54,0x24,0x40},
        {0xF3,0x0F,0x7F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x7F,0x64,0x24,0x60},
        {0xF3,0x0F,0x7F,0x6C,0x24,0x70}
      };
      for (const auto &b : stateXmmStores) {
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      e8(0x48); e8(0x8B); e8(0xCE);    // mov rcx,rsi (dialog)
      e8(0x48); e8(0xB8);
      e64(reinterpret_cast<uintptr_t>(&PrepareFifthUiBeforeCallbacksSeh));
      e8(0xFF); e8(0xD0);              // call rax

      const uint8_t stateXmmLoads[][6] = {
        {0xF3,0x0F,0x6F,0x44,0x24,0x20},
        {0xF3,0x0F,0x6F,0x4C,0x24,0x30},
        {0xF3,0x0F,0x6F,0x54,0x24,0x40},
        {0xF3,0x0F,0x6F,0x5C,0x24,0x50},
        {0xF3,0x0F,0x6F,0x64,0x24,0x60},
        {0xF3,0x0F,0x6F,0x6C,0x24,0x70}
      };
      for (const auto &b : stateXmmLoads) {
        std::memcpy(c+i,b,sizeof(b)); i+=(int)sizeof(b);
      }

      e8(0x48); e8(0x81); e8(0xC4); e32(0x80);
      e8(0x41); e8(0x5B);
      e8(0x41); e8(0x5A);
      e8(0x41); e8(0x59);
      e8(0x41); e8(0x58);
      e8(0x5A); e8(0x59); e8(0x58);

      const int skipStateLabel=i;

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
      // Replay the original first load so RAX is the current dialog layout.
      e8(0x48); e8(0x8B); e8(0x46); e8(0x08); // mov rax,[rsi+8]
      e8(0x48); e8(0xBB);              // mov rbx,&layout-global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveLayout));
      e8(0x48); e8(0x3B); e8(0x03);    // cmp rax,[rbx]
      e8(0x0F); e8(0x85);              // jne no-sidecar
      const int jneLayoutExit=i; e32(0);

      e8(0x50);                         // push rax
      e8(0x48); e8(0xB8);              // mov rax,&hitCounter
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiCallbackIndex4Hits));
      e8(0xF0); e8(0xFF); e8(0x00);    // lock inc dword ptr [rax]
      e8(0x58);                         // pop rax
      e8(0x48); e8(0xBB);              // mov rbx,&button-global
      e64(reinterpret_cast<uintptr_t>(&g_fifthUiActiveButton));
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

      if(!rel32(jneSkipState,skipStateLabel) ||
         !rel32(jeSide,sideLabel) ||
         !rel32(jneLayoutExit,noSideLabel) ||
         !rel32(jzExit,noSideLabel)) {
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

    static void SetFifthUiSidecarVisibleSeh(bool visible) {
      const uintptr_t button = g_fifthUiSidecarButton;
      if (!button || !IsValidPtr(button, sizeof(uintptr_t)))
        return;

      __try {
        const uintptr_t vt =
            *reinterpret_cast<const uintptr_t *>(button);
        if (!vt || !IsValidPtr(vt + 0x108, sizeof(uintptr_t)))
          return;

        const uintptr_t setVisible =
            *reinterpret_cast<const uintptr_t *>(vt + 0x108);
        if (!setVisible || !IsValidPtr(setVisible, 1))
          return;

        using SetBoolFn = void(__fastcall *)(uintptr_t, bool);
        reinterpret_cast<SetBoolFn>(setVisible)(button, visible);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
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

    static void BeginFifthUiNativeInitialize(uintptr_t layout) {
      // Native Layout::Initialize is the generation boundary for UI controls.
      // Retire all published sidecar references before CUIMaker rebuilds the
      // same address or a new layout. Never dereference the previous controls.
      g_fifthUiActiveLayout.store(0);
      g_fifthUiActiveButton.store(0);

      g_fifthUiSidecarLayout = 0;
      g_fifthUiSidecarButton = 0;
      g_fifthUiSidecarAttempted = false;
      g_fifthUiId7Registered = false;
      g_fifthUiRegisteredEpoch = 0;
      std::memset(g_fifthUiOriginalButtons, 0,
                  sizeof(g_fifthUiOriginalButtons));

      g_fifthUiMakerExpanded = false;
      g_fifthUiMakerAddr = 0;
      std::memset(g_fifthUiMakerOriginal, 0,
                  sizeof(g_fifthUiMakerOriginal));

      g_fifthUiCompactLogged = false;
      g_fifthUiResetSignalDumped = false;
      g_fifthUiOnSelectRuntimeLogged = 0;
      g_fifthUiOnSelectAnyHits = 0;
      InterlockedExchange(&g_fifthUiCallbackIndex4Hits, 0);

      if (layout) {
        g_trickUiLayout = layout;
        g_lastLoggedUiLayout = 0;
      }
    }

    static bool ValidateFifthUiRegistrySeh(uintptr_t layout) {
      if (!layout ||
          !g_fifthUiId7Registered ||
          g_fifthUiSidecarLayout != layout ||
          !g_fifthUiSidecarButton ||
          !IsValidPtr(layout, 0x2A8) ||
          !IsValidPtr(g_fifthUiSidecarButton, 0x1D8))
        return false;

      __try {
        const TrickUiInitTrace trace = ReadTrickUiInitTrace();
        if (!trace.hit ||
            g_fifthUiRegisteredEpoch != trace.hit ||
            trace.owner != layout ||
            trace.maker != layout + 0x140)
          return false;

        uintptr_t currentButtons[4] = {};
        if (!SafeCopySeh(layout + 0x1E0, currentButtons,
                         sizeof(currentButtons)) ||
            std::memcmp(currentButtons, g_fifthUiOriginalButtons,
                        sizeof(currentButtons)) != 0)
          return false;

        const uintptr_t exeBase =
            reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
        if (!exeBase)
          return false;

        constexpr uintptr_t kLookupLayoutRva = 0x01D15440;
        using LookupLayoutFn = uintptr_t(__fastcall *)(uintptr_t, int);
        const auto lookup =
            reinterpret_cast<LookupLayoutFn>(exeBase + kLookupLayoutRva);

        const uintptr_t maker = layout + 0x140;
        uintptr_t owner = 0;
        uint32_t count = 0;
        if (!SafeCopySeh(maker + 0x10, &count, sizeof(count)) ||
            !SafeReadPtrSeh(maker + 0x18, &owner) ||
            count != 8 || owner != layout ||
            lookup(maker, 7) != g_fifthUiSidecarButton)
          return false;

        uint32_t id = 0xFFFFFFFFu;
        uint32_t state = 0xFFFFFFFFu;
        if (!SafeCopySeh(g_fifthUiSidecarButton + 0x88, &id, sizeof(id)) ||
            !SafeCopySeh(g_fifthUiSidecarButton + 0x8C, &state, sizeof(state)))
          return false;

        return id == 7 && state == 1;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
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
        return ValidateFifthUiRegistrySeh(layout);

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
        const int compactStep=(oldStep*7)/8;
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
        g_fifthUiRegisteredEpoch=ReadTrickUiInitTrace().hit;
        std::memcpy(g_fifthUiOriginalButtons,buttons,sizeof(buttons));

        // Callback registration is a one-shot Dialog::Initialize event and can
        // happen before the battle model is ready. Publish the verified ID7
        // sidecar immediately for callback setup only; visibility is still
        // controlled later by the resolved native count N.
        g_fifthUiActiveLayout.store(layout);
        g_fifthUiActiveButton.store(g_fifthUiSidecarButton);

        // RegisterLayout may apply the descriptor's initial visibility. Keep
        // the sidecar hidden until N is resolved; callback binding does not
        // require it to be visible.
        const uintptr_t sidecarVt =
            *reinterpret_cast<const uintptr_t *>(g_fifthUiSidecarButton);
        if (sidecarVt && IsValidPtr(sidecarVt + 0x108, sizeof(uintptr_t))) {
          const uintptr_t setVisible =
              *reinterpret_cast<const uintptr_t *>(sidecarVt + 0x108);
          if (setVisible && IsValidPtr(setVisible, 1)) {
            using SetBoolFn = void(__fastcall *)(uintptr_t, bool);
            reinterpret_cast<SetBoolFn>(setVisible)(
                g_fifthUiSidecarButton, false);
          }
        }

        const bool getButtonHookReady = EnsureFifthUiGetTrickButtonHook();
        const bool openHookReady = EnsureFifthUiOpenDisplayHook();
        AddLog(u8"[책략5UICB] callback loop 조기 훅=%s / index4Hits=%ld",
               g_fifthUiCallbackLoopHookApplied ? "READY" : "FAILED",
               (long)g_fifthUiCallbackIndex4Hits);
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
          IsValidPtr(g_fifthUiSidecarButton, 0x1D8)) {
        SetFifthUiSidecarVisibleSeh(false);
        AddLog(u8"[책략5수명] 재사용 layout=%p: 기존 sidecar=%p 재표시. 새 버튼 생성 안 함.",
               reinterpret_cast<void *>(layout),
               reinterpret_cast<void *>(g_fifthUiSidecarButton));
        return true;
      }

      if (g_fifthUiSidecarLayout != layout) {
        // Previous layout lifetime has ended. Never dereference its model/sidecar
        // pointers during save/load; only drop bookkeeping and bind the new owner.
        g_fifthUiModelCountAddr = 0;
        g_fifthUiModelCountOriginal = 0;
        g_fifthUiModelCountApplied = false;
        g_fifthUiModelEntryAddr = 0;
        g_fifthUiModelEntryOriginal[0] = 0;
        g_fifthUiModelEntryOriginal[1] = 0;
        g_fifthUiModelEntryApplied = false;
        g_trickUiDialog = 0;
        g_lastLoggedUiLayout = 0;
        g_fifthUiCompactLogged = false;
        g_fifthUiResetSignalDumped = false;
        InterlockedExchange(&g_fifthUiCallbackIndex4Hits, 0);

        // Do not carry the previous maker snapshot forward.
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
        const int compactStep = (oldStep * 7) / 8;
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
            reinterpret_cast<SetBoolFn>(setVisible)(button, false);
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

    static bool TryRecaptureBattleInfoFromDialogSeh() {
      const uintptr_t dialog = g_trickUiDialog;
      if (!dialog || !IsValidPtr(dialog, 0x40))
        return false;

      __try {
        uintptr_t holder = 0;
        uintptr_t inner = 0;
        if (!SafeReadPtrSeh(dialog + 0x20, &holder) ||
            !holder ||
            !IsValidPtr(holder, sizeof(uintptr_t)) ||
            !SafeReadPtrSeh(holder, &inner) ||
            !inner)
          return false;

        const uint8_t side =
            *reinterpret_cast<const uint8_t *>(inner + kSideOffset);
        if (side > 1 || !ValidateInfo(inner, side))
          return false;

        const uintptr_t current =
            (side == 0) ? static_cast<uintptr_t>(g_attackInfo)
                        : static_cast<uintptr_t>(g_defenseInfo);
        if (current != inner) {
          if (side == 0)
            g_attackInfo = inner;
          else
            g_defenseInfo = inner;

          AddLog(u8"[책략5수명] dialog runtime model에서 현재 %s Camp::Impl 재캡처: %p",
                 side == 0 ? u8"공격측" : u8"수비측",
                 reinterpret_cast<void *>(inner));
        }
        return true;
      } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    static bool TryResolveFifthMetadataTableFromOwnerSeh(
        uintptr_t owner, uintptr_t *outTable = nullptr) {
      if (!owner)
        return false;

      __try {
        const uintptr_t modelBase = owner + 0xF0;
        uint32_t count = 0;
        StratagemFiveModel::Entry entries[5] = {};
        if (!IsValidPtr(modelBase + 0x10, sizeof(entries)) ||
            !IsValidPtr(modelBase + 0x60, sizeof(count)) ||
            !SafeCopySeh(modelBase + 0x10, entries, sizeof(entries)) ||
            !SafeCopySeh(modelBase + 0x60, &count, sizeof(count)) ||
            count < 1 || count > 5)
          return false;

        uintptr_t table = 0;
        for (uint32_t i = 0; i < count && !table; ++i) {
          const uintptr_t row = entries[i].data;
          if (!row || !IsValidPtr(row + 0x0C, 1))
            continue;

          const uint8_t a = *reinterpret_cast<const uint8_t *>(row + 0x08);
          const uint8_t b = *reinterpret_cast<const uint8_t *>(row + 0x0A);
          const uint8_t d = *reinterpret_cast<const uint8_t *>(row + 0x0C);
          if (a != b || a != d || a < 1 || a > 5)
            continue;

          const uintptr_t candidate =
              row - static_cast<uintptr_t>(a - 1) * 0x20;
          if (!IsValidPtr(candidate, 5 * 0x20))
            continue;

          bool valid = true;
          for (uint32_t n = 0; n < 5; ++n) {
            const uintptr_t r = candidate + static_cast<uintptr_t>(n) * 0x20;
            const uint8_t expected = static_cast<uint8_t>(n + 1);
            const uint8_t x = *reinterpret_cast<const uint8_t *>(r + 0x08);
            const uint8_t y = *reinterpret_cast<const uint8_t *>(r + 0x0A);
            const uint8_t z = *reinterpret_cast<const uint8_t *>(r + 0x0C);
            if (x != expected || y != expected || z != expected) {
              valid = false;
              break;
            }
          }
          if (!valid)
            continue;

          for (uint32_t n = 0; n < count; ++n) {
            uint32_t rowId = 0;
            if (!StratagemFiveModel::RowId(entries[n].data,
                                             candidate, rowId)) {
              valid = false;
              break;
            }
          }
          if (valid)
            table = candidate;
        }

        if (!table)
          return false;

        g_fiveMetadataTable = table;
        g_fiveMetadataAddr = table + 4 * 0x20;
        g_fiveMetadataApplied = true;
        if (outTable)
          *outTable = table;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    static bool TryApplyFifthCampForOwnerSeh(uintptr_t owner) {
      if (!owner || !g_fiveMetadataTable || !g_fiveMetadataAddr ||
          !g_id5CountApplied || g_id5CountOwner != owner)
        return false;

      if (g_fiveRuntimeSlotApplied &&
          g_fiveRuntimeOwner == owner &&
          g_fiveRuntimeSlotAddr &&
          IsValidPtr(g_fiveRuntimeSlotAddr, sizeof(uintptr_t)) &&
          *reinterpret_cast<const uintptr_t *>(g_fiveRuntimeSlotAddr) ==
              g_fiveMetadataAddr)
        return true;

      StratagemFiveModel::Plan plan{};
      StratagemFiveModel::Entry entries[5] = {};
      uintptr_t campRows[5] = {};
      uintptr_t modelBase = 0;
      uintptr_t tricksAddr = 0;
      if (!TryReadFifthPlanForOwnerSeh(owner, &plan, &entries, &campRows,
                                        &modelBase, &tricksAddr) ||
          plan.originalCount > 4 || plan.campSlot >= 5)
        return false;

      const uintptr_t slotAddr =
          tricksAddr + static_cast<uintptr_t>(plan.campSlot) *
                           sizeof(uintptr_t);
      if (!IsValidPtr(slotAddr, sizeof(uintptr_t)))
        return false;

      const uintptr_t oldValue =
          *reinterpret_cast<const uintptr_t *>(slotAddr);
      if (oldValue != 0 && oldValue != g_fiveMetadataAddr)
        return false;

      if (oldValue != g_fiveMetadataAddr) {
        DWORD oldProtect = 0;
        DWORD tmpProtect = 0;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(slotAddr),
                            sizeof(uintptr_t), PAGE_READWRITE, &oldProtect))
          return false;
        *reinterpret_cast<uintptr_t *>(slotAddr) = g_fiveMetadataAddr;
        VirtualProtect(reinterpret_cast<LPVOID>(slotAddr),
                       sizeof(uintptr_t), oldProtect, &tmpProtect);
        if (*reinterpret_cast<const uintptr_t *>(slotAddr) !=
            g_fiveMetadataAddr)
          return false;
      }

      g_fiveRuntimeSlotAddr = slotAddr;
      g_fiveRuntimeSlotOriginal = oldValue;
      g_fiveRuntimeSlotApplied = true;
      g_fiveRuntimeOwner = owner;
      g_fifthRuntimeOriginalCount = plan.originalCount;
      return true;
    }

    static bool TryReadFifthPlanForOwnerSeh(
        uintptr_t owner,
        StratagemFiveModel::Plan *outPlan,
        StratagemFiveModel::Entry (*outEntries)[5] = nullptr,
        uintptr_t (*outCampRows)[5] = nullptr,
        uintptr_t *outModelBase = nullptr,
        uintptr_t *outTricksAddr = nullptr) {
      if (!owner || !outPlan || !g_fiveMetadataTable)
        return false;

      __try {
        constexpr uintptr_t kCampImplCampDataOffset = 0x10;
        constexpr uintptr_t kCampDataTricksOffset = 0x68;
        constexpr uintptr_t kTrickerOffset = 0xF0;
        constexpr uintptr_t kEntriesOffset = 0x10;
        constexpr uintptr_t kCountOffset = 0x60;

        const uintptr_t modelBase = owner + kTrickerOffset;
        if (!IsValidPtr(modelBase + kEntriesOffset,
                        5 * sizeof(StratagemFiveModel::Entry)) ||
            !IsValidPtr(modelBase + kCountOffset, sizeof(uint32_t)))
          return false;

        uintptr_t campData = 0;
        if (!SafeReadPtrSeh(owner + kCampImplCampDataOffset, &campData) ||
            !campData)
          return false;

        const uintptr_t tricksAddr = campData + kCampDataTricksOffset;
        if (!IsValidPtr(tricksAddr, 5 * sizeof(uintptr_t)))
          return false;

        StratagemFiveModel::Entry entries[5] = {};
        uintptr_t campRows[5] = {};
        uint32_t count = 0;
        if (!SafeCopySeh(modelBase + kEntriesOffset, entries, sizeof(entries)) ||
            !SafeCopySeh(modelBase + kCountOffset, &count, sizeof(count)) ||
            !SafeCopySeh(tricksAddr, campRows, sizeof(campRows)))
          return false;

        StratagemFiveModel::Plan plan{};
        if (!StratagemFiveModel::Prepare(
                entries, count, campRows, g_fiveMetadataTable, plan))
          return false;

        *outPlan = plan;
        if (outEntries)
          std::memcpy(*outEntries, entries, sizeof(entries));
        if (outCampRows)
          std::memcpy(*outCampRows, campRows, sizeof(campRows));
        if (outModelBase)
          *outModelBase = modelBase;
        if (outTricksAddr)
          *outTricksAddr = tricksAddr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    static void DiscardFifthAiProbeState() {
      g_fifthAiProbeApplied = false;
      g_fifthAiProbeOwner = 0;
      g_fifthAiProbeEntryAddr = 0;
      g_fifthAiProbeCampSlotAddr = 0;
      g_fifthAiProbeOriginalData = 0;
      g_fifthAiProbeOriginalCampRow = 0;
      g_fifthAiProbeInitialAvailable = 0;
      g_fifthAiProbeLastAvailable = 0;
      g_fifthAiProbeIndex = UINT32_MAX;
      g_fifthAiProbeConsumptionLogged = false;
    }

    static bool RestoreFifthAiProbeSeh() {
      if (!g_fifthAiProbeApplied)
        return true;

      bool entryRestored = false;
      bool campRestored = false;

      __try {
        if (g_fifthAiProbeEntryAddr &&
            IsValidPtr(g_fifthAiProbeEntryAddr, sizeof(uintptr_t))) {
          DWORD oldProtect = 0;
          DWORD tmpProtect = 0;
          if (VirtualProtect(
                  reinterpret_cast<LPVOID>(g_fifthAiProbeEntryAddr),
                  sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
            *reinterpret_cast<uintptr_t *>(g_fifthAiProbeEntryAddr) =
                g_fifthAiProbeOriginalData;
            VirtualProtect(
                reinterpret_cast<LPVOID>(g_fifthAiProbeEntryAddr),
                sizeof(uintptr_t), oldProtect, &tmpProtect);
            entryRestored =
                *reinterpret_cast<const uintptr_t *>(
                    g_fifthAiProbeEntryAddr) ==
                g_fifthAiProbeOriginalData;
          }
        }

        if (g_fifthAiProbeCampSlotAddr &&
            IsValidPtr(g_fifthAiProbeCampSlotAddr, sizeof(uintptr_t))) {
          DWORD oldProtect = 0;
          DWORD tmpProtect = 0;
          if (VirtualProtect(
                  reinterpret_cast<LPVOID>(g_fifthAiProbeCampSlotAddr),
                  sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
            *reinterpret_cast<uintptr_t *>(g_fifthAiProbeCampSlotAddr) =
                g_fifthAiProbeOriginalCampRow;
            VirtualProtect(
                reinterpret_cast<LPVOID>(g_fifthAiProbeCampSlotAddr),
                sizeof(uintptr_t), oldProtect, &tmpProtect);
            campRestored =
                *reinterpret_cast<const uintptr_t *>(
                    g_fifthAiProbeCampSlotAddr) ==
                g_fifthAiProbeOriginalCampRow;
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        entryRestored = false;
        campRestored = false;
      }

      AddLog(u8"[책략5AIPROBE] 원복: entry=%d camp=%d owner=%p slot=%u",
             entryRestored ? 1 : 0, campRestored ? 1 : 0,
             reinterpret_cast<void *>(g_fifthAiProbeOwner),
             (unsigned)g_fifthAiProbeIndex);

      DiscardFifthAiProbeState();
      return entryRestored && campRestored;
    }

    static bool TryArmFifthAiProbeSeh() {
      if (g_fifthAiProbeApplied)
        return true;
      if (!g_fiveMetadataAddr || !g_fiveMetadataTable)
        return false;

      uintptr_t playerOwner = 0;
      if (!TryGetDialogCampOwnerSeh(&playerOwner) || !playerOwner)
        return false;

      uintptr_t aiOwner = 0;
      uint8_t aiSide = 0xFF;
      const uintptr_t attack = static_cast<uintptr_t>(g_attackInfo);
      const uintptr_t defense = static_cast<uintptr_t>(g_defenseInfo);

      if (attack && attack != playerOwner && ValidateInfo(attack, 0)) {
        aiOwner = attack;
        aiSide = 0;
      }
      if (defense && defense != playerOwner && ValidateInfo(defense, 1)) {
        if (aiOwner && aiOwner != defense)
          return false;
        aiOwner = defense;
        aiSide = 1;
      }
      if (!aiOwner)
        return false;

      uintptr_t campData = 0;
      uintptr_t modelBase = aiOwner + 0xF0;
      uint32_t count = 0;
      StratagemFiveModel::Entry entries[5] = {};
      uintptr_t campRows[5] = {};
      uintptr_t tricksAddr = 0;

      __try {
        if (!IsValidPtr(modelBase + 0x10, sizeof(entries)) ||
            !IsValidPtr(modelBase + 0x60, sizeof(count)) ||
            !SafeCopySeh(modelBase + 0x10, entries, sizeof(entries)) ||
            !SafeCopySeh(modelBase + 0x60, &count, sizeof(count)) ||
            count < 1 || count > 4 ||
            !SafeReadPtrSeh(aiOwner + 0x10, &campData) ||
            !campData)
          return false;

        tricksAddr = campData + 0x68;
        if (!IsValidPtr(tricksAddr, sizeof(campRows)) ||
            !SafeCopySeh(tricksAddr, campRows, sizeof(campRows)))
          return false;

        for (uint32_t i = 0; i < count; ++i) {
          if (entries[i].data == g_fiveMetadataAddr) {
            AddLog(u8"[책략5AIPROBE] AI측에 이미 ID5 존재: side=%u owner=%p slot=%u available=%u",
                   (unsigned)aiSide, reinterpret_cast<void *>(aiOwner),
                   (unsigned)i, (unsigned)entries[i].available);
            return false;
          }
        }

        uint32_t probeIndex = UINT32_MAX;
        for (uint32_t i = count; i > 0; --i) {
          const uint32_t idx = i - 1;
          if (entries[idx].available > 0 &&
              entries[idx].data &&
              campRows[idx] == entries[idx].data) {
            probeIndex = idx;
            break;
          }
        }
        if (probeIndex == UINT32_MAX) {
          AddLog(u8"[책략5AIPROBE] AI측 교체 가능한 가용 슬롯 없음: side=%u owner=%p count=%u",
                 (unsigned)aiSide, reinterpret_cast<void *>(aiOwner),
                 (unsigned)count);
          return false;
        }

        const uintptr_t entryAddr =
            modelBase + 0x10 +
            static_cast<uintptr_t>(probeIndex) *
                sizeof(StratagemFiveModel::Entry);
        const uintptr_t campSlotAddr =
            tricksAddr +
            static_cast<uintptr_t>(probeIndex) * sizeof(uintptr_t);

        if (!IsValidPtr(entryAddr, sizeof(StratagemFiveModel::Entry)) ||
            !IsValidPtr(campSlotAddr, sizeof(uintptr_t)))
          return false;

        const uintptr_t originalData = entries[probeIndex].data;
        const uintptr_t originalCampRow = campRows[probeIndex];
        uint32_t originalRowId = 0;
        const bool originalRowOk =
            StratagemFiveModel::RowId(
                originalData, g_fiveMetadataTable, originalRowId);

        DWORD entryProtect = 0;
        DWORD entryTmp = 0;
        if (!VirtualProtect(
                reinterpret_cast<LPVOID>(entryAddr), sizeof(uintptr_t),
                PAGE_READWRITE, &entryProtect))
          return false;
        *reinterpret_cast<uintptr_t *>(entryAddr) = g_fiveMetadataAddr;
        VirtualProtect(reinterpret_cast<LPVOID>(entryAddr), sizeof(uintptr_t),
                       entryProtect, &entryTmp);
        if (*reinterpret_cast<const uintptr_t *>(entryAddr) !=
            g_fiveMetadataAddr)
          return false;

        DWORD campProtect = 0;
        DWORD campTmp = 0;
        if (!VirtualProtect(
                reinterpret_cast<LPVOID>(campSlotAddr), sizeof(uintptr_t),
                PAGE_READWRITE, &campProtect)) {
          DWORD restoreProtect = 0;
          DWORD restoreTmp = 0;
          if (VirtualProtect(
                  reinterpret_cast<LPVOID>(entryAddr), sizeof(uintptr_t),
                  PAGE_READWRITE, &restoreProtect)) {
            *reinterpret_cast<uintptr_t *>(entryAddr) = originalData;
            VirtualProtect(reinterpret_cast<LPVOID>(entryAddr),
                           sizeof(uintptr_t), restoreProtect, &restoreTmp);
          }
          return false;
        }
        *reinterpret_cast<uintptr_t *>(campSlotAddr) = g_fiveMetadataAddr;
        VirtualProtect(reinterpret_cast<LPVOID>(campSlotAddr),
                       sizeof(uintptr_t), campProtect, &campTmp);

        if (*reinterpret_cast<const uintptr_t *>(campSlotAddr) !=
            g_fiveMetadataAddr) {
          DWORD restoreProtect = 0;
          DWORD restoreTmp = 0;
          if (VirtualProtect(
                  reinterpret_cast<LPVOID>(entryAddr), sizeof(uintptr_t),
                  PAGE_READWRITE, &restoreProtect)) {
            *reinterpret_cast<uintptr_t *>(entryAddr) = originalData;
            VirtualProtect(reinterpret_cast<LPVOID>(entryAddr),
                           sizeof(uintptr_t), restoreProtect, &restoreTmp);
          }
          return false;
        }

        g_fifthAiProbeApplied = true;
        g_fifthAiProbeOwner = aiOwner;
        g_fifthAiProbeEntryAddr = entryAddr;
        g_fifthAiProbeCampSlotAddr = campSlotAddr;
        g_fifthAiProbeOriginalData = originalData;
        g_fifthAiProbeOriginalCampRow = originalCampRow;
        g_fifthAiProbeInitialAvailable = entries[probeIndex].available;
        g_fifthAiProbeLastAvailable = entries[probeIndex].available;
        g_fifthAiProbeIndex = probeIndex;
        g_fifthAiProbeConsumptionLogged = false;

        AddLog(u8"[책략5AIPROBE] AI측 슬롯을 ID5로 치환: side=%u owner=%p count=%u slot=%u originalRow=%s%u available=%u -> row5",
               (unsigned)aiSide, reinterpret_cast<void *>(aiOwner),
               (unsigned)count, (unsigned)probeIndex,
               originalRowOk ? "" : "INVALID/",
               originalRowOk ? (unsigned)originalRowId : 0u,
               (unsigned)entries[probeIndex].available);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    static void UpdateFifthAiProbeSeh() {
      if (!g_fifthAiProbeApplied ||
          !g_fifthAiProbeEntryAddr ||
          g_fifthAiProbeIndex >= 5)
        return;

      __try {
        if (!IsValidPtr(g_fifthAiProbeEntryAddr,
                        sizeof(StratagemFiveModel::Entry)))
          return;

        StratagemFiveModel::Entry current{};
        if (!SafeCopySeh(g_fifthAiProbeEntryAddr,
                         &current, sizeof(current)))
          return;

        if (current.data != g_fiveMetadataAddr) {
          AddLog(u8"[책략5AIPROBE] AI측 probe 슬롯 데이터가 변경됨: owner=%p slot=%u data=%p",
                 reinterpret_cast<void *>(g_fifthAiProbeOwner),
                 (unsigned)g_fifthAiProbeIndex,
                 reinterpret_cast<void *>(current.data));
          return;
        }

        const uint8_t available = current.available;
        if (available != g_fifthAiProbeLastAvailable) {
          AddLog(u8"[책략5AIPROBE] AI측 ID5 available 변화: slot=%u %u -> %u",
                 (unsigned)g_fifthAiProbeIndex,
                 (unsigned)g_fifthAiProbeLastAvailable,
                 (unsigned)available);
          if (available < g_fifthAiProbeLastAvailable &&
              !g_fifthAiProbeConsumptionLogged) {
            g_fifthAiProbeConsumptionLogged = true;
            AddLog(u8"[책략5AIPROBE] AI가 ID5 슬롯을 소비한 정황 확인. 실제 사기/효과 발생 여부도 확인하세요.");
          }
          g_fifthAiProbeLastAvailable = available;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    static void LogFifthPlanSnapshotSeh(
        uintptr_t owner, const char *reason) {
      static uintptr_t s_lastOwner = 0;
      static ULONGLONG s_lastTick = 0;

      const ULONGLONG now = GetTickCount64();
      if (!owner || (owner == s_lastOwner && now - s_lastTick < 3000))
        return;
      s_lastOwner = owner;
      s_lastTick = now;

      __try {
        const uintptr_t modelBase = owner + 0xF0;
        uintptr_t campData = 0;
        uint32_t count = 0;
        StratagemFiveModel::Entry entries[5] = {};
        uintptr_t campRows[5] = {};

        const bool modelOk =
            IsValidPtr(modelBase + 0x10, sizeof(entries)) &&
            IsValidPtr(modelBase + 0x60, sizeof(count)) &&
            SafeCopySeh(modelBase + 0x10, entries, sizeof(entries)) &&
            SafeCopySeh(modelBase + 0x60, &count, sizeof(count));

        const bool campOk =
            SafeReadPtrSeh(owner + 0x10, &campData) &&
            campData &&
            IsValidPtr(campData + 0x68, sizeof(campRows)) &&
            SafeCopySeh(campData + 0x68, campRows, sizeof(campRows));

        AddLog(u8"[책략5NDBG] plan 실패 snapshot: reason=%s owner=%p modelOk=%d campOk=%d count=%u table=%p",
               reason ? reason : "?",
               reinterpret_cast<void *>(owner),
               modelOk ? 1 : 0, campOk ? 1 : 0,
               (unsigned)count,
               reinterpret_cast<void *>(g_fiveMetadataTable));

        if (modelOk) {
          for (int i = 0; i < 5; ++i) {
            uint32_t rowId = 0;
            const bool rowOk =
                StratagemFiveModel::RowId(
                    entries[i].data, g_fiveMetadataTable, rowId);
            AddLog(u8"[책략5NDBG] entry%d data=%p rowId=%s%u index=%u available=%u",
                   i,
                   reinterpret_cast<void *>(entries[i].data),
                   rowOk ? "" : "INVALID/",
                   rowOk ? (unsigned)rowId : 0u,
                   (unsigned)entries[i].index,
                   (unsigned)entries[i].available);
          }
        }

        if (campOk) {
          AddLog(u8"[책략5NDBG] CampData rows=%p,%p,%p,%p,%p",
                 reinterpret_cast<void *>(campRows[0]),
                 reinterpret_cast<void *>(campRows[1]),
                 reinterpret_cast<void *>(campRows[2]),
                 reinterpret_cast<void *>(campRows[3]),
                 reinterpret_cast<void *>(campRows[4]));
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[책략5NDBG] plan 실패 snapshot 읽기 중 예외: owner=%p",
               reinterpret_cast<void *>(owner));
      }
    }

    static bool AdvanceFifthRuntimeStateSeh(uintptr_t dialog) {
      if (!g_id5CountRequested.load() ||
          !g_fiveMetadataRequested.load()) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingOwner);
        return false;
      }

      uintptr_t owner = 0;
      if (dialog && IsValidPtr(dialog, 0x40)) {
        g_trickUiDialog = dialog;
        uintptr_t layout = 0;
        if (SafeReadPtrSeh(dialog + 0x08, &layout) &&
            layout && IsValidPtr(layout, 0x2A8))
          g_trickUiLayout = layout;
        TryGetDialogCampOwnerSeh(&owner);
      }

      if (!owner) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingOwner);
        return false;
      }

      uintptr_t metadataTable = g_fiveMetadataTable;
      if (!metadataTable &&
          !TryResolveFifthMetadataTableFromOwnerSeh(owner, &metadataTable)) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingTable);
        return false;
      }

      if (!IsSpell5HealProbeReady()) {
        if (!SetSpell5HealProbeFromMetadataTable(metadataTable) ||
            !IsSpell5HealProbeReady()) {
          g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingData);
          return false;
        }
      }

      SetStratagemFiveCountTest(true);
      if (!g_id5CountApplied ||
          g_id5CountOwner != owner ||
          g_id5CountEntryIndex >= 5) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingCount);
        return false;
      }

      if (!TryApplyFifthCampForOwnerSeh(owner)) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingCamp);
        return false;
      }

      if (!TryExtendFifthDialogModelCountSeh(dialog)) {
        g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingModel);
        return false;
      }

      g_fifthRuntimeStage.store(FifthRuntimeStage::Ready);
      return true;
    }

    static bool TryGetDialogCampOwnerSeh(uintptr_t *outOwner) {
      if (!outOwner)
        return false;
      *outOwner = 0;

      const uintptr_t dialog = g_trickUiDialog;
      if (!dialog || !IsValidPtr(dialog, 0x40)) {
        AddLog(u8"[책략5STATE] owner fail: reason=dialog-range dialog=%p",
               reinterpret_cast<void *>(dialog));
        return false;
      }

      uintptr_t holder = 0;
      uintptr_t inner = 0;
      uintptr_t campData = 0;
      uint32_t count = 0;
      bool holderRead = false;
      bool holderRange = false;
      bool innerRead = false;
      bool entriesRange = false;
      bool countRange = false;
      bool countRead = false;
      bool countValid = false;
      bool campRead = false;
      bool campRowsRange = false;
      const char *reason = "unknown";
      DWORD exceptionCode = 0;
      bool success = false;

      __try {
        holderRead = SafeReadPtrSeh(dialog + 0x20, &holder);
        if (!holderRead) {
          reason = "holder-read";
        } else if (!holder) {
          reason = "holder-null";
        } else {
          holderRange = IsValidPtr(holder, sizeof(uintptr_t));
          if (!holderRange) {
            reason = "holder-range";
          } else {
            innerRead = SafeReadPtrSeh(holder, &inner);
            if (!innerRead) {
              reason = "inner-read";
            } else if (!inner) {
              reason = "inner-null";
            } else {
              // Dialog::Initialize is already about to consume Tricker entries.
              // Do not wait for the older battle-info side/gauge fields to settle;
              // validate only the Camp/Tricker structures required by ID5.
              const uintptr_t modelBase = inner + 0xF0;
              entriesRange =
                  IsValidPtr(modelBase + 0x10,
                             5 * sizeof(StratagemFiveModel::Entry));
              countRange = IsValidPtr(modelBase + 0x60, sizeof(count));
              if (!entriesRange) {
                reason = "model-entry-range";
              } else if (!countRange) {
                reason = "model-count-range";
              } else {
                countRead = SafeCopySeh(modelBase + 0x60,
                                        &count, sizeof(count));
                if (!countRead) {
                  reason = "model-count-read";
                } else {
                  countValid = count >= 1 && count <= 5;
                  if (!countValid) {
                    reason = "model-count-value";
                  } else {
                    campRead = SafeReadPtrSeh(inner + 0x10, &campData);
                    if (!campRead) {
                      reason = "campdata-read";
                    } else if (!campData) {
                      reason = "campdata-null";
                    } else {
                      campRowsRange =
                          IsValidPtr(campData + 0x68,
                                     5 * sizeof(uintptr_t));
                      if (!campRowsRange) {
                        reason = "camp-rows-range";
                      } else {
                        *outOwner = inner;
                        reason = "ok";
                        success = true;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
        reason = "exception";
      }

      if (!success) {
        AddLog(
            u8"[책략5STATE] owner fail: reason=%s dialog=%p holderRead=%d holder=%p holderRange=%d innerRead=%d inner=%p entriesRange=%d countRange=%d countRead=%d count=%u countValid=%d campRead=%d campData=%p campRowsRange=%d exception=%08X",
            reason,
            reinterpret_cast<void *>(dialog),
            holderRead ? 1 : 0,
            reinterpret_cast<void *>(holder),
            holderRange ? 1 : 0,
            innerRead ? 1 : 0,
            reinterpret_cast<void *>(inner),
            entriesRange ? 1 : 0,
            countRange ? 1 : 0,
            countRead ? 1 : 0,
            (unsigned)count,
            countValid ? 1 : 0,
            campRead ? 1 : 0,
            reinterpret_cast<void *>(campData),
            campRowsRange ? 1 : 0,
            static_cast<unsigned>(exceptionCode));
      }
      return success;
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
    const bool layoutPostReady = EnsureFifthUiLayoutPostButtonsHook();
    const bool resetCompactReady = EnsureFifthUiResetCompactHook();
    const bool onSelectReady = EnsureFifthUiOnTrickSelectBoundHook();
    const bool onSelectRuntimeReady = EnsureFifthUiOnTrickSelectRuntimeHook();
    LogFifthUiSignalCallsites();
    LogFifthUiCallbackTargets();
    LogFifthUiCallbackCodeTargets();
    const bool preCallbackReady = EnsureFifthUiPreCallbackHook();
    const bool callbackLoopReady = EnsureFifthUiCallbackLoopHook();
    const bool ready = initReady && layoutPostReady && resetCompactReady &&
                       onSelectReady && onSelectRuntimeReady &&
                       preCallbackReady && callbackLoopReady;
    if (!ready && reportFailure) {
      AddLog(u8"[책략5UIHELPER] 조기 UI 훅 준비 실패: init=%d layoutPost=%d reset=%d onSelect=%d onSelectRT=%d preCallback=%d callbackLoop=%d",
             initReady?1:0, layoutPostReady?1:0, resetCompactReady?1:0,
             onSelectReady?1:0, onSelectRuntimeReady?1:0,
             preCallbackReady?1:0, callbackLoopReady?1:0);
    }
    return ready;
  }

  bool SetStratagemFiveCountTest(bool enable) {
    if (!enable) {
      g_id5CountRequested = false;

      if (!g_id5CountApplied)
        return true;

      const bool ownerStillCurrent =
          (g_id5CountOwner == g_attackInfo &&
           ValidateInfo(g_id5CountOwner, 0)) ||
          (g_id5CountOwner == g_defenseInfo &&
           ValidateInfo(g_id5CountOwner, 1));

      const uintptr_t expectedAddr =
          (g_id5CountEntryIndex < 5 && g_id5CountOwner)
              ? g_id5CountOwner + 0xF0 + 0x10 +
                    static_cast<uintptr_t>(g_id5CountEntryIndex) * 0x10 +
                    0x0C
              : 0;

      if (ownerStillCurrent &&
          expectedAddr &&
          g_id5CountAddr == expectedAddr &&
          IsValidPtr(g_id5CountAddr, 1)) {
        DWORD oldProtect = 0;
        DWORD tmpProtect = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(g_id5CountAddr), 1,
                           PAGE_READWRITE, &oldProtect)) {
          *reinterpret_cast<uint8_t *>(g_id5CountAddr) =
              g_id5CountOriginal;
          VirtualProtect(reinterpret_cast<LPVOID>(g_id5CountAddr), 1,
                         oldProtect, &tmpProtect);
        }
        AddLog(u8"[책략5슬롯DBG] ID5 가용횟수 원복: entry=%u addr=%p value=%u",
               (unsigned)g_id5CountEntryIndex,
               reinterpret_cast<void *>(g_id5CountAddr),
               (unsigned)g_id5CountOriginal);
      } else {
        AddLog(u8"[책략5수명] 이전 전투 ID5 횟수 포인터는 현재 세대가 아니므로 쓰지 않고 폐기.");
      }

      g_id5CountApplied = false;
      g_id5CountAddr = 0;
      g_id5CountOwner = 0;
      g_id5CountOriginal = 0;
      g_id5CountEntryIndex = UINT32_MAX;
      return true;
    }

    g_id5CountRequested = true;

    if (!EnsureCaptureHook()) {
      AddLog(u8"[책략5수명] ID5 횟수 ON 요청 저장. Camp 캡처 훅 준비 후 자동 적용 대기.");
      return true;
    }

    if (!g_fiveMetadataTable) {
      AddLog(u8"[책략5수명] ID5 횟수 ON 요청 저장. native TrickData table 준비 후 자동 적용 대기.");
      return true;
    }

    if (!ValidateInfo(g_attackInfo, 0) &&
        !ValidateInfo(g_defenseInfo, 1))
      TryRecaptureBattleInfoFromDialogSeh();

    if (g_id5CountApplied) {
      const bool ownerStillCurrent =
          (g_id5CountOwner == g_attackInfo &&
           ValidateInfo(g_id5CountOwner, 0)) ||
          (g_id5CountOwner == g_defenseInfo &&
           ValidateInfo(g_id5CountOwner, 1));
      const uintptr_t expectedAddr =
          (g_id5CountEntryIndex < 5 && g_id5CountOwner)
              ? g_id5CountOwner + 0xF0 + 0x10 +
                    static_cast<uintptr_t>(g_id5CountEntryIndex) * 0x10 +
                    0x0C
              : 0;

      // Once injected for this battle generation, do not refill it merely
      // because the player spent the one use and available became zero.
      if (ownerStillCurrent &&
          expectedAddr &&
          g_id5CountAddr == expectedAddr &&
          IsValidPtr(expectedAddr, 1))
        return true;

      AddLog(u8"[책략5수명] 새 전투/로드 세대 감지: 이전 ID5 횟수 적용 상태 폐기.");
      g_id5CountApplied = false;
      g_id5CountAddr = 0;
      g_id5CountOwner = 0;
      g_id5CountOriginal = 0;
      g_id5CountEntryIndex = UINT32_MAX;
    }

    uintptr_t chosen = 0;
    const char *chosenName = nullptr;
    StratagemFiveModel::Plan chosenPlan{};

    uintptr_t dialogOwner = 0;
    if (TryGetDialogCampOwnerSeh(&dialogOwner)) {
      StratagemFiveModel::Plan plan{};
      if (TryReadFifthPlanForOwnerSeh(dialogOwner, &plan)) {
        chosen = dialogOwner;
        chosenName = u8"현재 dialog측";
        chosenPlan = plan;
      } else {
        LogFifthPlanSnapshotSeh(dialogOwner, "count-dialog-owner");
      }
    }

    if (!chosen) {
      struct Candidate {
        uintptr_t ptr;
        uint8_t side;
        const char *name;
      };
      const Candidate candidates[] = {
          {g_attackInfo, 0, u8"공격측"},
          {g_defenseInfo, 1, u8"수비측"},
      };

      for (const auto &candidate : candidates) {
        if (!ValidateInfo(candidate.ptr, candidate.side))
          continue;

        StratagemFiveModel::Plan plan{};
        if (!TryReadFifthPlanForOwnerSeh(candidate.ptr, &plan))
          continue;

        if (chosen && chosen != candidate.ptr) {
          AddLog(u8"[책략5수명] ID5 횟수 ON 요청 유지. 공격/수비 모두 유효하여 dialog측 확정 대기.");
          return true;
        }

        chosen = candidate.ptr;
        chosenName = candidate.name;
        chosenPlan = plan;
      }
    }

    if (!chosen || chosenPlan.originalCount > 4) {
      AddLog(u8"[책략5수명] ID5 횟수 ON 요청 저장. 현재 전투 원본 책략 목록 N을 아직 확정하지 못함.");
      return true;
    }

    const uint32_t entryIndex = chosenPlan.originalCount;
    const uintptr_t availableAddr =
        chosen + 0xF0 + 0x10 +
        static_cast<uintptr_t>(entryIndex) * 0x10 + 0x0C;
    if (!IsValidPtr(availableAddr, 1)) {
      AddLog(u8"[책략5수명] ID5 횟수 ON 요청 유지. entry%u available 주소 미준비.",
             (unsigned)entryIndex);
      return true;
    }

    const uint8_t original =
        *reinterpret_cast<const uint8_t *>(availableAddr);

    // If ID5 is already the last model entry, preserve a consumed zero rather
    // than turning this into an unlimited-use refresh loop.
    if (!chosenPlan.alreadyPresent || original != 0) {
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(availableAddr), 1,
                          PAGE_READWRITE, &oldProtect)) {
        AddLog(u8"[책략5수명] ID5 횟수 ON 요청 유지. entry%u 쓰기 권한 획득 실패.",
               (unsigned)entryIndex);
        return true;
      }

      *reinterpret_cast<uint8_t *>(availableAddr) = 1;
      VirtualProtect(reinterpret_cast<LPVOID>(availableAddr), 1,
                     oldProtect, &tmpProtect);

      if (*reinterpret_cast<const uint8_t *>(availableAddr) != 1) {
        AddLog(u8"[책략5수명] ID5 횟수 ON 요청 유지. entry%u available=1 검증 실패.",
               (unsigned)entryIndex);
        return true;
      }
    }

    g_id5CountAddr = availableAddr;
    g_id5CountOwner = chosen;
    g_id5CountOriginal = original;
    g_id5CountEntryIndex = entryIndex;
    g_id5CountApplied = true;

    AddLog(u8"[책략5슬롯DBG] %s 원본 책략 N=%u / ID5 entry=%u available %u -> %u: %p",
           chosenName ? chosenName : u8"현재측",
           (unsigned)chosenPlan.originalCount,
           (unsigned)entryIndex,
           (unsigned)original,
           (unsigned)*reinterpret_cast<const uint8_t *>(availableAddr),
           reinterpret_cast<void *>(availableAddr));
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

  static void RestoreFifthUiModelCountSeh() {
    if(!g_fifthUiModelCountApplied && !g_fifthUiModelEntryApplied)
      return;

    const uintptr_t countAddr=g_fifthUiModelCountAddr;
    const uint32_t countOriginal=g_fifthUiModelCountOriginal;
    const uintptr_t entryAddr=g_fifthUiModelEntryAddr;
    const uint64_t entryOriginal0=g_fifthUiModelEntryOriginal[0];
    const uint64_t entryOriginal1=g_fifthUiModelEntryOriginal[1];

    __try {
      // Shrink the visible range first, then restore the synthetic entry.
      if(g_fifthUiModelCountApplied &&
         countAddr && IsValidPtr(countAddr,sizeof(uint32_t)) &&
         *reinterpret_cast<const uint32_t *>(countAddr)==countOriginal+1) {
        *reinterpret_cast<uint32_t *>(countAddr)=countOriginal;
      }

      if(g_fifthUiModelEntryApplied &&
         entryAddr && IsValidPtr(entryAddr,2*sizeof(uint64_t))) {
        auto *q=reinterpret_cast<uint64_t *>(entryAddr);
        q[0]=entryOriginal0;
        q[1]=entryOriginal1;
      }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }

    g_fifthUiModelCountAddr=0;
    g_fifthUiModelCountOriginal=0;
    g_fifthUiModelCountApplied=false;
    g_fifthUiModelEntryAddr=0;
    g_fifthUiModelEntryOriginal[0]=0;
    g_fifthUiModelEntryOriginal[1]=0;
    g_fifthUiModelEntryApplied=false;
    g_fifthRuntimeOriginalCount=0;
    g_fifthUiActiveLayout.store(0);
    g_fifthUiActiveButton.store(0);
    SetFifthUiSidecarVisibleSeh(false);
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
      g_fiveRuntimeOwner = 0;
    };

    if (!enable) {
      g_fiveMetadataRequested = false;

      // Runtime model/Camp 연결은 원복하되, live layout에 이미 생성/등록된
      // sidecar와 ID7 bookkeeping은 절대 버리지 않는다. helper7은 최초
      // Initialize에서 한 번 소모되므로 여기서 maker를 7칸으로 되돌리거나
      // sidecar 전역 포인터를 지우면 같은 layout에서 다시 복구할 수 없다.
      RestoreFifthUiModelCountSeh();
      restoreRuntimeSlot();

      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;

      // UI bridge는 현재 layout 수명 동안 유지하고 표시만 잠시 끈다.
      // 재활성화 시 동일 sidecar를 그대로 재사용한다.
      SetFifthUiSidecarVisibleSeh(false);

      AddLog(u8"[책략5메타DBG] 5번 내부 등록 해제. live UI sidecar/ID7 등록은 재사용을 위해 유지.");
      return true;
    }

    // Checkbox ON means persistent intent. Battle-local Camp/UI wiring may
    // not exist yet, so store the request before trying the current generation.
    g_fiveMetadataRequested = true;

    // The DLL worker normally installed this already. This is only a guarded
    // fallback/status report, never an attempt to reinitialize a live maker.
    if (!PrepareStratagemFiveUiBridge()) {
      AddLog(u8"[책략5수명] ID5 내부등록 ON 요청 저장. UI bridge 준비 후 자동 적용 대기.");
      return true;
    }
    LogTrickUiBridgeStatus("metadata-enable");

    if (g_fiveMetadataApplied && g_fiveRuntimeSlotApplied) {
      const bool sameGeneration =
          g_fiveRuntimeOwner &&
          g_fiveRuntimeOwner == g_id5CountOwner &&
          g_fiveRuntimeSlotAddr &&
          g_fiveMetadataAddr &&
          IsValidPtr(g_fiveRuntimeSlotAddr, sizeof(uintptr_t)) &&
          *reinterpret_cast<const uintptr_t *>(g_fiveRuntimeSlotAddr) ==
              g_fiveMetadataAddr;
      if (sameGeneration) {
        g_fiveMetadataRequested = true;
        return true;
      }

      AddLog(u8"[책략5수명] 새 전투/로드 세대 감지: 이전 내부등록 포인터를 쓰지 않고 폐기.");
      g_fiveMetadataApplied = false;
      g_fiveMetadataAddr = 0;
      g_fiveMetadataTable = 0;
      g_fiveRuntimeSlotApplied = false;
      g_fiveRuntimeSlotAddr = 0;
      g_fiveRuntimeSlotOriginal = 0;
      g_fiveRuntimeOwner = 0;
    }

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

    const uintptr_t row5 = table + 4 * kRecordStride;

    g_fiveMetadataTable = table;
    g_fiveMetadataAddr = row5;
    g_fiveMetadataApplied = true;

    AddLog(u8"[책략5메타DBG] native TrickData 확인: table=%p row5=%p",
           reinterpret_cast<void *>(table),
           reinterpret_cast<void *>(row5));

    if (!g_id5CountApplied || !g_id5CountOwner) {
      // Keep the canonical table resolved. The count checkbox can now derive
      // the current native N from Camp::Impl+0xF0 instead of assuming N==4.
      AddLog(u8"[책략5수명] ID5 내부등록 ON 요청 저장. native table 준비 완료, 전장 owner/횟수 적용 대기.");
      return true;
    }

    StratagemFiveModel::Plan plan{};
    StratagemFiveModel::Entry entries[5] = {};
    uintptr_t campRows[5] = {};
    uintptr_t modelBase = 0;
    uintptr_t tricksAddr = 0;
    if (!TryReadFifthPlanForOwnerSeh(
            g_id5CountOwner, &plan, &entries, &campRows,
            &modelBase, &tricksAddr)) {
      LogFifthPlanSnapshotSeh(g_id5CountOwner, "metadata-owner");
      AddLog(u8"[책략5수명] ID5 내부등록 대기: 현재 owner의 원본 책략 목록 N/row 매핑 검증 실패.");
      return true;
    }

    if (plan.originalCount > 4 || plan.campSlot >= 5) {
      AddLog(u8"[책략5PDBDBG] 가변 책략 계획 범위 오류: N=%u campSlot=%u",
             (unsigned)plan.originalCount, (unsigned)plan.campSlot);
      return false;
    }

    const uintptr_t matchedSlot =
        tricksAddr + static_cast<uintptr_t>(plan.campSlot) *
                         sizeof(uintptr_t);
    const uintptr_t matchedOld =
        *reinterpret_cast<const uintptr_t *>(matchedSlot);

    if (matchedOld != 0 && matchedOld != row5) {
      AddLog(u8"[책략5PDBDBG] ID5 연결 대상 CampData slot%u가 비어있지 않음: %p",
             (unsigned)plan.campSlot,
             reinterpret_cast<void *>(matchedOld));
      return false;
    }

    if (matchedOld != row5) {
      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(matchedSlot),
                          sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
        AddLog(u8"[책략5PDBDBG] CampData slot%u 쓰기 권한 변경 실패.",
               (unsigned)plan.campSlot);
        return false;
      }

      *reinterpret_cast<uintptr_t *>(matchedSlot) = row5;
      VirtualProtect(reinterpret_cast<LPVOID>(matchedSlot),
                     sizeof(uintptr_t), oldProtect, &tmpProtect);

      if (*reinterpret_cast<const uintptr_t *>(matchedSlot) != row5) {
        AddLog(u8"[책략5PDBDBG] CampData slot%u -> row5 쓰기 검증 실패.",
               (unsigned)plan.campSlot);
        return false;
      }
    }

    g_fiveRuntimeSlotAddr = matchedSlot;
    g_fiveRuntimeSlotOriginal = matchedOld;
    g_fiveRuntimeSlotApplied = true;
    g_fiveRuntimeOwner = g_id5CountOwner;
    g_fiveMetadataRequested = true;
    g_fifthRuntimeOriginalCount = plan.originalCount;

    AddLog(u8"[책략5PDBDBG] 가변 ID5 연결 성공: N=%u modelBase=%p campSlot=%u old=%p new(row5)=%p",
           (unsigned)plan.originalCount,
           reinterpret_cast<void *>(modelBase),
           (unsigned)plan.campSlot,
           reinterpret_cast<void *>(matchedOld),
           reinterpret_cast<void *>(row5));

    if (!EnsureTrickUiLayoutCaptureHook())
      AddLog(u8"[책략5UICAP] layout 캡처 훅은 설치되지 않았습니다.");
    if (!EnsureTrickUiDialogCaptureHook())
      AddLog(u8"[책략5UICAP] dialog 캡처 훅은 설치되지 않았습니다.");

    const uintptr_t liveDialog =
        static_cast<uintptr_t>(g_trickUiDialog);
    if (liveDialog && IsValidPtr(liveDialog, 0x40)) {
      if (TryExtendFifthDialogModelCountSeh(liveDialog)) {
        AddLog(u8"[책략5수명] 내부등록 직후 현재 dialog에 N+1 model/UI 즉시 적용.");
      }
    }

    return true;
  }


  static void PublishFifthUiForOriginalCountSeh(
      uintptr_t dialog, uint32_t originalCount) {
    const uintptr_t layout = g_trickUiLayout;

    g_fifthRuntimeOriginalCount = originalCount;

    // Native m_pButtons[4] is sufficient while N+1 <= 4. Only N==4 needs
    // the external fifth physical button.
    if (originalCount < 4) {
      g_fifthUiActiveLayout.store(0);
      g_fifthUiActiveButton.store(0);
      SetFifthUiSidecarVisibleSeh(false);
      return;
    }

    if (originalCount != 4 ||
        !dialog || !layout ||
        !ValidateFifthUiRegistrySeh(layout)) {
      g_fifthUiActiveLayout.store(0);
      g_fifthUiActiveButton.store(0);
      SetFifthUiSidecarVisibleSeh(false);
      return;
    }

    uintptr_t dialogLayout = 0;
    if (!SafeReadPtrSeh(dialog + 0x08, &dialogLayout) ||
        dialogLayout != layout)
      return;

    g_fifthUiActiveLayout.store(layout);
    g_fifthUiActiveButton.store(g_fifthUiSidecarButton);
    SetFifthUiSidecarVisibleSeh(true);
    CompactFifthUiButtonsAfterResetSeh(layout);
  }

  static bool TryExtendFifthDialogModelCountSeh(uintptr_t dialog) {
    if (!dialog || !g_fiveRuntimeSlotApplied ||
        !g_fiveMetadataAddr || !g_fiveMetadataTable)
      return false;

    __try {
      uintptr_t holder = 0;
      uintptr_t inner = 0;
      if (!SafeReadPtrSeh(dialog + 0x20, &holder) ||
          !holder || !IsValidPtr(holder, sizeof(uintptr_t)) ||
          !SafeReadPtrSeh(holder, &inner) ||
          !inner || inner != g_fiveRuntimeOwner) {
        AddLog(u8"[책략5UIMODEL] 현재 dialog owner 확인 실패: dialog=%p holder=%p inner=%p expected=%p",
               reinterpret_cast<void *>(dialog),
               reinterpret_cast<void *>(holder),
               reinterpret_cast<void *>(inner),
               reinterpret_cast<void *>(g_fiveRuntimeOwner));
        return false;
      }

      StratagemFiveModel::Plan plan{};
      StratagemFiveModel::Entry entries[5] = {};
      uintptr_t campRows[5] = {};
      uintptr_t modelBase = 0;
      uintptr_t tricksAddr = 0;
      if (!TryReadFifthPlanForOwnerSeh(
              inner, &plan, &entries, &campRows,
              &modelBase, &tricksAddr)) {
        AddLog(u8"[책략5UIMODEL] 가변 N model/Camp 계획 검증 실패: owner=%p",
               reinterpret_cast<void *>(inner));
        return false;
      }

      if (plan.originalCount < 1 || plan.originalCount > 4) {
        AddLog(u8"[책략5UIMODEL] 원본 책략 N 범위 오류: %u",
               (unsigned)plan.originalCount);
        return false;
      }

      const uintptr_t entryAddr =
          modelBase + 0x10 +
          static_cast<uintptr_t>(plan.originalCount) *
              sizeof(StratagemFiveModel::Entry);
      const uintptr_t countAddr = modelBase + 0x60;

      if (plan.alreadyPresent) {
        g_fifthRuntimeOriginalCount = plan.originalCount;
        PublishFifthUiForOriginalCountSeh(dialog, plan.originalCount);
        AddLog(u8"[책략5UIMODEL] ID5가 이미 마지막 entry에 존재: N=%u total=%u entry=%u",
               (unsigned)plan.originalCount,
               (unsigned)(plan.originalCount + 1),
               (unsigned)plan.originalCount);
        return true;
      }

      if (!IsValidPtr(entryAddr, sizeof(StratagemFiveModel::Entry)) ||
          !IsValidPtr(countAddr, sizeof(uint32_t))) {
        AddLog(u8"[책략5UIMODEL] 가변 entry/count 쓰기 대상 범위 무효: N=%u",
               (unsigned)plan.originalCount);
        return false;
      }

      g_fifthUiModelEntryAddr = entryAddr;
      std::memcpy(g_fifthUiModelEntryOriginal,
                  &plan.before,
                  sizeof(plan.before));
      g_fifthUiModelEntryApplied = true;
      g_fifthUiModelCountAddr = countAddr;
      g_fifthUiModelCountOriginal = plan.originalCount;
      g_fifthUiModelCountApplied = true;

      auto *entry =
          reinterpret_cast<StratagemFiveModel::Entry *>(entryAddr);
      *entry = plan.added;
      *reinterpret_cast<uint32_t *>(countAddr) =
          plan.originalCount + 1;

      StratagemFiveModel::Entry verify{};
      uint32_t verifyCount = 0;
      if (!SafeCopySeh(entryAddr, &verify, sizeof(verify)) ||
          !SafeCopySeh(countAddr, &verifyCount, sizeof(verifyCount)) ||
          verify.data != plan.added.data ||
          verify.index != plan.added.index ||
          verify.available != plan.added.available ||
          verifyCount != plan.originalCount + 1) {
        *reinterpret_cast<uint32_t *>(countAddr) =
            plan.originalCount;
        std::memcpy(reinterpret_cast<void *>(entryAddr),
                    &plan.before, sizeof(plan.before));

        g_fifthUiModelCountAddr = 0;
        g_fifthUiModelCountOriginal = 0;
        g_fifthUiModelCountApplied = false;
        g_fifthUiModelEntryAddr = 0;
        g_fifthUiModelEntryOriginal[0] = 0;
        g_fifthUiModelEntryOriginal[1] = 0;
        g_fifthUiModelEntryApplied = false;

        AddLog(u8"[책략5UIMODEL] 가변 ID5 entry/count 검증 실패. 즉시 원복.");
        return false;
      }

      g_fifthRuntimeOriginalCount = plan.originalCount;
      PublishFifthUiForOriginalCountSeh(dialog, plan.originalCount);

      AddLog(u8"[책략5UIMODEL] 가변 ID5 합성 성공: N=%u -> total=%u / entry%u row5=%p index=%u available=1 / campSlot=%u",
             (unsigned)plan.originalCount,
             (unsigned)(plan.originalCount + 1),
             (unsigned)plan.originalCount,
             reinterpret_cast<void *>(plan.added.data),
             (unsigned)plan.added.index,
             (unsigned)plan.campSlot);
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      AddLog(u8"[책략5UIMODEL] 가변 ID5 entry 적용 중 예외. 쓰기 중단.");
      return false;
    }
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

    AddLog(u8"[책략5UICB] live callback index4Hits=%ld / sidecar=%p id7=%d",
           (long)g_fifthUiCallbackIndex4Hits,
           reinterpret_cast<void *>(g_fifthUiSidecarButton),
           g_fifthUiId7Registered ? 1 : 0);

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

    if(dialog && IsValidPtr(dialog,0x40))
      TryExtendFifthDialogModelCountSeh(dialog);

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


    auto dumpSignalState = [&](const char *label, uintptr_t button) {
      if(!button || !IsValidPtr(button+0x78,0x60))
        return;
      uint64_t q[12]={};
      if(!SafeCopySeh(button+0x78,q,sizeof(q)))
        return;
      AddLog(u8"[책략5UISIGSTATE] %s button=%p +78: %016llX %016llX %016llX %016llX %016llX %016llX",
             label,reinterpret_cast<void *>(button),
             (unsigned long long)q[0],(unsigned long long)q[1],
             (unsigned long long)q[2],(unsigned long long)q[3],
             (unsigned long long)q[4],(unsigned long long)q[5]);
      AddLog(u8"[책략5UISIGSTATE] %s +A8: %016llX %016llX %016llX %016llX %016llX %016llX",
             label,
             (unsigned long long)q[6],(unsigned long long)q[7],
             (unsigned long long)q[8],(unsigned long long)q[9],
             (unsigned long long)q[10],(unsigned long long)q[11]);
    };
    dumpSignalState("btn0",buttons[0]);
    dumpSignalState("btn3",buttons[3]);
    dumpSignalState("sidecar",g_fifthUiSidecarButton);

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

  static void AbandonStratagemFiveBattleRuntimeState() {
    // Never restore through battle/UI addresses here. This helper is used only
    // after a battle generation is ending or a game/save generation changed,
    // where those objects may already be destroyed or reused. Keep the user's
    // requested ON flags and the process-wide hooks; drop only instance state.
    g_attackInfo = 0;
    g_defenseInfo = 0;

    g_id5CountApplied = false;
    g_id5CountAddr = 0;
    g_id5CountOwner = 0;
    g_id5CountOriginal = 0;
    g_id5CountEntryIndex = UINT32_MAX;

    g_fiveMetadataApplied = false;
    g_fiveMetadataAddr = 0;
    g_fiveMetadataTable = 0;
    g_fiveRuntimeSlotApplied = false;
    g_fiveRuntimeSlotAddr = 0;
    g_fiveRuntimeSlotOriginal = 0;
    g_fiveRuntimeOwner = 0;

    g_fifthUiModelCountAddr = 0;
    g_fifthUiModelCountOriginal = 0;
    g_fifthUiModelCountApplied = false;
    g_fifthUiModelEntryAddr = 0;
    g_fifthUiModelEntryOriginal[0] = 0;
    g_fifthUiModelEntryOriginal[1] = 0;
    g_fifthUiModelEntryApplied = false;
    g_fifthRuntimeOriginalCount = 0;
    DiscardFifthAiProbeState();
    g_fifthUiActiveLayout.store(0);
    g_fifthUiActiveButton.store(0);

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

    g_fifthUiMakerExpanded = false;
    g_fifthUiMakerAddr = 0;
    std::memset(g_fifthUiMakerOriginal, 0, sizeof(g_fifthUiMakerOriginal));

    g_fifthUiCompactLogged = false;
    g_fifthUiResetSignalDumped = false;
    g_fifthUiOnSelectRuntimeLogged = 0;
    g_fifthUiOnSelectAnyHits = 0;
    InterlockedExchange(&g_fifthUiCallbackIndex4Hits, 0);
    g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingOwner);
  }

  bool SetStratagemFiveFeature(bool enable) {
    if (enable) {
      // One user action raises every persistent request. Individual setters may
      // still report "not ready yet", but they store the request before that
      // point; readiness is owned by AdvanceFifthRuntimeStateSeh().
      PrepareStratagemFiveUiBridge(false);
      SetSpell5HealProbe(true);
      SetStratagemFiveMetadataTest(true);
      SetStratagemFiveCountTest(true);

      const uintptr_t dialog = g_trickUiDialog;
      if (dialog && IsValidPtr(dialog, 0x40))
        AdvanceFifthRuntimeStateSeh(dialog);

      AddLog(u8"[책략5STATE] 통합 기능 ON: data/count/metadata 요청 동시 유지 / stage=%u",
             (unsigned)g_fifthRuntimeStage.load());
      return true;
    }

    // Unwind in dependency order. The AI probe reuses a native slot, so
    // restore that slot while the battle objects are still known-live.
    const bool aiProbeOk = RestoreFifthAiProbeSeh();
    const bool metadataOk = SetStratagemFiveMetadataTest(false);
    const bool countOk = SetStratagemFiveCountTest(false);
    const bool dataOk = SetSpell5HealProbe(false);

    g_fifthRuntimeStage.store(FifthRuntimeStage::WaitingOwner);
    AddLog(u8"[책략5STATE] 통합 기능 OFF: metadata/count/data 순서로 해제.");
    return aiProbeOk && metadataOk && countOk && dataOk;
  }

  void ResetStratagemFiveBattleRuntime() {
    // Every battle is a fresh ID5 generation. Hide the old physical sidecar
    // while the layout is still potentially reachable, then forget every
    // battle-local object/reference. Process-wide hooks and persistent ON
    // requests remain installed and the next Layout/Dialog generation rebuilds
    // everything from native state.
    SetFifthUiSidecarVisibleSeh(false);
    AbandonStratagemFiveBattleRuntimeState();

    AddLog(u8"[책략5수명] 전투 종료 확정: ID5 battle-local 상태 전체 폐기(sidecar/ID7/maker/Camp/model/count/dialog/layout). 다음 전투에서 새로 적용.");
  }

  void ResetStratagemFiveSessionRuntime(uintptr_t oldP1,
                                         uintptr_t newP1) {
    AddLog(u8"[책략5수명] 게임 세대 변경 감지: p1 %p -> %p. 전투/UI 런타임 상태 초기화.",
           reinterpret_cast<void *>(oldP1),
           reinterpret_cast<void *>(newP1));

    AbandonStratagemFiveBattleRuntimeState();

    // Hooks themselves intentionally stay installed. Their index4 paths now
    // require exact current-layout ownership and therefore safely fall back
    // while the new save is loading.
  }

  void RefreshStratagemFiveBattleRuntime() {
    if (!g_id5CountRequested.load() ||
        !g_fiveMetadataRequested.load())
      return;

    const uintptr_t dialog = g_trickUiDialog;
    if (!dialog || !IsValidPtr(dialog, 0x40))
      return;

    const FifthRuntimeStage before = g_fifthRuntimeStage.load();
    const bool ready = AdvanceFifthRuntimeStateSeh(dialog);
    const FifthRuntimeStage after = g_fifthRuntimeStage.load();

    // Log only transitions; the state machine itself is safe to call from
    // every battle lifecycle event and does not depend on elapsed time.
    if (after != before) {
      AddLog(u8"[책략5STATE] 진행: %u -> %u / dialog=%p",
             (unsigned)before, (unsigned)after,
             reinterpret_cast<void *>(dialog));
    }

    if (ready) {
      // If N==4 and the sidecar registry became available after model
      // synthesis, publishing again is idempotent and activates index4.
      if (g_fifthRuntimeOriginalCount == 4 &&
          g_trickUiLayout &&
          ValidateFifthUiRegistrySeh(g_trickUiLayout))
        TryExtendFifthDialogModelCountSeh(dialog);

      // Experimental capability probe only. The AI list length and its native
      // available count stay unchanged; one existing slot is represented by
      // row5 so we can see whether the original AI selector can consume ID5.
      if (!g_fifthAiProbeApplied)
        TryArmFifthAiProbeSeh();
      UpdateFifthAiProbeSeh();
    }
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
