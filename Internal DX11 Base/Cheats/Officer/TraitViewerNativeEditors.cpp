#include "pch.h"
#include "TraitViewerNativeEditors.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace DX11Base {
namespace {

enum class PatchKind { Bytes, JumpCave, JumpModule };
struct Reloc { size_t offset; uintptr_t targetOffset; };
struct AssertSpec { uintptr_t offset; const char *hex; };
struct PatchSpec { uintptr_t offset; PatchKind kind; const char *hex; size_t size; uintptr_t targetOffset; };
struct BlockSpec {
  const char *name; uintptr_t anchorOffset; size_t allocationSize; const char *caveB64;
  const Reloc *relocs; size_t relocCount; const AssertSpec *enableAsserts; size_t enableAssertCount;
  const AssertSpec *disableAsserts; size_t disableAssertCount; const PatchSpec *enablePatches; size_t enablePatchCount;
  const PatchSpec *disablePatches; size_t disablePatchCount;
};
struct Runtime { bool applied=false; uintptr_t base=0; uintptr_t cave=0; };

template <typename T,size_t N> constexpr size_t CountOf(const T (&)[N]) { return N; }

int HexNibble(char c) {
  if(c>='0'&&c<='9') return c-'0';
  if(c>='A'&&c<='F') return c-'A'+10;
  if(c>='a'&&c<='f') return c-'a'+10;
  return -1;
}
bool DecodeHex(const char *hex,std::vector<uint8_t> &out) {
  if(!hex) return false;
  const size_t n=std::strlen(hex);
  if(n%2) return false;
  out.resize(n/2);
  for(size_t i=0;i<n;i+=2){
    const int a=HexNibble(hex[i]),b=HexNibble(hex[i+1]);
    if(a<0||b<0) return false;
    out[i/2]=static_cast<uint8_t>((a<<4)|b);
  }
  return true;
}
int B64Value(char c) {
  if(c>='A'&&c<='Z')return c-'A';
  if(c>='a'&&c<='z')return c-'a'+26;
  if(c>='0'&&c<='9')return c-'0'+52;
  if(c=='+')return 62;
  if(c=='/')return 63;
  return -1;
}
bool DecodeBase64(const char *s,std::vector<uint8_t>&out) {
  out.clear();
  if(!s) return false;
  int q[4]{};
  int qn=0;
  for(const char *p=s;*p;++p){
    if(*p=='=') break;
    const int d=B64Value(*p);
    if(d<0) continue;
    q[qn++]=d;
    if(qn==4){
      out.push_back(static_cast<uint8_t>((q[0]<<2)|(q[1]>>4)));
      out.push_back(static_cast<uint8_t>((q[1]<<4)|(q[2]>>2)));
      out.push_back(static_cast<uint8_t>((q[2]<<6)|q[3]));
      qn=0;
    }
  }
  if(qn==2) out.push_back(static_cast<uint8_t>((q[0]<<2)|(q[1]>>4)));
  else if(qn==3){
    out.push_back(static_cast<uint8_t>((q[0]<<2)|(q[1]>>4)));
    out.push_back(static_cast<uint8_t>((q[1]<<4)|(q[2]>>2)));
  }
  return qn!=1;
}
bool MatchHex(uintptr_t address,const char *hex) {
  std::vector<uint8_t>b;
  if(!DecodeHex(hex,b))return false;
  return std::memcmp(reinterpret_cast<const void*>(address),b.data(),b.size())==0;
}
bool WriteMemory(uintptr_t address,const void*data,size_t size) {
  DWORD old=0;
  if(!VirtualProtect(reinterpret_cast<void*>(address),size,PAGE_EXECUTE_READWRITE,&old))return false;
  std::memcpy(reinterpret_cast<void*>(address),data,size);
  FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size);
  DWORD ignored=0;
  VirtualProtect(reinterpret_cast<void*>(address),size,old,&ignored);
  return true;
}
bool PutRel32(std::vector<uint8_t>&code,size_t off,uintptr_t codeAddress,uintptr_t target) {
  if(off+4>code.size())return false;
  const uintptr_t next=codeAddress+off+4;
  const int64_t d=static_cast<int64_t>(target)-static_cast<int64_t>(next);
  if(d<(std::numeric_limits<int32_t>::min)()||d>(std::numeric_limits<int32_t>::max)())return false;
  const int32_t r=static_cast<int32_t>(d);
  std::memcpy(code.data()+off,&r,4);
  return true;
}
bool Verify(uintptr_t base,const AssertSpec*specs,size_t count,const char*name,const char*phase) {
  for(size_t i=0;i<count;++i){
    if(!MatchHex(base+specs[i].offset,specs[i].hex)){
      AddLog(u8"[기재 화면/%s] %s assert 실패: +0x%llX",name,phase,static_cast<unsigned long long>(specs[i].offset));
      return false;
    }
  }
  return true;
}
bool ApplyPatch(uintptr_t base,uintptr_t cave,const PatchSpec&p) {
  if(p.kind==PatchKind::Bytes){
    std::vector<uint8_t>b;
    if(!DecodeHex(p.hex,b))return false;
    return WriteMemory(base+p.offset,b.data(),b.size());
  }
  if(p.size<5)return false;
  const uintptr_t target=p.kind==PatchKind::JumpCave?cave+p.targetOffset:base+p.targetOffset;
  const int64_t d=static_cast<int64_t>(target)-static_cast<int64_t>(base+p.offset+5);
  if(d<(std::numeric_limits<int32_t>::min)()||d>(std::numeric_limits<int32_t>::max)())return false;
  std::vector<uint8_t>b(p.size,0x90);
  b[0]=0xE9;
  const int32_t r=static_cast<int32_t>(d);
  std::memcpy(b.data()+1,&r,4);
  return WriteMemory(base+p.offset,b.data(),b.size());
}
bool Install(Runtime&rt,const BlockSpec&s) {
  if(rt.applied)return true;
  auto m=GetModuleHandleW(L"SAN8RPK.exe");
  if(!m)return false;
  const uintptr_t base=reinterpret_cast<uintptr_t>(m);
  if(!Verify(base,s.enableAsserts,s.enableAssertCount,s.name,"ENABLE"))return false;
  const uintptr_t cave=AllocNear(base+s.anchorOffset,s.allocationSize);
  if(!cave)return false;
  std::vector<uint8_t>code;
  if(!DecodeBase64(s.caveB64,code)||code.empty()||code.size()>s.allocationSize){
    VirtualFree(reinterpret_cast<void*>(cave),0,MEM_RELEASE);
    return false;
  }
  for(size_t i=0;i<s.relocCount;++i){
    if(!PutRel32(code,s.relocs[i].offset,cave,base+s.relocs[i].targetOffset)){
      VirtualFree(reinterpret_cast<void*>(cave),0,MEM_RELEASE);
      return false;
    }
  }
  if(!WriteMemory(cave,code.data(),code.size())){
    VirtualFree(reinterpret_cast<void*>(cave),0,MEM_RELEASE);
    return false;
  }
  for(size_t i=0;i<s.enablePatchCount;++i){
    if(!ApplyPatch(base,cave,s.enablePatches[i])){
      for(size_t j=0;j<s.disablePatchCount;++j)ApplyPatch(base,cave,s.disablePatches[j]);
      VirtualFree(reinterpret_cast<void*>(cave),0,MEM_RELEASE);
      return false;
    }
  }
  rt={true,base,cave};
  AddLog(u8"[기재 화면/%s] 적용",s.name);
  return true;
}
bool Remove(Runtime&rt,const BlockSpec&s) {
  if(!rt.applied)return true;
  if(!Verify(rt.base,s.disableAsserts,s.disableAssertCount,s.name,"DISABLE"))return false;
  for(size_t i=0;i<s.disablePatchCount;++i)
    if(!ApplyPatch(rt.base,rt.cave,s.disablePatches[i]))return false;
  VirtualFree(reinterpret_cast<void*>(rt.cave),0,MEM_RELEASE);
  rt={};
  AddLog(u8"[기재 화면/%s] 해제",s.name);
  return true;
}

#include "TraitViewerKiraseData.inc"
#include "TraitViewerInProgressData.inc"
#include "TraitViewerBaseEditorData.inc"

// 진단 전용: 편집 객체에 기재3 원본(+0x238)을 백업하는 확실한 지점에서
// editor(RDI) 주소만 기록하고 원본 명령을 그대로 재실행합니다.
static constexpr char kDirtyDiagCaveB64[] =
  "SIk9OQAAAEiJjzgCAADpAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
static constexpr Reloc kDirtyDiagRelocs[] = {
  {0x0F,0x12BA1B0},
};
static constexpr AssertSpec kDirtyDiagEnableAsserts[] = {
  {0x12BA1A9,"48898F38020000"},
};
static constexpr PatchSpec kDirtyDiagEnablePatches[] = {
  {0x12BA1A9,PatchKind::JumpCave,nullptr,7,0x00},
};
static constexpr PatchSpec kDirtyDiagDisablePatches[] = {
  {0x12BA1A9,PatchKind::Bytes,"48898F38020000",0,0},
};
static constexpr BlockSpec kDirtyDiagSpec = {
  "trait dirty diag",0x12BA1A9,0x1000,kDirtyDiagCaveB64,
  kDirtyDiagRelocs,CountOf(kDirtyDiagRelocs),
  kDirtyDiagEnableAsserts,CountOf(kDirtyDiagEnableAsserts),
  nullptr,0,
  kDirtyDiagEnablePatches,CountOf(kDirtyDiagEnablePatches),
  kDirtyDiagDisablePatches,CountOf(kDirtyDiagDisablePatches)
};

static Runtime g_kirase,g_inProgress,g_baseEditor,g_dirtyDiagRuntime;

static bool g_dirtyDiagEnabled = false;
static bool g_dirtyDiagBaselineReady = false;
static uintptr_t g_dirtyDiagTraceOfficer = 0;

struct TraitReadTraceEvent {
  uintptr_t rip = 0;
  uintptr_t address = 0;
};

constexpr LONG kTraitReadTraceCapacity = 1024;
static TraitReadTraceEvent g_traitReadTraceEvents[kTraitReadTraceCapacity] = {};
static volatile LONG g_traitReadTraceWriteIndex = 0;
static PVOID g_traitReadTraceVeh = nullptr;
static uintptr_t g_traitReadTracePage = 0;
static SIZE_T g_traitReadTracePageSize = 0;
static DWORD g_traitReadTraceBaseProtect = 0;
static bool g_traitReadTraceArmed = false;
static thread_local uintptr_t t_traitReadTraceRearmPage = 0;

bool SafeReadPtrDiag(uintptr_t address, uintptr_t &out) {
  __try {
    out = *reinterpret_cast<uintptr_t *>(address);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = 0;
    return false;
  }
}

bool SafeCopyDiag(void *dst, const void *src, size_t size) {
  __try {
    std::memcpy(dst, src, size);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

uint16_t TraitIdFromPtrDiag(uintptr_t p) {
  if (p < 0x10000)
    return 0;
  __try {
    return *reinterpret_cast<uint16_t *>(p + 0x08);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

uintptr_t GetDirtyDiagEditor() {
  if (!g_dirtyDiagRuntime.applied || g_dirtyDiagRuntime.cave < 0x10000)
    return 0;
  uintptr_t editor = 0;
  SafeReadPtrDiag(g_dirtyDiagRuntime.cave + 0x40, editor);
  return editor;
}

void ReadOfficerTraitIdsDiag(uintptr_t officer, uint16_t ids[3]) {
  ids[0] = ids[1] = ids[2] = 0;
  if (officer < 0x10000)
    return;
  for (int i = 0; i < 3; ++i) {
    uintptr_t p = 0;
    if (SafeReadPtrDiag(officer + 0x88 + static_cast<uintptr_t>(i) * 8, p))
      ids[i] = TraitIdFromPtrDiag(p);
  }
}

LONG CALLBACK TraitReadTraceVeh(PEXCEPTION_POINTERS ep) {
  if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
    return EXCEPTION_CONTINUE_SEARCH;

  const DWORD code = ep->ExceptionRecord->ExceptionCode;

  if (code == STATUS_GUARD_PAGE_VIOLATION) {
    if (!g_traitReadTraceArmed || !g_traitReadTracePage)
      return EXCEPTION_CONTINUE_SEARCH;

    const ULONG_PTR accessType =
        ep->ExceptionRecord->NumberParameters >= 1
            ? ep->ExceptionRecord->ExceptionInformation[0]
            : 0;
    const uintptr_t accessAddress =
        ep->ExceptionRecord->NumberParameters >= 2
            ? static_cast<uintptr_t>(ep->ExceptionRecord->ExceptionInformation[1])
            : 0;

    const uintptr_t firstSlot = g_dirtyDiagTraceOfficer + 0x88;
    const uintptr_t lastSlotEnd = firstSlot + 3 * sizeof(uintptr_t);

    // ExceptionInformation[0] == 0 : read
    if (accessType == 0 && accessAddress >= firstSlot && accessAddress < lastSlotEnd) {
      const LONG index = InterlockedIncrement(&g_traitReadTraceWriteIndex) - 1;
      if (index >= 0 && index < kTraitReadTraceCapacity) {
        g_traitReadTraceEvents[index].rip = static_cast<uintptr_t>(ep->ContextRecord->Rip);
        g_traitReadTraceEvents[index].address = accessAddress;
      }
    }

    // PAGE_GUARD는 한 번 접근하면 풀리므로 다음 명령에서 다시 겁니다.
    t_traitReadTraceRearmPage = g_traitReadTracePage;
    ep->ContextRecord->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  if (code == STATUS_SINGLE_STEP && t_traitReadTraceRearmPage != 0) {
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void *>(t_traitReadTraceRearmPage),
                   g_traitReadTracePageSize,
                   g_traitReadTraceBaseProtect | PAGE_GUARD,
                   &ignored);
    t_traitReadTraceRearmPage = 0;
    ep->ContextRecord->EFlags &= ~0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

void DisarmTraitReadTrace() {
  if (g_traitReadTraceArmed && g_traitReadTracePage && g_traitReadTracePageSize) {
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void *>(g_traitReadTracePage),
                   g_traitReadTracePageSize,
                   g_traitReadTraceBaseProtect,
                   &ignored);
  }

  g_traitReadTraceArmed = false;
  g_traitReadTracePage = 0;
  g_traitReadTracePageSize = 0;
  g_traitReadTraceBaseProtect = 0;
  t_traitReadTraceRearmPage = 0;

  if (g_traitReadTraceVeh) {
    RemoveVectoredExceptionHandler(g_traitReadTraceVeh);
    g_traitReadTraceVeh = nullptr;
  }
}

bool ArmTraitReadTrace(uintptr_t officer) {
  DisarmTraitReadTrace();
  g_traitReadTraceWriteIndex = 0;
  std::memset(g_traitReadTraceEvents, 0, sizeof(g_traitReadTraceEvents));

  if (officer < 0x10000)
    return false;

  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const uintptr_t pageSize = static_cast<uintptr_t>(si.dwPageSize);
  const uintptr_t page = (officer + 0x88) & ~(pageSize - 1);

  g_traitReadTraceVeh = AddVectoredExceptionHandler(1, TraitReadTraceVeh);
  if (!g_traitReadTraceVeh)
    return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<const void *>(page), &mbi, sizeof(mbi))) {
    RemoveVectoredExceptionHandler(g_traitReadTraceVeh);
    g_traitReadTraceVeh = nullptr;
    return false;
  }

  const DWORD baseProtect = mbi.Protect & ~PAGE_GUARD;
  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(page),
                      pageSize,
                      baseProtect | PAGE_GUARD,
                      &oldProtect)) {
    RemoveVectoredExceptionHandler(g_traitReadTraceVeh);
    g_traitReadTraceVeh = nullptr;
    return false;
  }

  g_traitReadTracePage = page;
  g_traitReadTracePageSize = pageSize;
  g_traitReadTraceBaseProtect = oldProtect & ~PAGE_GUARD;
  g_traitReadTraceArmed = true;
  return true;
}

bool IsRipFromGameExe(uintptr_t rip, uintptr_t exeBase) {
  if (!exeBase || rip < exeBase)
    return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<const void *>(rip), &mbi, sizeof(mbi)))
    return false;

  return reinterpret_cast<uintptr_t>(mbi.AllocationBase) == exeBase;
}

void FormatCodeBytesDiag(uintptr_t rip, char *out, size_t outSize) {
  if (!out || outSize == 0)
    return;

  out[0] = '\0';
  unsigned char bytes[20] = {};
  if (!SafeCopyDiag(bytes, reinterpret_cast<const void *>(rip), sizeof(bytes)))
    return;

  size_t used = 0;
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    if (used + 4 >= outSize)
      break;
    const int n = sprintf_s(out + used, outSize - used, i ? " %02X" : "%02X", bytes[i]);
    if (n <= 0)
      break;
    used += static_cast<size_t>(n);
  }
}

void FlushTraitReadTrace() {
  LONG count = g_traitReadTraceWriteIndex;
  if (count < 0)
    count = 0;
  if (count > kTraitReadTraceCapacity)
    count = kTraitReadTraceCapacity;

  struct UniqueRead {
    uintptr_t rip = 0;
    int slot = 0;
    int count = 0;
  };

  std::vector<UniqueRead> unique;
  unique.reserve(static_cast<size_t>(count));

  const uintptr_t firstSlot = g_dirtyDiagTraceOfficer + 0x88;
  const uintptr_t exeBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(L"SAN8RPK.exe"));

  for (LONG i = 0; i < count; ++i) {
    const auto &ev = g_traitReadTraceEvents[i];
    if (!IsRipFromGameExe(ev.rip, exeBase))
      continue;

    const int slot =
        static_cast<int>((ev.address - firstSlot) / sizeof(uintptr_t)) + 1;

    bool found = false;
    for (auto &u : unique) {
      if (u.rip == ev.rip && u.slot == slot) {
        ++u.count;
        found = true;
        break;
      }
    }
    if (!found)
      unique.push_back({ev.rip, slot, 1});
  }

  AddLog(u8"[기재 dirty READ] 고유 접근:%d 전체 이벤트:%d",
         static_cast<int>(unique.size()),
         static_cast<int>(count));

  for (const auto &u : unique) {
    char codeBytes[128] = {};
    FormatCodeBytesDiag(u.rip, codeBytes, sizeof(codeBytes));
    AddLog(u8"[기재 dirty READ] RIP:SAN8RPK.exe+0x%llX 슬롯%d 횟수:%d CODE:%s",
           static_cast<unsigned long long>(u.rip - exeBase),
           u.slot,
           u.count,
           codeBytes);
  }

  if (g_traitReadTraceWriteIndex > kTraitReadTraceCapacity)
    AddLog(u8"[기재 dirty READ] 이벤트가 1024개를 넘어 일부가 생략되었습니다.");
}

} // namespace

bool SetTraitViewerKirase(bool enable){return enable?Install(g_kirase,kKiraseSpec):Remove(g_kirase,kKiraseSpec);}
bool IsTraitViewerKiraseApplied(){return g_kirase.applied;}
bool SetTraitViewerInProgressEditor(bool enable){return enable?Install(g_inProgress,kInProgressSpec):Remove(g_inProgress,kInProgressSpec);}
bool IsTraitViewerInProgressEditorApplied(){return g_inProgress.applied;}

void SetInProgressTraitDirtyDiagnostics(bool enable) {
  if (g_dirtyDiagEnabled == enable)
    return;

  if (enable) {
    if (!g_inProgress.applied) {
      AddLog(u8"[기재 dirty 진단] 시작 실패: 먼저 '기재 화면 보이기'를 활성화하세요.");
      return;
    }

    if (!Install(g_dirtyDiagRuntime,kDirtyDiagSpec)) {
      AddLog(u8"[기재 dirty 진단] 시작 실패: 무장 편집 포착 훅 설치 실패.");
      return;
    }

    g_dirtyDiagEnabled = true;
    g_dirtyDiagBaselineReady = false;
    g_dirtyDiagTraceOfficer = 0;
    AddLog(u8"[기재 dirty 진단] 시작 완료. 무장 편집 화면을 새로 연 뒤 '기준 캡처'를 누르세요.");
    return;
  }

  DisarmTraitReadTrace();
  if (g_dirtyDiagRuntime.applied)
    Remove(g_dirtyDiagRuntime,kDirtyDiagSpec);

  g_dirtyDiagEnabled = false;
  g_dirtyDiagBaselineReady = false;
  g_dirtyDiagTraceOfficer = 0;
  AddLog(u8"[기재 dirty 진단] 종료.");
}

bool IsInProgressTraitDirtyDiagnosticsEnabled() {
  return g_dirtyDiagEnabled;
}

void TickInProgressTraitDirtyDiagnostics() {
}

void CaptureInProgressTraitDirtyBaseline() {
  if (!g_dirtyDiagEnabled) {
    AddLog(u8"[기재 dirty 진단] 기준 캡처 실패: 진단을 먼저 켜세요.");
    return;
  }

  const uintptr_t editor = GetDirtyDiagEditor();
  uintptr_t officer = 0;
  if (editor < 0x10000 ||
      !SafeReadPtrDiag(editor + 0x1A8, officer) ||
      officer < 0x10000) {
    AddLog(u8"[기재 dirty 진단] 기준 캡처 실패: 무장 주소 미포착. 무장 편집 화면을 닫았다 다시 여세요.");
    return;
  }

  g_dirtyDiagTraceOfficer = officer;

  uint16_t ids[3] = {};
  ReadOfficerTraitIdsDiag(officer, ids);
  AddLog(u8"[기재 dirty 진단/기준] officer:%p 현재:%u/%u/%u",
         reinterpret_cast<void *>(officer),
         static_cast<unsigned>(ids[0]),
         static_cast<unsigned>(ids[1]),
         static_cast<unsigned>(ids[2]));

  if (!ArmTraitReadTrace(officer)) {
    g_dirtyDiagTraceOfficer = 0;
    AddLog(u8"[기재 dirty 진단] READ 추적 시작 실패.");
    return;
  }

  g_dirtyDiagBaselineReady = true;
  AddLog(u8"[기재 dirty 진단] READ 추적 시작. 기재를 변경하고 기재 목록의 '결정'으로 돌아온 뒤 '현재 비교'를 누르세요.");
}

void CompareInProgressTraitDirtyState() {
  if (!g_dirtyDiagEnabled ||
      !g_dirtyDiagBaselineReady ||
      g_dirtyDiagTraceOfficer < 0x10000) {
    AddLog(u8"[기재 dirty 진단] 비교 실패: 먼저 기준 캡처를 하세요.");
    return;
  }

  // 로그를 뽑은 뒤 PAGE_GUARD를 반드시 해제합니다.
  FlushTraitReadTrace();
  DisarmTraitReadTrace();

  uint16_t ids[3] = {};
  ReadOfficerTraitIdsDiag(g_dirtyDiagTraceOfficer, ids);
  AddLog(u8"[기재 dirty 진단/현재] officer:%p 현재:%u/%u/%u",
         reinterpret_cast<void *>(g_dirtyDiagTraceOfficer),
         static_cast<unsigned>(ids[0]),
         static_cast<unsigned>(ids[1]),
         static_cast<unsigned>(ids[2]));

  g_dirtyDiagBaselineReady = false;
  AddLog(u8"[기재 dirty 진단] READ 추적 종료. 다음 테스트는 다시 '기준 캡처'부터 시작하세요.");
}

bool SetTraitViewerBaseEditor(bool enable){return enable?Install(g_baseEditor,kBaseEditorSpec):Remove(g_baseEditor,kBaseEditorSpec);}
bool IsTraitViewerBaseEditorApplied(){return g_baseEditor.applied;}

} // namespace DX11Base
