# Embedded trait editor expansion work log

## Scope

Expand the existing `기재 이름/설명 편집기` so the embedded default trait set can be edited without changing the existing `version.dll` source-priority policy.

The implementation preserves the existing game-default 72-row behavior and adds a separate ID-backed path for embedded default traits.

## Branch / baseline

- Repository: `WOOSEOK99/S8RPKCheats`
- Branch: `feature/trait-editor-embedded`
- Starting `main` HEAD: `e988c4e358086d536bdcc0f992a39cd125849a8a`
- Latest implementation HEAD before this document update: `3a05df390b4ff3a1a4623013df114bcb2794a7ff`
- Build/test ownership: **user only**. ChatGPT must not build or run the project.

## Confirmed behavior / design

- `TraitTextEditorData.cpp` still owns the original static 72-row editor data set.
- Existing `trait_texts.ini` persistence and `ROW:<1-based row index>` behavior are unchanged.
- The embedded JSON `customNames` table is parsed with the same runtime parser already used by `TraitConfigRuntime`.
- A JSON `customNames` key is treated as a 0-based table index; editor `traitId` is `index + 1`, matching the runtime's `nativeNamePtrs[traitId - 1]` lookup.
- Existing 72-row name/description hooks remain unchanged.
- Embedded rows are stored and applied separately by real trait ID, so duplicate display names can be edited independently.
- Embedded edits are persisted separately in `trait_texts_embedded.ini`; this intentionally avoids changing or migrating the legacy `trait_texts.ini` format.
- Embedded text overrides are active only when `version.dll` is absent. The established external `version.dll + san8r_traits_config.json` priority policy is unchanged.
- The cheat's own `주인공` / `모든 무장` editor does not use the game's trait text getter for custom traits; it reads `CustomTraitDisplay.h`, which keeps a separate JSON-derived display cache. Therefore applied embedded editor text must also be mirrored into that display cache.

## Safety decisions

1. Do not assume the 72 editor row indexes are identical to in-game trait IDs.
2. Keep the legacy 72-row edit path behavior intact.
3. Add embedded custom traits using explicit JSON/runtime trait IDs.
4. Do not change the `version.dll` priority policy.
5. Use the existing embedded runtime name/description pointer machinery rather than installing another broad hook.
6. Keep old published string allocations alive, matching the existing runtime safety rule for UI-held pointers.
7. Mirror only **successfully applied** embedded edits into the cheat-internal display cache. Unsaved/unapplied typing must not change `주인공` / `모든 무장` displays.
8. Do not build or run tests; only source-level review/diff verification is performed here.

## Steps

### Step 0 — resumable work log and branch

- [x] Created `feature/trait-editor-embedded` from verified `main` HEAD `e988c4e358086d536bdcc0f992a39cd125849a8a`.
- [x] Added this resumable work log before implementation.

Status: complete.

### Step 1 — embedded editor row model and catalog

- [x] Extended `TraitTextEditRow` with optional `embedded` / `traitId` metadata while keeping the existing 4-field aggregate initializers valid.
- [x] Added `GetEmbeddedTraitTextCatalog()` using `IDR_JSON_TRAITS_DEFAULT` and the runtime's existing `ParseCustomMetaLine()` behavior.
- [x] Added a separate embedded editor-row vector so the legacy 72-row vector is not reordered or remapped.
- [x] Only named embedded `customNames` entries are exposed as editable embedded rows.

Status: complete.

### Step 2 — ID-based persistence without legacy migration

Original plan proposed adding `ID:` records to `trait_texts.ini`. During implementation this was changed to the safer option below so the legacy file/parser is untouched.

- [x] Existing `trait_texts.ini` behavior remains unchanged.
- [x] Embedded edits use `trait_texts_embedded.ini` next to `trait_texts.ini`.
- [x] Embedded format: `ID:<traitId>:<hex name>:<hex desc>`.
- [x] Unknown/stale IDs are ignored while loading.
- [x] Window save/load and auto-apply handle legacy and embedded files independently.

Status: complete.

### Step 3 — embedded runtime name override

- [x] Preserved the existing 72-row name hook.
- [x] Added ID-based embedded overrides to `TraitConfigRuntime`.
- [x] Overrides republish the runtime's `nativeNamePtrs` table rather than comparing old display-name strings.
- [x] Apply/clear uses stable allocated strings through the existing `PublishNativeTexts()` path.
- [x] `version.dll` presence blocks non-empty embedded overrides rather than changing external priority behavior.

Status: complete.

### Step 4 — embedded runtime description override

- [x] Preserved legacy 72-row description and special-description hooks.
- [x] Embedded descriptions republish `nativeDescPtrs` / `nativeDescFormatPtrs` by real trait ID.
- [x] Republish marks `descTablesSynced = false` so the existing 71~200 text-table synchronization runs again on the next runtime tick.
- [x] Existing 201+ descmap fallback automatically reads the republished pointer arrays.
- [x] Existing `%` format conversion in `PublishNativeTexts()` remains the single formatting implementation.

Status: complete at source level; user runtime testing required.

### Step 5 — editor UI and validation

- [x] Editor list now has separate headings for the legacy 72 rows and embedded ID-backed rows.
- [x] Embedded rows display the real trait ID.
- [x] Existing UTF-8, 5 UTF-16-code-unit name, 512-code-unit description, and format-token rules are mirrored for embedded rows.
- [x] Apply / remove / save / reset / automatic saved-edit loading now include both sources.
- [x] Fixed a source-review finding where switching between legacy and embedded lists could retain the previous vector reference for the rest of the frame.

Status: complete at source level.

### Step 6 — source review / diff verification

- [x] Compared branch against starting `main` HEAD.
- [x] Confirmed no project/build-system file changes were required because implementation stays in already-compiled translation units / headers.
- [x] Confirmed the existing 72-row `.cpp` data and legacy hook `.cpp` files were not modified.
- [x] Confirmed `version.dll` source-priority logic remains in place.
- [x] No build or game runtime test was executed.

Status: source review complete; user build/runtime verification pending.

### Step 7 — fix cheat-internal `주인공` / `모든 무장` stale trait text

User runtime report:

- `게임 정보 > 무장정보` showed the edited embedded trait name/description correctly.
- The cheat's `주인공` and `모든 무장` officer editor still showed the pre-edit embedded name/description.

Root cause confirmed in source:

- `OfficerDetail.cpp` includes and uses `CustomTraitDisplay.h` for configured custom trait metadata.
- `CustomTraitDisplay.h` independently parses/caches JSON `customNames` and therefore did not see the editor's runtime `nativeNamePtrs` / `nativeDescPtrs` overrides.

Fix:

- [x] Added a small applied-editor display overlay map to `CustomTraitDisplay.h`, keyed by the same custom index (`traitId - 1`).
- [x] `GetCustomTraitDisplayInfo()` now merges that overlay only when external `version.dll` is absent.
- [x] `TraitTextEditorWindow::ApplyEmbeddedRows()` updates the internal display overlay only **after** the runtime override succeeds.
- [x] `RemoveAll()` clears the internal display overlay when the runtime embedded override is cleared.
- [x] Auto-apply uses the same `ApplyAll()` path, so saved embedded edits also repopulate the internal display overlay after restart.
- [x] No changes were made to the game getter hooks or legacy 72-row path for this fix.

Implementation commits:

- `330fcb9fb9d529e11bd5b899717e3cf51bd0a97c` — `fix: reflect applied embedded trait text in editor displays`
- `3a05df390b4ff3a1a4623013df114bcb2794a7ff` — `fix: sync embedded trait edits to internal officer displays`

Status: source-level fix complete; user build/runtime verification required.

## Current changed files versus starting `main`

- `Internal DX11 Base/Cheats/Officer/CustomTraitDisplay.h`
- `Internal DX11 Base/Cheats/Officer/TraitConfigRuntime.cpp`
- `Internal DX11 Base/Cheats/Officer/TraitConfigRuntime.h`
- `Internal DX11 Base/Cheats/Officer/TraitTextEditorData.h`
- `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`
- `TRAIT_EDITOR_EMBEDDED_WORK.md`

## Known verification points / risks

- Build has intentionally not been run. Compile/link errors, if any, must be reported from the user's build and fixed on this same branch.
- Runtime behavior must be checked in game for both description paths: IDs in the existing description text-table synchronization range and IDs handled by the descmap fallback.
- The editor intentionally refuses non-empty embedded overrides while external `version.dll` is present; legacy 72-row editing continues through its existing version.dll-aware backend.
- Embedded rows with duplicate displayed names must be tested independently to confirm ID isolation in game.
- `PublishNativeTexts()` intentionally retains old allocated strings, matching pre-existing runtime behavior; repeated apply operations therefore favor pointer safety over reclaiming those small allocations immediately.
- The new `CustomTraitDisplay` overlay is UI-only and does not alter trait grades or assignability; it only replaces non-empty applied name/description fields.

## Resume procedure for a new chat

1. Read this file first.
2. Inspect branch `feature/trait-editor-embedded` and its current HEAD.
3. Compare it with starting HEAD `e988c4e358086d536bdcc0f992a39cd125849a8a`.
4. Treat `3a05df390b4ff3a1a4623013df114bcb2794a7ff` as the latest implementation commit before the current document update.
5. Inspect actual changed files before continuing; do not rely only on chat history.
6. If the user reports a build error, fix only that verified error first and commit it on this branch.
7. Do not build. The user performs all builds/tests.

## User-side build/runtime checklist

1. Build the branch using the user's normal configuration. Send the exact compiler/linker errors if the build fails.
2. Start without external `version.dll` and open `기재 이름/설명 편집기`.
3. Confirm the original 72 rows still appear and behave as before.
4. Confirm a second `내장 기본기재 (ID 기반)` section appears with real IDs.
5. Edit one embedded name and description, press `적용`, then verify all three display paths show the same edited text:
   - `게임 정보 > 무장정보`
   - cheat `주인공`
   - cheat `모든 무장`
6. Press `적용 해제` and verify all three paths return to the embedded default text.
7. Edit two embedded traits that share the same displayed name and verify they change independently.
8. Edit one embedded description in the lower-ID text-table range and one in the 201+ range if available; verify both display correctly.
9. Save, restart, and confirm `trait_texts_embedded.ini` edits auto-apply to both game UI and cheat-internal officer UI.
10. Reapply multiple times and switch rapidly between legacy/embedded list entries; confirm no UI crash or stale selection.
11. Repeat with external `version.dll` present: verify embedded override application is rejected and the established external trait runtime remains authoritative.
