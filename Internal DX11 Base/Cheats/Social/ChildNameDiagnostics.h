#pragma once

// Generated-child runtime name diagnostics are temporarily disabled.
//
// IMPORTANT:
// - version.dll is a third-party mod DLL and must not be treated as native game code.
// - The previously observed 0x42-spaced name input buffer was an UI/input-side structure,
//   not a verified permanent officer-name table.
// - Do not perform writes, PAGE_GUARD probes, or speculative runtime-name overrides here.
//
// Next investigation must start from SAN8RPK.exe/native game code paths only.

namespace DX11Base {
namespace ChildNameDiagnostics {

static void Tick() {
  // Intentionally disabled until a SAN8RPK.exe-native officer name path is verified.
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
