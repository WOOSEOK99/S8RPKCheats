#pragma once

#include "../../Cheats.h"
#include "../../NotificationManager.h"
#include "../../showlog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace DX11Base {

  inline bool bCouncilContinueAfterMove = false;

  namespace CouncilContinueAfterMoveDetail {
    constexpr uintptr_t kGlobalPtrOffset = 0x02E98BC8;
    constexpr uintptr_t kCommandCountOffset = 0x02E99220;
    constexpr uintptr_t kCommandArrayOffset = 0x02E99230;
    constexpr uintptr_t kCommandWrapperVtableOffset = 0x0269D3D0;
    constexpr uintptr_t kMoveCommandVtableOffset = 0x026A7FC8;
    constexpr uintptr_t kAssignCommandVtableOffset = 0x026A7F98;
    constexpr uintptr_t kAutoWarVtableOffset = 0x025BFF20;
    constexpr uintptr_t kManualPhaseVtableOffset = 0x026CF3D8;

    constexpr uintptr_t kMoveHookOffset = 0x019622ED;
    constexpr uintptr_t kFlagsHookOffset = 0x017B1A70;
    constexpr uintptr_t kWarHookOffset = 0x01441479;
    constexpr uintptr_t kManualCaptureHookOffset = 0x01E4DC61;
    constexpr uintptr_t kManualPhaseHookOffset = 0x01BD3246;
    constexpr uintptr_t kAssignHookOffset = 0x01964660;

    constexpr uintptr_t kMoveContinueOffset = 0x019622FB;
    constexpr uintptr_t kFlagsContinueOffset = 0x017B1A80;
    constexpr uintptr_t kWarContinueOffset = 0x0144148A;
    constexpr uintptr_t kManualCaptureContinueOffset = 0x01E4DC70;
    constexpr uintptr_t kManualPhaseBypassOffset = 0x01BD32BE;
    constexpr uintptr_t kManualPhaseDoneOffset = 0x01BD3334;
    constexpr uintptr_t kManualPhaseNormalOffset = 0x01BD3257;
    constexpr uintptr_t kAssignContinueOffset = 0x0196466E;

    constexpr size_t kCaveSize = 0x1000;
    constexpr size_t kTemplateSize = 0x500;

    constexpr size_t kMoveEntry = 0x000;
    constexpr size_t kFlagsEntry = 0x060;
    constexpr size_t kWarEntry = 0x180;
    constexpr size_t kManualCaptureEntry = 0x210;
    constexpr size_t kManualPhaseEntry = 0x2E0;
    constexpr size_t kAssignEntry = 0x3F0;
    constexpr size_t kPendingDataOffset = 0x450;

    constexpr size_t kGlobalPtrQword = 0x480;
    constexpr size_t kCommandCountQword = 0x488;
    constexpr size_t kCommandArrayQword = 0x490;
    constexpr size_t kCommandWrapperVtableQword = 0x498;
    constexpr size_t kMoveCommandVtableQword = 0x4A0;
    constexpr size_t kAssignCommandVtableQword = 0x4A8;
    constexpr size_t kAutoWarVtableQword = 0x4B0;
    constexpr size_t kManualPhaseVtableQword = 0x4B8;
    constexpr size_t kMoveContinueQword = 0x4C0;
    constexpr size_t kFlagsContinueQword = 0x4C8;
    constexpr size_t kWarContinueQword = 0x4D0;
    constexpr size_t kManualCaptureContinueQword = 0x4D8;
    constexpr size_t kManualPhaseBypassQword = 0x4E0;
    constexpr size_t kManualPhaseDoneQword = 0x4E8;
    constexpr size_t kManualPhaseNormalQword = 0x4F0;
    constexpr size_t kAssignContinueQword = 0x4F8;

    inline constexpr uint8_t kMoveOriginal[] = {
        0x48, 0x8B, 0x43, 0x58, 0xBA, 0x3C, 0x00, 0x00, 0x00,
        0xB9, 0x03, 0x00, 0x00, 0x00};
    inline constexpr uint8_t kFlagsOriginal[] = {
        0x83, 0x8B, 0x20, 0x03, 0x00, 0x00, 0x03, 0x66,
        0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    inline constexpr uint8_t kWarOriginal[] = {
        0x48, 0x8B, 0x0B, 0xBA, 0x03, 0x00, 0x00, 0x00, 0x48,
        0x83, 0xC1, 0x10, 0x48, 0x89, 0x74, 0x24, 0x38};
    inline constexpr uint8_t kManualCaptureOriginal[] = {
        0xC6, 0x44, 0x24, 0x40, 0x00, 0xC6, 0x44, 0x24, 0x38,
        0x00, 0x44, 0x88, 0x64, 0x24, 0x30};
    inline constexpr uint8_t kManualPhaseOriginal[] = {
        0xF6, 0x83, 0x20, 0x03, 0x00, 0x00, 0x02, 0x0F, 0x85,
        0xE1, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4B, 0x10};
    inline constexpr uint8_t kAssignOriginal[] = {
        0x48, 0x8B, 0x43, 0x50, 0xBA, 0x3C, 0x00, 0x00, 0x00,
        0xB9, 0x03, 0x00, 0x00, 0x00};

    inline constexpr char kTemplateHex[] =
        "488b4358ba3c000000b903000000415241539c4885c074334c8b50204d85d2742a4c3b536874244c8b1d520400004d8b1b4d85db7415493983e00000"
        "00750c80a020030000fdba030000009d415b415aff256a040000cccccccccccccccccccc9c50515241504151415241534839f30f84d50000004c8b15"
        "040400004d8b124d85d20f84c20000004939b2e00000000f85b50000004c396e200f85ab0000004c396b200f85a10000004c8b15d8030000498b1248"
        "85d20f848e0000004883fa400f87840000004c8b15c3030000498b0a4885c97475488b014885c074644c8b15b40300004c39107558488b8078040000"
        "4885c07455488b40084885c0744c488b40404885c074434c8b15920300004c391074204c8b158e0300004c3910752b4839705075254c396858741f4c"
        "3968387519eb304839705875114c396868740beb224883c10848ffca758b415b415a415941585a59589d838b2003000003ff2565030000415b415a41"
        "5941585a59589dff2553030000cccccccccccccccccccccc9c50415241534c8b15230300004c39177561488b05e7020000488b004885c07452488b80"
        "e00000004885c074464c8b5f384d85db743d48837f300074364c395f3074304c395820752a4c8b50184d85d2742141534d8b9b900000004d85db7411"
        "4d395310415b750b80a020030000fdeb02415b415b415a589d488b0bba030000004883c1104889742438ff25c00200009c5041524153488b05630200"
        "00488b004885c00f849b000000483998e00000000f858e00000048c7050f020000000000004c8d9030e01e004c39d67577803e007572f680e4710000"
        "017569f683200300000275604d85ed745b4c39a8b0df1e0075524c8b53204d85d274494d39ea74444c8b5b184d85db743b4c399810df1e0075324889"
        "1dbf0100004c8915c00100004c892dc10100004c891dc2010000440fb798aa73000044891dbb0100004889058c010000415b415a589dc644244000c6"
        "442438004488642430ff25f9010000cc9c5041524153488b05630100004885c00f84cd00000048391d5b0100000f85c000000048c705420100000000"
        "00004839c50f85ac000000483998e00000000f859f0000004c8b158d0100004c39160f858f00000083be78040000040f8582000000440fb790aa7300"
        "00443915280100007571f680e47100000175684c8b15fe0000004c39968804000075584c8b15fe0000004c395318754b4c39968004000075424c8b1d"
        "e00000004c395b2075354d8b9b900000004d85db74294d39531075234c8b5b104d85db741a41807b080d741380a320030000fd415b415a589dff251d"
        "010000415b415a589df68320030000027406ff2510010000488b4b10ff250e010000cccccccccccccccccccccccccccc488b4350ba3c000000b90300"
        "0000415241539c4885c074394c8b50204d85d274304c3b5358742a4c3b533875244c8b1d5c0000004d8b1b4d85db7415493983e0000000750c80a020"
        "030000fdba030000009d415b415aff25ac000000cccccccc000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000111111111111111122222222222222223333333333333333444444444444444455555555555555556666666666666666"
        "777777777777777788888888888888889999999999999999aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbccccccccccccccccddddddddddddddddeeeeeeee"
        "eeeeeeeeababababababababcdcdcdcdcdcdcdcd";

    struct HookSpec {
      uintptr_t offset;
      const uint8_t* original;
      size_t size;
      size_t entryOffset;
    };

    inline constexpr HookSpec kHooks[] = {
        {kMoveHookOffset, kMoveOriginal, sizeof(kMoveOriginal), kMoveEntry},
        {kFlagsHookOffset, kFlagsOriginal, sizeof(kFlagsOriginal), kFlagsEntry},
        {kWarHookOffset, kWarOriginal, sizeof(kWarOriginal), kWarEntry},
        {kManualCaptureHookOffset, kManualCaptureOriginal, sizeof(kManualCaptureOriginal), kManualCaptureEntry},
        {kManualPhaseHookOffset, kManualPhaseOriginal, sizeof(kManualPhaseOriginal), kManualPhaseEntry},
        {kAssignHookOffset, kAssignOriginal, sizeof(kAssignOriginal), kAssignEntry},
    };

    inline bool gApplied = false;
    inline uintptr_t gCave = 0;
    inline std::array<std::array<uint8_t, 17>, std::size(kHooks)> gPatched{};

    inline int HexNibble(char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    }

    inline bool DecodeTemplate(std::vector<uint8_t>& out) {
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

    inline bool ReadBytesRaw(uintptr_t address, uint8_t* out, size_t size) {
      if (!out || size == 0 || !IsValidPtr(address, size))
        return false;
      __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    inline bool BytesEqual(uintptr_t address, const uint8_t* expected, size_t size) {
      std::vector<uint8_t> current(size);
      return ReadBytesRaw(address, current.data(), size) &&
             std::memcmp(current.data(), expected, size) == 0;
    }

    inline bool WriteBytesRaw(uintptr_t address, const uint8_t* bytes, size_t size) {
      if (!bytes || size == 0 || !IsValidPtr(address, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool ok = false;
      __try {
        std::memcpy(reinterpret_cast<void*>(address), bytes, size);
        ok = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);
      if (ok)
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), size);
      return ok;
    }

    inline bool PutQword(std::vector<uint8_t>& blob, size_t offset, uintptr_t value) {
      if (offset + sizeof(uint64_t) > blob.size())
        return false;
      const uint64_t v = static_cast<uint64_t>(value);
      std::memcpy(blob.data() + offset, &v, sizeof(v));
      return true;
    }

    inline void BuildAbsoluteJump(uintptr_t destination, uint8_t* out, size_t size) {
      std::memset(out, 0x90, size);
      out[0] = 0xFF;
      out[1] = 0x25;
      out[2] = out[3] = out[4] = out[5] = 0x00;
      const uint64_t dst = static_cast<uint64_t>(destination);
      std::memcpy(out + 6, &dst, sizeof(dst));
    }

    inline bool BuildPayload(uintptr_t exeBase, std::vector<uint8_t>& out) {
      if (!DecodeTemplate(out))
        return false;

      std::fill(out.begin() + kPendingDataOffset, out.begin() + kGlobalPtrQword, 0);

      return PutQword(out, kGlobalPtrQword, exeBase + kGlobalPtrOffset) &&
             PutQword(out, kCommandCountQword, exeBase + kCommandCountOffset) &&
             PutQword(out, kCommandArrayQword, exeBase + kCommandArrayOffset) &&
             PutQword(out, kCommandWrapperVtableQword, exeBase + kCommandWrapperVtableOffset) &&
             PutQword(out, kMoveCommandVtableQword, exeBase + kMoveCommandVtableOffset) &&
             PutQword(out, kAssignCommandVtableQword, exeBase + kAssignCommandVtableOffset) &&
             PutQword(out, kAutoWarVtableQword, exeBase + kAutoWarVtableOffset) &&
             PutQword(out, kManualPhaseVtableQword, exeBase + kManualPhaseVtableOffset) &&
             PutQword(out, kMoveContinueQword, exeBase + kMoveContinueOffset) &&
             PutQword(out, kFlagsContinueQword, exeBase + kFlagsContinueOffset) &&
             PutQword(out, kWarContinueQword, exeBase + kWarContinueOffset) &&
             PutQword(out, kManualCaptureContinueQword, exeBase + kManualCaptureContinueOffset) &&
             PutQword(out, kManualPhaseBypassQword, exeBase + kManualPhaseBypassOffset) &&
             PutQword(out, kManualPhaseDoneQword, exeBase + kManualPhaseDoneOffset) &&
             PutQword(out, kManualPhaseNormalQword, exeBase + kManualPhaseNormalOffset) &&
             PutQword(out, kAssignContinueQword, exeBase + kAssignContinueOffset);
    }

    inline bool ValidateOriginals(uintptr_t exeBase) {
      for (const HookSpec& hook : kHooks) {
        if (!BytesEqual(exeBase + hook.offset, hook.original, hook.size)) {
          AddLog(u8"[평정이동] 적용 보류: 원본 바이트 불일치 (+%llX)",
                 static_cast<unsigned long long>(hook.offset));
          return false;
        }
      }
      return true;
    }

    inline bool ValidatePatched(uintptr_t exeBase) {
      for (size_t i = 0; i < std::size(kHooks); ++i) {
        if (!BytesEqual(exeBase + kHooks[i].offset, gPatched[i].data(), kHooks[i].size))
          return false;
      }
      return true;
    }
  } // namespace CouncilContinueAfterMoveDetail

  inline bool IsCouncilContinueAfterMoveApplied() {
    return CouncilContinueAfterMoveDetail::gApplied;
  }

  inline bool SetCouncilContinueAfterMove(bool enable) {
    using namespace CouncilContinueAfterMoveDetail;

    const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (!exeBase) {
      AddLog(u8"[평정이동] SAN8RPK.exe 베이스를 찾지 못했습니다.");
      return false;
    }

    if (enable) {
      if (gApplied)
        return true;

      if (!ValidateOriginals(exeBase))
        return false;

      if (!gCave) {
        gCave = reinterpret_cast<uintptr_t>(
            VirtualAlloc(nullptr, kCaveSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!gCave) {
          AddLog(u8"[평정이동] 코드케이브 할당 실패");
          return false;
        }
      }

      std::vector<uint8_t> payload;
      if (!BuildPayload(exeBase, payload)) {
        AddLog(u8"[평정이동] payload 구성 실패");
        return false;
      }

      std::memset(reinterpret_cast<void*>(gCave), 0xCC, kCaveSize);
      std::memcpy(reinterpret_cast<void*>(gCave), payload.data(), payload.size());
      FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(gCave), kCaveSize);

      for (size_t i = 0; i < std::size(kHooks); ++i) {
        std::fill(gPatched[i].begin(), gPatched[i].end(), 0x90);
        BuildAbsoluteJump(gCave + kHooks[i].entryOffset,
                          gPatched[i].data(), kHooks[i].size);
      }

      size_t written = 0;
      for (; written < std::size(kHooks); ++written) {
        if (!WriteBytesRaw(exeBase + kHooks[written].offset,
                           gPatched[written].data(), kHooks[written].size))
          break;
      }

      if (written != std::size(kHooks)) {
        while (written > 0) {
          --written;
          WriteBytesRaw(exeBase + kHooks[written].offset,
                        kHooks[written].original, kHooks[written].size);
        }
        AddLog(u8"[평정이동] hook 적용 실패 - 이번 변경분 원복");
        return false;
      }

      if (!ValidatePatched(exeBase)) {
        for (size_t i = 0; i < std::size(kHooks); ++i)
          WriteBytesRaw(exeBase + kHooks[i].offset, kHooks[i].original, kHooks[i].size);
        AddLog(u8"[평정이동] hook 검증 실패 - 원본 복구");
        return false;
      }

      gApplied = true;
      AddLog(u8"[평정이동] 활성화 완료: 이동/배정/자동전투/수동전투 후 평정 지속");
      return true;
    }

    if (!gApplied)
      return true;

    if (!ValidatePatched(exeBase)) {
      AddLog(u8"[평정이동] 해제 거부: hook 지점이 외부에서 변경되었습니다.");
      return false;
    }

    size_t restored = 0;
    for (; restored < std::size(kHooks); ++restored) {
      if (!WriteBytesRaw(exeBase + kHooks[restored].offset,
                         kHooks[restored].original, kHooks[restored].size))
        break;
    }

    if (restored != std::size(kHooks)) {
      while (restored > 0) {
        --restored;
        WriteBytesRaw(exeBase + kHooks[restored].offset,
                      gPatched[restored].data(), kHooks[restored].size);
      }
      AddLog(u8"[평정이동] 해제 실패 - 기존 ON 상태 복구 시도");
      return false;
    }

    gApplied = false;
    // CT와 동일하게 코드케이브는 프로세스 종료까지 유지합니다.
    AddLog(u8"[평정이동] 비활성화 완료");
    return true;
  }

  inline void DrawCouncilContinueAfterMoveSection(float scale) {
    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0));
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), u8"[ 평정 이동 ]");

    if (ImGui::Checkbox(u8"도시 이동 후 평정 지속", &bCouncilContinueAfterMove)) {
      const bool requested = bCouncilContinueAfterMove;
      if (!SetCouncilContinueAfterMove(requested))
        bCouncilContinueAfterMove = IsCouncilContinueAfterMoveApplied();
      NotifyFeatureToggle(u8"도시 이동 후 평정 지속", bCouncilContinueAfterMove);
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                         u8"평정 중 주인공의 도시가 바뀌어도 평정을 계속 진행합니다.");
      ImGui::TextUnformatted(u8"- 일반 이동 / 배정 이동");
      ImGui::TextUnformatted(u8"- 자동전투 승리 후 점령지 이동");
      ImGui::TextUnformatted(u8"- 수동전투 승리 후 점령지 이동");
      ImGui::TextUnformatted(u8"- 같은 이동 명령의 다른 장수에게 종료 플래그가 번지는 동작 보정");
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                         u8"주인공의 행동 완료 bit0은 유지하고 평정 종료에 관여하는 bit1만 해제합니다.");
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f),
                         u8"※ 6개 hook 지점의 원본 바이트가 모두 일치할 때만 적용됩니다.");
      ImGui::EndTooltip();
    }

    ImGui::EndGroup();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float pad = 6.0f * scale;
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(min.x - pad, min.y - pad),
        ImVec2(max.x + pad, max.y + pad),
        IM_COL32(255, 165, 0, 140), 8.0f, 0, 1.2f);
    ImGui::Spacing();
  }

} // namespace DX11Base
