// Offline compiler evidence only. This TU is not part of the game DLL project.
#define NOMINMAX
#include <string>
#include "../../Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h"

using namespace DX11Base::IsolatedTerritoryMovementDetail;
// Taking these addresses forces the real, unchanged helper bodies to be emitted.
extern "C" {
decltype(&AreConnected) ct4_connected = &AreConnected;
decltype(&FilterConnectedList) ct4_filter = &FilterConnectedList;
decltype(&MovementContextAllowed) ct4_context = &MovementContextAllowed;
}
