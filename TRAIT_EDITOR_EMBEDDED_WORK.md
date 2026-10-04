# Embedded trait editor expansion work log

## Scope

Expand the existing `기재 이름/설명 편집기` so the embedded default trait set can be edited without changing the existing `version.dll` source-priority policy.

The implementation must preserve the existing game-default 72-row behavior unless a step explicitly replaces part of it with a verified equivalent.

## Branch / baseline

- Repository: `WOOSEOK99/S8RPKCheats`
- Branch: `feature/trait-editor-embedded`
- Starting `main` HEAD: `e988c4e358086d536bdcc0f992a39cd125849a8a`
- Build/test ownership: **user only**. ChatGPT must not build or run the project.

## Confirmed current behavior

- `TraitTextEditorData.cpp` owns a static 72-row editor data set.
- `trait_texts.ini` persists edits by `ROW:<1-based row index>`.
- The editor UI iterates `GetTraitTextEditRows()` and therefore has no independent 72-item UI limit.
- When `version.dll` is absent, `TraitConfigRuntime` loads the embedded `S8RPK_traits_default.json` and publishes custom names/descriptions for the active trait table.
- The embedded JSON contains ID-keyed `customNames` data for many custom traits through the 253 range.
- Existing runtime name editing is string-based (`oldName -> newName`), which is unsafe for duplicate custom trait names if the embedded set is simply appended.
- Existing game-default description editing uses code-cave string replacement and contains special handling for `괴물` / `잔병첩보`.

## Safety decisions

1. Do not assume the 72 editor row indexes are identical to in-game trait IDs unless verified.
2. Keep the legacy 72-row edit path behavior intact.
3. Add embedded custom traits using their explicit JSON trait IDs.
4. Do not change the `version.dll` priority policy. When `version.dll` exists, embedded runtime data remains disabled.
5. Avoid broad refactors and unrelated formatting changes.
6. Do not build or run tests; only source-level review/diff verification is performed here.

## Planned steps

### Step 0 — resumable work log and branch

- [x] Create `feature/trait-editor-embedded` from the verified `main` HEAD.
- [x] Add this resumable work log.

### Step 1 — editor row model and embedded-row loading

Goal: make the editor data model distinguish legacy game rows from embedded ID-backed rows.

Planned changes:

- Extend `TraitTextEditRow` with source/identity metadata needed for embedded rows.
- Keep the existing 72 static rows unchanged in content and order.
- Load embedded `customNames` from `IDR_JSON_TRAITS_DEFAULT` and append only editable named custom traits.
- Avoid duplicates where an embedded entry represents an existing legacy row; do not guess ID mappings.
- Keep `GetTraitTextEditRows()` as the UI source so the window expands automatically.

Status: pending.

### Step 2 — persistence format with backward compatibility

Goal: save embedded edits by stable trait ID without breaking existing `trait_texts.ini` files.

Planned changes:

- Continue accepting legacy `ROW:` records for the original 72 rows.
- Add an ID-keyed record form for embedded rows (for example `ID:<traitId>:...`).
- Save legacy rows in the legacy-compatible form and embedded rows by ID.
- Ignore unknown/stale IDs safely.

Status: pending.

### Step 3 — embedded runtime name override

Goal: edit embedded custom names without relying on duplicate-prone string matching.

Planned changes:

- Preserve the existing 72-row name hook behavior.
- Add an ID-based override path for embedded rows in the no-`version.dll` runtime backend.
- Prefer integrating the override with `TraitConfigRuntime`'s existing `traitObject + 0x08` ID lookup / published name pointer path rather than adding another broad game hook.
- Ensure apply/remove/reapply are safe and do not free strings still potentially referenced by the UI.

Status: pending.

### Step 4 — embedded runtime description override

Goal: edit embedded descriptions through the runtime's existing ID-keyed description data instead of forcing them through the legacy 72-row code-cave replacement path.

Planned changes:

- Preserve legacy 72-row description hooks unchanged.
- Add embedded ID-based description overrides in `TraitConfigRuntime`.
- Invalidate/resync any description pointer tables required by the runtime after changes.
- Keep format-string (`%`) handling compatible with the existing runtime's normal/format description pointers.

Status: pending.

### Step 5 — editor UI distinction and validation

Goal: make the expanded list understandable and keep validation source-correct.

Planned changes:

- Show embedded trait IDs in the list for ID-backed rows.
- Keep the existing 5 UTF-16 code-unit name limit unless runtime evidence requires otherwise.
- Keep description format-token validation.
- Clearly distinguish legacy game rows and embedded custom rows without changing unrelated UI behavior.

Status: pending.

### Step 6 — source review / diff verification

- Review branch diff against starting `main` HEAD.
- Confirm only intended files changed.
- Confirm no build/test claims are made.
- Update this file with exact final branch HEAD, changed files, unresolved risks, and user-side test checklist.

Status: pending.

## Resume procedure for a new chat

1. Read this file first.
2. Inspect branch `feature/trait-editor-embedded` and its current HEAD.
3. Compare it with starting HEAD `e988c4e358086d536bdcc0f992a39cd125849a8a`.
4. Read the latest completed/pending step markers in this file.
5. Inspect actual changed files before continuing; do not rely only on chat history.
6. Do not build. The user performs all builds/tests.

## User-side test checklist (to be finalized)

- Existing 72 default traits still load/edit/save/apply as before.
- Embedded custom traits appear in the editor when the embedded runtime is active.
- Two traits sharing the same displayed name can be edited independently.
- Embedded edits survive save/reload through `trait_texts.ini`.
- Apply / remove / reapply does not accumulate hooks or corrupt displayed text.
- With external `version.dll`, the established external runtime priority remains unchanged.
