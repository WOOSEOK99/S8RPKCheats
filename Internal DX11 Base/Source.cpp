#include "pch.h"
#include "debug.h"

namespace {

[[noreturn]] void WINAPI T05FreeLibraryAndExitThread(HMODULE module, DWORD exitCode) {
  // 정상 unload 경로에서만 실행됩니다. DllMain 종료 경로에서는 loader lock 아래
  // join하지 않도록 기존 terminating cleanup 경로를 그대로 유지합니다.
  DX11Base::ShutdownDebugScannerT05();
  ::FreeLibraryAndExitThread(module, exitCode);
}

} // namespace

#define FreeLibraryAndExitThread T05FreeLibraryAndExitThread
#include "Source_impl.inc"
#undef FreeLibraryAndExitThread
