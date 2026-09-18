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

static Runtime g_kirase,g_inProgress,g_baseEditor;

} // namespace

bool SetTraitViewerKirase(bool enable){return enable?Install(g_kirase,kKiraseSpec):Remove(g_kirase,kKiraseSpec);}
bool IsTraitViewerKiraseApplied(){return g_kirase.applied;}
bool SetTraitViewerInProgressEditor(bool enable){return enable?Install(g_inProgress,kInProgressSpec):Remove(g_inProgress,kInProgressSpec);}
bool IsTraitViewerInProgressEditorApplied(){return g_inProgress.applied;}
bool SetTraitViewerBaseEditor(bool enable){return enable?Install(g_baseEditor,kBaseEditorSpec):Remove(g_baseEditor,kBaseEditorSpec);}
bool IsTraitViewerBaseEditorApplied(){return g_baseEditor.applied;}

} // namespace DX11Base
