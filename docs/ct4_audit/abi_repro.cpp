// Offline reproduction. Only synthetic local objects; no game process access.
#define NOMINMAX
#include <string>
#include <cstdio>
#include "../../Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h"
using namespace DX11Base::IsolatedTerritoryMovementDetail;

extern "C" uintptr_t CaptureConnectedReturn(decltype(&AreConnected), uintptr_t, uintptr_t);
__declspec(noinline) uint8_t __fastcall Valid(uintptr_t) { return 1; }
alignas(16) uintptr_t vt[10] = {};
struct alignas(16) Object { unsigned char bytes[0xA0] = {}; };
void Put(Object& obj, size_t offset, uintptr_t value) {
    std::memcpy(obj.bytes + offset, &value, sizeof(value));
}
uintptr_t Ptr(Object& obj) { return reinterpret_cast<uintptr_t>(&obj); }
void Init(Object& obj) { Put(obj, 0, reinterpret_cast<uintptr_t>(vt)); }
void Check(const char* name, Object& src, Object& dst, bool expected) {
    const uintptr_t raw = CaptureConnectedReturn(&AreConnected, Ptr(src), Ptr(dst));
    const bool logical = (raw & 0xFF) != 0;
    const bool stub = static_cast<uint32_t>(raw) != 0;
    std::printf("%s raw_RAX=%016llX AL=%u test_EAX=%u expected=%u mismatch=%u\n", name,
        static_cast<unsigned long long>(raw), logical, stub, expected, logical != stub);
    if (logical != expected) std::exit(2);
}
int main() {
    vt[9] = reinterpret_cast<uintptr_t>(&Valid);
    Object owner, holder, mainland, mainland2, isolated;
    for (auto* obj : {&owner, &holder, &mainland, &mainland2, &isolated}) Init(*obj);
    Put(holder, 0x10, Ptr(owner));
    for (auto* city : {&mainland, &mainland2, &isolated}) Put(*city, 0x90, Ptr(holder));
    Put(mainland, 0x20, Ptr(mainland2));
    Put(mainland2, 0x20, Ptr(mainland));
    Check("isolated_to_mainland", isolated, mainland, false);
    Check("mainland_to_isolated", mainland, isolated, false);
    Check("mainland_to_mainland2", mainland, mainland2, true);
    Check("same_city", mainland, mainland, true);
    return 0;
}
