#include "Cheats.h"
#include "Cheats\Social\InstantLoveCave.h"
#include "pch.h"
#include <psapi.h>
#include <string>
#include <vector>

#include "Cheats/System/SkillCountManager.h"
#include "Cheats/War/DeputyCaptureFix.h"
#include "Cheats/War/IsolatedCityCaptureFix.h"

namespace DX11Base {

    static uintptr_t s_gameBasePtrAddr = 0;
    static uint32_t s_dynamicTraitsOffset = 0;
    extern bool bLoveCave;
    void AddLog(const char *fmt, ...);
    // ───────────────────────────────────────────────
    //  유틸리티
    // ───────────────────────────────────────────────

    bool IsValidPtr(uintptr_t addr, SIZE_T size) {
        if (!addr || size == 0) return false;

        const uintptr_t endAddr = addr + size - 1;
        if (endAddr < addr) return false;
        
        // 시작 주소 검사
        MEMORY_BASIC_INFORMATION mbiStart{};
        if (VirtualQuery((LPCVOID)addr, &mbiStart, sizeof(mbiStart)) != sizeof(mbiStart))
            return false;
        if (mbiStart.State != MEM_COMMIT || (mbiStart.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        // 대부분의 런타임 검사는 1~8바이트이며 같은 메모리 영역 안에 있습니다.
        // 시작 주소의 VirtualQuery 결과가 검사 끝 주소까지 포함하면 추가 호출을 생략합니다.
        uintptr_t regionStart = (uintptr_t)mbiStart.BaseAddress;
        uintptr_t regionEnd = regionStart + mbiStart.RegionSize - 1;
        if (regionEnd >= regionStart && endAddr <= regionEnd)
            return true;

        // 실제로 다른 메모리 영역까지 걸치는 범위만 끝 주소를 추가 검사
        MEMORY_BASIC_INFORMATION mbiEnd{};
        if (VirtualQuery((LPCVOID)endAddr, &mbiEnd, sizeof(mbiEnd)) != sizeof(mbiEnd))
            return false;
        if (mbiEnd.State != MEM_COMMIT || (mbiEnd.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        return true;
    }

    uintptr_t ResolveRelAddr(uintptr_t instAddr, int opOffset, int instSize) {
        if (!instAddr)
            return 0;
        int32_t rel = *(int32_t *)(instAddr + opOffset);
        return instAddr + rel + instSize;
    }

    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const std::string &pattern) {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == ' ')
                continue;
            if (pattern[i] == '?') {
                bytes.push_back(0);
                mask.push_back(false);
                if (i + 1 < pattern.size() && pattern[i + 1] == '?')
                    i++;
            } else {
                bytes.push_back((uint8_t)strtoul(pattern.substr(i, 2).c_str(), nullptr, 16));
                mask.push_back(true);
                i++;
            }
        }
        uint8_t *pStart = (uint8_t *)start;
        size_t searchLen = (size_t)(end - start) - bytes.size();
        for (size_t i = 0; i < searchLen; ++i) {
            bool found = true;
            for (size_t j = 0; j < bytes.size(); ++j) {
                if (mask[j] && pStart[i + j] != bytes[j]) {
                    found = false;
                    break;
                }
            }
            if (found)
                return (uintptr_t)(pStart + i);
        }
        return 0;
    }

    // 1. 실시간으로 낚아챈 주인공 주소를 저장할 전역 변수
    uintptr_t g_HeroAddr = 0;

    // 2. 원래 코드를 저장할 트램펄린
    typedef void(__fastcall *tGetHeroStats)(void *);
    tGetHeroStats oGetHeroStats = nullptr;

    // 3. 우리가 가로챌 후킹 함수 (Naked 함수 또는 인라인 어셈블리 활용)
    // x64에서는 __declspec(naked)를 사용할 수 없으므로, 트램펄린 지점으로 점프하기
    // 전 레지스터를 저장합니다.
    extern "C" void HookHandler();

    // 5. 실제 레지스터를 낚아채는 로직 (Proxy 함수)
    // 이 함수는 게임 엔진이 주인공 스탯을 읽을 때마다 호출되어 g_HeroAddr를
    // 최신화합니다.

    void __fastcall hkHeroLogic(void *rcx) {
        // 어셈블리 수준에서 R15를 가져와야 하므로, 인라인 어셈블리가 지원되지 않는
        // x64 환경에서는 별도의 .asm 파일을 쓰거나 레지스터 접근 헬퍼를 사용해야
        // 합니다. 여기서는 개념적으로 g_HeroAddr = (R15 값) 이 들어간다고 보시면
        // 됩니다.

        // [임시 헬퍼] 호출 시점의 R15 값을 낚아챘다고 가정 (프로젝트 설정에 따라
        // 어셈블리 구현 필요) g_HeroAddr = GetR15Register();

        return oGetHeroStats(rcx);
    }

    // ───────────────────────────────────────────────
    //  기존 함수들 (변경 없음)
    // ───────────────────────────────────────────────

    uintptr_t GetGameBaseFast() {
        if (!s_gameBasePtrAddr)
            return 0;
        __try {
            return *reinterpret_cast<uintptr_t *>(s_gameBasePtrAddr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    uintptr_t GetGameBase() {
        if (!s_gameBasePtrAddr)
            return 0;

        static uintptr_t s_cachedGameBase = 0;
        static uint64_t s_lastGameBaseProbeMs = 0;

        const uint64_t now = GetTickCount64();
        uintptr_t base = *(uintptr_t *)s_gameBasePtrAddr;

        // 포인터 값이 같고 최근에 검증했으면 VirtualQuery 생략 (Loops 등에서 매프레임 호출됨)
        if (base != 0 && base == s_cachedGameBase && (now - s_lastGameBaseProbeMs) < 300ull)
            return base;

        s_lastGameBaseProbeMs = now;

        if (!IsValidPtr(s_gameBasePtrAddr, 8)) {
            s_cachedGameBase = 0;
            return 0;
        }
        base = *(uintptr_t *)s_gameBasePtrAddr;
        if (!base || !IsValidPtr(base, 8)) {
            s_cachedGameBase = 0;
            return 0;
        }
        s_cachedGameBase = base;
        return base;
    }

    bool InitCheats() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi)))
            return false;
        uintptr_t imgEnd = exeBase + mi.SizeOfImage;

        // gameBase 스캔
        const std::string gameBasePat = "4C 8B 05 ? ? ? ? 41 0F B7";
        uintptr_t foundAddr = FindPattern(exeBase, imgEnd, gameBasePat);
        if (foundAddr)
            s_gameBasePtrAddr = ResolveRelAddr(foundAddr, 3, 7);

        // 저장된 전법 횟수 설정 불러오기 (S8RPK_skill_counts.json)
        LoadSkillCounts();

        // V2.0 포로 처리 개선 Step 1.
        // 별도 UI 토글 없이 기본 적용하며, 원본 바이트가 맞지 않으면
        // DeputyCaptureFix 내부에서 아무 것도 쓰지 않고 안전하게 보류합니다.
        InstallDeputyCaptureFix();
        InstallIsolatedCityCaptureFix();

        return (s_gameBasePtrAddr != 0);
    }

    // 단일 항목 수정 (VirtualProtect 포함 - UI 버튼용)
    void ModifyStat(uintptr_t targetBase, uintptr_t offset, int value, int size) {
      if (!targetBase)
        return;
      uintptr_t targetAddr = targetBase + offset;
      __try {
        if (IsValidPtr(targetAddr, size)) {
          DWORD oldProt;
          if (VirtualProtect((LPVOID)targetAddr, size, PAGE_READWRITE, &oldProt)) {
            ModifyStatFast(targetAddr, value, size);
            VirtualProtect((LPVOID)targetAddr, size, oldProt, &oldProt);
          } else {
            AddLog(u8"[ERROR] VirtualProtect 실패 (Addr:%p, Size:%d)", (void *)targetAddr, size);
          }
        } else {
          AddLog(u8"[ERROR] IsValidPtr 실패 (Addr:%p, Size:%d)", (void *)targetAddr, size);
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[CRITICAL] ModifyStat 예외 발생 (Addr:%p)", (void *)targetAddr);
      }
    }

    // 초고속 수정 (VirtualProtect 제외 - 대량 처리 루프용)
    // 호출 전에 호출자가 VirtualProtect로 전체 영역을 쓰기 가능하게 만들어야 함.
    void ModifyStatFast(uintptr_t targetAddr, int value, int size) {
      __try {
        if (size == 1)
          *(unsigned char *)targetAddr = (unsigned char)value;
        else if (size == 2)
          *(short *)targetAddr = (short)value;
        else if (size == 4)
          *(int *)targetAddr = value;
        else if (size == 8)
          *(uint64_t *)targetAddr = (uint64_t)value;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        // 루프 내에서 대량 처리 시 로그가 너무 많아질 수 있으므로 로그는 생략하거나 카운트만 할 수도 있음
      }
    }

    void MaximizeHeroStats() {
        if (!g_HeroAddr || !IsValidPtr(g_HeroAddr, 0x150)) {
            AddLog("[FAIL] 주인공 주소를 아직 찾지 못했습니다. (정보창을 한번 "
                   "열어주세요)");
            return;
        }

        // 분석된 오프셋 적용
        struct Stats {
            unsigned char lead;  // +AA
            unsigned char war;   // +AB
            unsigned char intel; // +AC
            unsigned char pol;   // +AD
            unsigned char cha;   // +AE
        };

        Stats *s = (Stats *)(g_HeroAddr + 0xAA);

        DWORD old;
        if (VirtualProtect(s, sizeof(Stats), PAGE_READWRITE, &old)) {
            s->lead = 100;
            s->war = 100;
            s->intel = 100;
            s->pol = 100;
            s->cha = 100;
            VirtualProtect(s, sizeof(Stats), old, &old);
            AddLog("[SUCCESS] 주인공(%p) 모든 능력치 100 완료!", (void *)g_HeroAddr);
        }
    }

    void SetInstantAttitude(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        static uintptr_t targetAddr = 0;
        if (targetAddr == 0)
            targetAddr = FindPattern(exeBase, exeBase + 0x2000000, "44 88 44 07 70");
        if (!targetAddr)
            return;

        DWORD oldProtect;
        // 5바이트 명령어 패치
        if (VirtualProtect((LPVOID)targetAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            if (enable) {
                // [패치] mov byte ptr [rdi+rax+70], 64 (C6 44 07 70 64)
                unsigned char patch[] = {0xC6, 0x44, 0x07, 0x70, 0x64};
                memcpy((void *)targetAddr, patch, 5);
            } else {
                // [원복] 원래 코드: 44 88 44 07 70
                unsigned char original[] = {0x44, 0x88, 0x44, 0x07, 0x70};
                memcpy((void *)targetAddr, original, 5);
            }
            VirtualProtect((LPVOID)targetAddr, 5, oldProtect, &oldProtect);
        }
    }
    static uintptr_t addr = 0;
    static BYTE original[8];
    static bool saved = false;
    static bool applied = false;

    void SetInstantLove() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!addr)
            addr = FindPattern(exeBase, exeBase + 0x3000000, "0F B6 84 18 F0 00 00 00");
        if (!addr)
            return;

        if (!saved) {
            memcpy(original, (void *)addr, 8);
            saved = true;
        }

        if (!applied) {
            DWORD old;
            VirtualProtect((LPVOID)addr, 8, PAGE_EXECUTE_READWRITE, &old);
            BYTE patch[8] = {0xB8, 0x64, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90};
            memcpy((void *)addr, patch, 8);

            VirtualProtect((LPVOID)addr, 8, old, &old);

            applied = true;
        }
    }

    void RestoreInstantLove() {
        if (addr && saved && applied) {

            DWORD old;
            VirtualProtect((LPVOID)addr, 8, PAGE_EXECUTE_READWRITE, &old);

            memcpy((void *)addr, original, 8);

            VirtualProtect((LPVOID)addr, 8, old, &old);

            applied = false;
        }
    }

    // 결혼 패치 관련 변수
    uintptr_t marriageAddr = 0;
    BYTE marriageOriginal[2]; // 75 21 (또는 해당 위치의 2바이트)
    bool marriageSaved = false;
    bool marriageApplied = false;

    static uintptr_t FindMarriageConditionAddress() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return 0;

        // 기존 프로젝트에서 실제 동작 확인된 결혼 제한 패턴.
        // "mov rax,[rdi+10] / cmp r14,rax" 지점을 찾고,
        // 바로 2바이트 앞의 JNE(75 xx) / 패치된 JMP(EB xx)를 사용합니다.
        const uintptr_t searchAddr =
            FindPattern(exeBase, exeBase + 0x3000000,
                        "48 8B 47 10 4C 3B F0");
        if (!searchAddr || searchAddr <= exeBase + 2)
            return 0;

        const uintptr_t found = searchAddr - 2;
        if (!IsValidPtr(found, 2))
            return 0;

        const uint8_t op = *(uint8_t*)found;
        if (op != 0x75 && op != 0xEB)
            return 0;

        return found;
    }

    void ToggleMarriageCondition() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        // 1. 기존 동작 확인 패턴으로 JNE/JMP 지점을 검색.
        if (!marriageAddr) {
            marriageAddr = FindMarriageConditionAddress();
            if (marriageAddr) {
                AddLog(
                    "[Marriage] original pattern found: %p / current=%02X %02X",
                    (void*)marriageAddr,
                    *(uint8_t*)(marriageAddr + 0),
                    *(uint8_t*)(marriageAddr + 1));
            } else {
                AddLog("[Marriage] original marriage pattern not found");
            }
        }

        if (!marriageAddr)
            return;

        // 2. 원본 백업
        if (!marriageSaved) {
            memcpy(marriageOriginal, (void *)marriageAddr, 2);
            marriageSaved = true;
        }

        DWORD old;
        VirtualProtect((LPVOID)marriageAddr, 2, PAGE_EXECUTE_READWRITE, &old);

        if (!marriageApplied) {
            // CT와 동일하게 JNE(75 xx) -> JMP(EB xx), 분기 변위는 유지.
            *(BYTE *)marriageAddr = 0xEB;
            marriageApplied = true;
        } else {
            // 원본 복구 (75 21)
            memcpy((void *)marriageAddr, marriageOriginal, 2);
            marriageApplied = false;
        }

        VirtualProtect((LPVOID)marriageAddr, 2, old, &old);
    }

    void SetMarriageCondition(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) {
            marriageApplied = false;
            return;
        }

        // 기존 프로젝트에서 동작 확인된 패턴으로 실제 분기 주소를 해석합니다.
        if (!marriageAddr) {
            marriageAddr = FindMarriageConditionAddress();
            if (marriageAddr) {
                AddLog(
                    "[Marriage] original pattern found: %p / current=%02X %02X",
                    (void*)marriageAddr,
                    *(uint8_t*)(marriageAddr + 0),
                    *(uint8_t*)(marriageAddr + 1));
            } else {
                AddLog("[Marriage] original marriage pattern not found");
                marriageApplied = false;
                return;
            }
        }

        if (!IsValidPtr(marriageAddr, 2)) {
            AddLog("[Marriage] patch address invalid: %p",
                   (void*)marriageAddr);
            marriageAddr = 0;
            marriageSaved = false;
            marriageApplied = false;
            return;
        }

        const BYTE currentOp = *(BYTE*)marriageAddr;
        const bool actualApplied = (currentOp == 0xEB);

        // 설정 로드 시 marriageApplied bool이 먼저 true가 될 수 있으므로
        // bool이 아니라 실제 명령어 바이트를 기준으로 판단합니다.
        if (actualApplied == enable) {
            marriageApplied = actualApplied;
            AddLog(
                "[Marriage] already %s / bytes=%02X %02X",
                actualApplied ? "ON" : "OFF",
                *(uint8_t*)(marriageAddr + 0),
                *(uint8_t*)(marriageAddr + 1));
            return;
        }

        // 원본은 실제 JNE 상태에서만 백업합니다.
        if (!marriageSaved) {
            if (currentOp != 0x75) {
                AddLog(
                    "[Marriage] original backup blocked: unexpected opcode=%02X",
                    currentOp);
                marriageApplied = actualApplied;
                return;
            }
            memcpy(marriageOriginal, (void*)marriageAddr, 2);
            marriageSaved = true;
        }

        DWORD old = 0;
        if (!VirtualProtect((LPVOID)marriageAddr, 2,
                            PAGE_EXECUTE_READWRITE, &old)) {
            AddLog("[Marriage] VirtualProtect failed: %p",
                   (void*)marriageAddr);
            marriageApplied = actualApplied;
            return;
        }

        if (enable) {
            // JNE(75 xx) -> JMP(EB xx), 분기 변위는 그대로 유지.
            *(BYTE*)marriageAddr = 0xEB;
        } else {
            memcpy((void*)marriageAddr, marriageOriginal, 2);
        }

        FlushInstructionCache(
            GetCurrentProcess(), (LPCVOID)marriageAddr, 2);

        DWORD tmp = 0;
        VirtualProtect((LPVOID)marriageAddr, 2, old, &tmp);

        const BYTE verifyOp = *(BYTE*)marriageAddr;
        marriageApplied = (verifyOp == 0xEB);

        AddLog(
            "[Marriage] request=%s / actual=%s / bytes=%02X %02X / addr=%p",
            enable ? "ON" : "OFF",
            marriageApplied ? "ON" : "OFF",
            *(uint8_t*)(marriageAddr + 0),
            *(uint8_t*)(marriageAddr + 1),
            (void*)marriageAddr);
    }

    bool g_isHeroHookInstalled = false; // 후킹 상태 플래그

    bool InstallHeroHook() {
        if (g_isHeroHookInstalled)
            return true; // 이미 설치됨

        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        // 스크립트에서 확인한 주인공 매력 읽는 지점
        uintptr_t targetAddr = FindPattern(exeBase, exeBase + 0x3000000, "41 0F B6 B7 AE 00 00 00");

        if (targetAddr && MH_Initialize() == MH_OK) {
            if (MH_CreateHook((LPVOID)targetAddr, &hkHeroLogic, (LPVOID *)&oGetHeroStats) == MH_OK) {
                if (MH_EnableHook((LPVOID)targetAddr) == MH_OK) {
                    g_isHeroHookInstalled = true;
                    AddLog(u8"[SUCCESS] 주인공 추적 후킹 설치 완료!");
                    return true;
                }
            }
        }
        AddLog(u8"[FAIL] 후킹 설치 실패 (패턴을 찾지 못함)");
        return false;
    }

    OfficerInfo GetHeroInfo() {
        OfficerInfo info = {
            0,
        };
        uintptr_t gameBase = GetGameBase(); // 이미 구현하신 함수
        if (!gameBase)
            return info;

        // 1. 포인터 컨테이너 접근
        uintptr_t *pContainer = (uintptr_t *)(gameBase + 0x3B8);
        if (IsBadReadPtr(pContainer, 8) || !*pContainer)
            return info;

        uintptr_t container = *pContainer;

        // 2. 주인공은 보통 배열의 첫 번째(0번)이거나 특정 인덱스에 있습니다.
        // 여기서는 '주인공' 주소를 가리키는 포인터를 읽어옵니다.
        // [치트엔진 이미지 기반] 컨테이너 내부의 첫 번째 포인터가 주인공일 확률이
        // 높음
        uintptr_t *pOfficerData = (uintptr_t *)(container); // +0x0 또는 +0xE0 등 확인 필요
        if (IsBadReadPtr(pOfficerData, 8) || !*pOfficerData)
            return info;

        uintptr_t dataAddr = *pOfficerData;
        info.address = dataAddr;

        // 3. 실제 데이터 읽기 (오프셋 적용)
        info.id = *(unsigned short *)(dataAddr + 0x08);
        info.lead = *(unsigned char *)(dataAddr + 0xAA);
        info.war = *(unsigned char *)(dataAddr + 0xAB);
        info.intel = *(unsigned char *)(dataAddr + 0xAC);
        info.pol = *(unsigned char *)(dataAddr + 0xAD);
        info.cha = *(unsigned char *)(dataAddr + 0xAE);

        return info;
    }

} // namespace DX11Base
