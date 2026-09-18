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
static uintptr_t g_dirtyDiagBaselineEditor = 0;
static unsigned char g_dirtyDiagBaseline[0x400] = {};

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

void LogDirtyDiagTraits(const char *tag, uintptr_t editor) {
  uintptr_t officer = 0;
  uintptr_t backup[3] = {};
  uintptr_t current[3] = {};
  SafeReadPtrDiag(editor + 0x1A8, officer);
  SafeReadPtrDiag(editor + 0x228, backup[0]);
  SafeReadPtrDiag(editor + 0x230, backup[1]);
  SafeReadPtrDiag(editor + 0x238, backup[2]);
  if (officer > 0x10000) {
    SafeReadPtrDiag(officer + 0x88, current[0]);
    SafeReadPtrDiag(officer + 0x90, current[1]);
    SafeReadPtrDiag(officer + 0x98, current[2]);
  }
  AddLog(
      u8"[기재 dirty 진단/%s] editor:%p officer:%p 원본:%u/%u/%u 현재:%u/%u/%u",
      tag,
      reinterpret_cast<void *>(editor),
      reinterpret_cast<void *>(officer),
      static_cast<unsigned>(TraitIdFromPtrDiag(backup[0])),
      static_cast<unsigned>(TraitIdFromPtrDiag(backup[1])),
      static_cast<unsigned>(TraitIdFromPtrDiag(backup[2])),
      static_cast<unsigned>(TraitIdFromPtrDiag(current[0])),
      static_cast<unsigned>(TraitIdFromPtrDiag(current[1])),
      static_cast<unsigned>(TraitIdFromPtrDiag(current[2])));
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
      AddLog(u8"[기재 dirty 진단] 시작 실패: 12BA1A9 진단 훅 설치 실패.");
      return;
    }
    g_dirtyDiagEnabled = true;
    g_dirtyDiagBaselineReady = false;
    g_dirtyDiagBaselineEditor = 0;
    AddLog(u8"[기재 dirty 진단] 시작 완료. 무장 편집 화면을 새로 열어 editor 주소를 잡으세요.");
    return;
  }

  if (g_dirtyDiagRuntime.applied)
    Remove(g_dirtyDiagRuntime,kDirtyDiagSpec);
  g_dirtyDiagEnabled = false;
  g_dirtyDiagBaselineReady = false;
  g_dirtyDiagBaselineEditor = 0;
  AddLog(u8"[기재 dirty 진단] 종료.");
}

bool IsInProgressTraitDirtyDiagnosticsEnabled() {
  return g_dirtyDiagEnabled;
}

void TickInProgressTraitDirtyDiagnostics() {
  // 버튼식 비교로 변경. 매 프레임 로그를 발생시키지 않습니다.
}

void CaptureInProgressTraitDirtyBaseline() {
  if (!g_dirtyDiagEnabled) {
    AddLog(u8"[기재 dirty 진단] 기준 캡처 실패: 진단을 먼저 켜세요.");
    return;
  }
  const uintptr_t editor = GetDirtyDiagEditor();
  if (editor < 0x10000) {
    AddLog(u8"[기재 dirty 진단] 기준 캡처 실패: editor 미포착. 무장 편집 화면을 닫았다 다시 여세요.");
    return;
  }
  if (!SafeCopyDiag(g_dirtyDiagBaseline,reinterpret_cast<const void *>(editor),sizeof(g_dirtyDiagBaseline))) {
    AddLog(u8"[기재 dirty 진단] 기준 캡처 실패: editor 메모리 읽기 실패.");
    return;
  }
  g_dirtyDiagBaselineReady = true;
  g_dirtyDiagBaselineEditor = editor;
  LogDirtyDiagTraits("기준",editor);
  AddLog(u8"[기재 dirty 진단] 기준 캡처 완료.");
}

void CompareInProgressTraitDirtyState() {
  if (!g_dirtyDiagEnabled || !g_dirtyDiagBaselineReady) {
    AddLog(u8"[기재 dirty 진단] 비교 실패: 먼저 기준 캡처를 하세요.");
    return;
  }

  const uintptr_t editor = GetDirtyDiagEditor();
  if (editor < 0x10000 || editor != g_dirtyDiagBaselineEditor) {
    AddLog(u8"[기재 dirty 진단] 비교 실패: editor가 바뀌었습니다. 같은 무장 화면에서 다시 기준 캡처하세요.");
    return;
  }

  unsigned char now[0x400] = {};
  if (!SafeCopyDiag(now,reinterpret_cast<const void *>(editor),sizeof(now))) {
    AddLog(u8"[기재 dirty 진단] 비교 실패: editor 메모리 읽기 실패.");
    return;
  }

  LogDirtyDiagTraits("현재",editor);

  int logged = 0;
  for (size_t off = 0; off + 8 <= sizeof(now); off += 8) {
    uint64_t before = 0, after = 0;
    std::memcpy(&before,g_dirtyDiagBaseline + off,8);
    std::memcpy(&after,now + off,8);
    if (before == after)
      continue;
    AddLog(u8"[기재 dirty 진단/DIFF] +0x%03zX : %016llX -> %016llX",
           off,
           static_cast<unsigned long long>(before),
           static_cast<unsigned long long>(after));
    if (++logged >= 80) {
      AddLog(u8"[기재 dirty 진단] 변경 항목이 많아 80개까지만 표시합니다.");
      break;
    }
  }

  if (logged == 0)
    AddLog(u8"[기재 dirty 진단] editor +0x000~+0x3FF 범위에서 변경 없음.");
  else
    AddLog(u8"[기재 dirty 진단] 비교 완료: %d개 qword 변경.",logged);
}

bool SetTraitViewerBaseEditor(bool enable){return enable?Install(g_baseEditor,kBaseEditorSpec):Remove(g_baseEditor,kBaseEditorSpec);}
bool IsTraitViewerBaseEditorApplied(){return g_baseEditor.applied;}

} // namespace DX11Base
