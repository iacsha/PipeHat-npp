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
6b. **Segment IDs are `A-Z` then two of `A-Z0-9`. The digits are not optional.** `PV1`, `NK1`,
   `GT1`, `IN1`, `IN2`, `PD1`, `DG1`, `PR1`, `PV2` hold the heaviest PHI, and `cmdScrubPHI`
   skips any line whose segment ID is empty. An all-alpha check leaks guarantor SSNs while
   reporting a clean scrub -- this shipped (`docs/05-CODE-REVIEW.md` C6). Never `iswalpha`/
   `iswalnum` here (locale-dependent; matches lowercase and accented Unicode). Keep
   `isSegmentStart` defined via `extractSegmentID`, and keep the scrub coverage check on
   `rawSegmentID` -- it must **not** call `HL7Lexer`, or it goes blind exactly when it matters.
   Re-run `tests/SegmentIDTest.cpp` after any lexer or PHI-map change.
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

## Endpoint profiles (unreleased) -- a profile owns its connection

`src/EndpointProfile.h` (header-only, **pure**: no Windows headers, no MSVC-only helpers) makes the
`.profile` file the per-interface unit. It gains `[Profile]` (`application`, `engine`, `messageType`,
`environment` as the closed enum `Local|DEV|QA|PROD`, `displayName`, `description`, `inherits`) and
`[Connection]` (`host`, `sendPort`, `listenPort`, `bindAddr`, `allowNonLoopback`) beside the rules.
A file with **no section header at all is read as rules from line one**, so old profiles still load.
`endpoint::resolve` walks the inheritance chain through a `readFile` callback, so every Win32 call
stays in `main.cpp`. Run `tests/EndpointProfileTest.cpp` after touching any of it.

**Non-negotiable endpoint-profile invariants (do not regress):**

1. **`enabled` and `saveReceived` stay global in `PipeHat.ini`.** One starts networking, the other
   writes cleartext PHI to disk, and selecting a profile is a menu click. `endpoint::parse` refuses
   both inside `[Connection]` with a warning rather than ignoring them.
2. **`environment` may only ADD friction.** It is a label; `allowNonLoopback` plus `bindAddr` is the
   real gate and `effectiveBindAddr()` still fails safe. There is no `requiresLessConfirmation()`
   and there must never be one -- reading the enum as permission makes a typo a bypass.
2b. **The non-loopback opt-in is ANDed across two places.** `PipeHat.ini` holds the global
   permission and the profile holds its own `allowNonLoopback`; a bind needs both. A profile is a
   file, and files arrive by email and get dropped into config folders, so a profile alone must
   never be able to expose a PHI receiver on the network. In `main.cpp` the global half lives in
   `g_mllpAllowNonLoopbackGlobal` and only the ANDed result reaches `g_mllp.allowNonLoopback`.
   `saveMllpConfig` writes the global variable, never the ANDed value, or a session with a loopback
   profile active would silently clear the user's standing permission.
2c. **An unreadable `environment` is treated as PROD, an absent one as nothing.**
   `Environment::Unrecognized` is a distinct value from `Unspecified` for this reason:
   a file that tried to name an environment and failed gets the most-confirming treatment, while
   every legacy profile with no environment at all keeps its current friction. A typo must not be
   the quiet path.
3. **A `[Connection]` is never inherited.** Rules inherit, the address does not. A child with no
   `[Connection]` resolves to loopback defaults, never its parent's, or `inherits` becomes a
   privilege path.
4. **Switching the active profile stops a running listener** -- from the Switch command and from the
   Settings dialog alike. The port and bind address just changed under it; re-arming is explicit.
5. **The cleartext-PHI acknowledgement is keyed on `endpointFingerprint()`, per session** --
   host, send port, listen port, effective bind address, allowNonLoopback and environment. Keying
   it on the profile NAME instead would let an acknowledgement survive an edit to the very host it
   authorized, and would let two profiles sharing a label share an approval.
5b. **The migration is startup-only**, gated by a session `static bool`. `loadProfile` also runs on
   every switch, where `g_mllp` holds the OUTGOING profile's connection -- ungated, it writes the
   previous endpoint's host and ports into any profile lacking a `[Connection]`, so "dev" stays
   pointed at production and loses the PROD confirm. Do not delete the call instead; it is the whole
   upgrade path for an existing `AllowNonLoopback=1` user.
5c. **The listener stops when the bind TARGET moves**, not only when the profile name changes --
   `listenPort` and `effectiveBindAddr` are compared across the Settings dialog, so an in-place edit
   of the active profile is caught.
6. **The migration of the old global `[MLLP]` block is idempotent by construction** -- it writes only
   a profile that has no `[Connection]`, and writing one removes that condition. No ini flag.
7. **Keep `EndpointProfile.h` free of Windows headers and MSVC-isms.** `std::stoi` throws and
   `_wtoi` is MSVC-only, hence `detail::parseIntOr`. This is what keeps the test runnable anywhere.

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
