# AGENTS.md -- PipeHat

Guidance for AI coding agents (OpenCode, etc.) working in this repo. Read before editing.
Claude Code also reads `CLAUDE.md` (fuller architecture notes); this file is the short,
regression-critical subset. Keep both in sync when invariants change.

## What this is

**PipeHat** -- a native C++ Notepad++ plugin (`PipeHat.dll`, x64, Unicode) for HL7 v2.x:
syntax highlighting, field tooltips, a dockable message tree, and a PHI scrubber. Activates
on MSH / FHS / BHS content or a `.hl7` / `.hl7v2` file extension.

Build: `cmake -S . -B build -A x64 && cmake --build build --config Release` ->
`build/Release/PipeHat.dll`. No tests in-repo; a standalone lexer harness lives outside the
repo. Docs: `docs/05-CODE-REVIEW.md` (defects + fix status), `docs/06-ROADMAP.md`.

## Non-negotiable invariants (these caused real bugs -- do not regress)

1. **Never read a Scintilla line into a fixed stack buffer.** `SCI_GETLINE` takes the *line
   number* (not a length), writes the whole line, and does **not** NUL-terminate -- fixed
   buffers overflow and `strlen()` over-reads. Always use the helpers in `src/SciUtils.h`
   (`getLineUtf8` / `getLineW`), which size from `SCI_LINELENGTH`.
2. **Single-style runs use `SCI_SETSTYLING` (int style), never `SCI_SETSTYLINGEX`** (that
   one wants a `char*` style array; passing an int faults).
3. **HL7 escape sequences must never cross a field separator.** When scanning `\...\`, stop
   at the field separator / EOL; an undelimited `\` (including the `\` in MSH-2 `^~\&`) is a
   literal, not an escape. Getting this wrong silently miscounts fields.
4. **MSH is off-by-one: MSH-1 IS the field separator, so the first value is MSH-2.** Every
   field counter (scrubber, tree, tooltip) starts one higher for `MSH`.
5. **The PHI scrubber must fail CLOSED.** A field the parser can't account for must be
   *reported*, never silently left unscrubbed. `cmdScrubPHI` counts skips and runs a
   residual identifier scan; keep that path intact. Silent PHI retention is the worst
   possible failure for this tool.
6. **Scrub empties the undo buffer** (`SCI_EMPTYUNDOBUFFER`) so originals aren't Ctrl+Z
   recoverable. Don't remove it.
7. **Never crash on malformed input.** Real-world HL7 has custom Z-segments, odd delimiters,
   and dialect quirks. Validation is advisory, never blocking.

## Conventions

- Vendored headers in `include/npp/` are **stripped** -- only constants this plugin uses. Add
  a `#define` there before using a new Scintilla message / NPP notification.
- The dock panel's `pszModuleName` must equal the deployed DLL name (`HL7_PLUGIN_DLL` =
  `PipeHat.dll`).
- `docs/00-04` are the original design brief and are **aspirational** -- several classes they
  describe don't exist. Trust the source and `docs/05`/`docs/06`.

## Feature modules (v1.1–v1.2, mostly header-only)

New features are **header-only** modules so they need no CMakeLists edit (sources are listed
explicitly, not globbed): `TriggerEventDB.h` (MSH-9/EVN-1/MSH-12 decode + `fieldValueAt`),
`HL7Escape.h` (escape decode), `ConformanceProfile.h` (editable per-interface rules),
`Validator.h` (structural malform checks). Prefer this pattern for the next feature.

- **The MSH off-by-one lives in every new field-walk.** `TriggerEventDB::fieldValueAt`, the
  conformance field splitter (`cmdCheckConformance`), and the validator's MSH check all
  special-case MSH (`MSH-N` = value after the `N-1`-th separator). Any new field iteration
  must do the same.
- Scintilla indicator slots: **18** = conformance squiggles, **19** = validation squiggles,
  **20** = compare-diff, **21** = current-field highlight (0–7 are reserved for lexers).
  Pick 22+ for new indicators.
- Menu commands live in `getFuncsArray` with static `ShortcutKey` objects (`Ctrl+Alt+Shift+`
  combos -- adding Shift dodges interceptions by other software on some machines);
  bump `g_funcItems[N]` when adding one. There are **20** items.
- Non-header-only modules (need CMake wiring): `SettingsDialog.{h,cpp}`, `MllpTransport.{h,cpp}`,
  `UpdateCheck.{h,cpp}` (WinHTTP, isolated). `main.cpp`'s hidden `HWND_MESSAGE` window marshals
  worker/listener results (MLLP receive/ACK, update check) onto the UI thread.
- `logEvent` (PHI-aware, metadata only) -> `PipeHat.log`; wire new outward/networked actions into it.

## MLLP networking (v2.0, unreleased) -- the only network feature

Three isolated layers: `MllpProtocol.h` (pure framing + ACK, header-only, standalone-tested),
`MllpTransport.{h,cpp}` (Winsock sender + threaded listener, needs `ws2_32` -- in CMake, and a
`#pragma comment(lib,...)`; loopback-tested), and `main.cpp` glue.

**Non-negotiable MLLP invariants (these are the security posture -- do not regress):**

1. **OFF by default.** `MllpConfig::enabled` defaults false; `loadMllpConfig` keeps that when
   `PipeHat.ini` is absent. Default startup opens **no sockets**.
2. **Loopback unless explicitly opted in.** Never bind a non-loopback address without
   `allowNonLoopback` AND a user-supplied `bindAddr`. Use `effectiveBindAddr()` -- it fails safe
   to `127.0.0.1`. A non-loopback bind requires an extra confirmation dialog.
3. **Cleartext-PHI confirmation** (`confirmCleartextOnce`) gates the first send/listen each
   session. PHI crosses the wire unencrypted (no TLS yet).
4. **UI work only on the UI thread.** Worker/listener threads never touch Notepad++/Scintilla
   directly -- they `PostMessage` to the hidden `HWND_MESSAGE` window (`g_hMllpWnd`,
   `mllpWndProc`); buffer creation and dialogs happen there.
5. **Teardown at `NPPN_SHUTDOWN`, never in `DllMain`.** `Listener::stop()` joins its thread;
   doing that under the DLL loader lock deadlocks.

## Dialogs / settings GUI (v1.3, NOT header-only)

`SettingsDialog.{h,cpp}` is a modal conformance-rule editor (`Settings`, Ctrl+Alt+Shift+P). Unlike the
feature modules it is **not** header-only -- it needs `.rc` dialog templates, so it is listed in
`CMakeLists.txt` and `resource.rc`.

- Dialog resource IDs live in `src/resource.h` (shared by `resource.rc` and `SettingsDialog.cpp`):
  `IDD_SETTINGS = 2`, `IDD_RULE = 3`. The dockable tree panel keeps dialog ID **1** (empty
  template, controls built in code). New dialogs take 4+.
- The editor reads/writes the same `PipeHat.profile` that `loadProfile()` parses. On save it is
  the source of truth: rule lines are regenerated from the grid and the documented header comment
  (`ConformanceProfile::defaultFileText()`) is preserved. `cmdSettings` reloads the profile after
  save so Check Conformance reflects edits without a restart.
- ListView needs `InitCommonControlsEx(ICC_LISTVIEW_CLASSES)` (done in `runModal`) and `comctl32`
  (already linked).
