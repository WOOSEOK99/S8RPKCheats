# Custom trait compatibility analysis

Analysis inputs (2026-09-24):

- `D:\game\SAN8R_113\SAN8RPK.exe` SHA-256 `000D450345728C23BEBCA581AB37B8BCF25266172767B5E6DEFB10A72A8C9457`
- `x64\Release\version.dll` SHA-256 `01BFB8941D58F231231244B5BD6B8073A97E9630B91EA2312DB3B9C2BC982C0A`
- The EXE's on-disk `.text` bytes are encrypted. Function bytes below were read from a running process and matched to the same build's PDB. The original DLL's `.text` was disassembled from disk.

| Game RVA | PDB role / observed behavior | Original DLL detour | New handling |
| --- | --- | --- | --- |
| `+17AAD60` | `person_util::IsHavePersonality(PersonData const*, PERSONALITY_ID)`; direct 3-slot ID check | `+B9BC0`: original first, then custom effect-type match, except caller `+1C7A135` | Existing hook retained; records matched IDs and caller RVA |
| `+1C79C90` | `EveryMonthTurn::UpdatePersonData()`; calls `+17AAD60` at `+1C7A130` | `+BA2E0`: original first, then custom effect type 166, signed value sum, amount cap 99,999 | Original first, same supplemental loop |
| `+13F91E0` | `MessageData::SetPersonalityID(PERSONALITY_ID)`; stores ID at `+211C` and refreshes message data | `+B9EB0`: substitutes the TLS matched ID before calling original | Remaps only for verified query/consumer pairs |
| `+16E63F0` | `DataCenter::GetPersonalityData(PERSONALITY_ID)`; returns a pointer from the ID array | `+B9EF0`: substitutes the TLS matched ID before calling original | Original first, then custom pointer for the verified conference pair |
| `+170C080` | `PersonData::IsHavePersonarity(PERSONALITY_ID) const`; direct 3-slot ID check | `+BA1B0`: original first, custom fallback only for requested ID 11 | Same ID 11 scope, with match cache for the later message |
| `+18A5E30` | `transfer::func::DirectionEnbouShinryo(PersonData*, PersonData*, ptr_list&, CityData*, CityData*)` | `+BA250`: checks second person for ID 26 custom match, then calls original | Same precheck, original result preserved |

The EXE's source data pointer at `exe+2E98BC8` leads to a root whose personality pointer array starts at `root+57A4D0`. Array entry `id` points to the same record as `record0 + id * 0x40`. This confirmed the fallback table offset `0x57A4D0` and record ID offset `+8` in live memory. The monthly person pointer array is `root+576C88` through `root+57A4D0`, 1801 entries. The original DLL checks a virtual validity function at vtable slot `+0x48`, scans three held records, reads six effects at `record+0x0A` with stride six, interprets effect type 166's value with `movsx`, and adds positive sums to `person+0x300` up to 99,999.

The message and data bridges require both a matching cached base trait ID and an exact pair of query and consumer return RVAs. Unknown pairs pass through unchanged and produce a bounded diagnostic log. The current verified pairs are in `kMessageBridgeSites` and `kDataBridgeSites` in `TraitConfigRuntime.cpp`.

Hook target signatures and first bytes were checked against the live EXE. A mismatched prologue leaves that hook uninstalled and logs the reason. The installation is retried from `TickTraitConfigRuntime()`.

The Release `hid.dll` build validates compilation and linkage. Gameplay outcomes still need an in-game run with a custom trait carrying each relevant effect type. In particular, the current `san8r_traits_config.json` has no custom effect type 166, so the monthly supplement cannot be exercised with that file unchanged.
