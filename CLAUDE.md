# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**PipeHat** — a native C++ Notepad++ plugin (`PipeHat.dll`) that turns Notepad++ into an
HL7 v2.x message viewer: syntax highlighting, hover tooltips with field definitions, a
dockable message tree panel, and a PHI scrubber. Windows-only, Unicode, x64. No runtime
dependencies beyond the Notepad++ / Scintilla host and Win32 common controls. (The CMake
target, DLL, and repo folder still differ in name — target/DLL are `PipeHat`, the repo dir
is `PipeHat-npp`.)

## Build

CMake + MSVC (Visual Studio 2019/2022). The build is out-of-source in `build/`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
# output: build/Release/PipeHat.dll
```

To test in Notepad++, copy `PipeHat.dll` into the Notepad++ `plugins/PipeHat/`
directory and restart. The plugin activates when a buffer starts with `MSH`, `FHS`, or
`BHS` in the first few content-bearing lines, or when the file has a `.hl7`/`.hl7v2`
extension (whichever is detected first). The dockable panel registration in
`MessageTreeView::create` passes `pszModuleName = HL7_PLUGIN_DLL` (`PipeHat.dll`) — this
**must** match the deployed DLL filename or the dock panel won't register.

There is **no test suite, linter, or CI** in this repo. Verification is manual: open a
`.hl7` sample in a debug Notepad++ build and exercise the menu commands.

## Architecture

The plugin is a single DLL exporting the standard Notepad++ plugin ABI from `src/main.cpp`
(`setInfo`, `getName`, `getFuncsArray`, `beNotified`, `messageProc`, `isUnicode`). `main.cpp`
holds the global state and wires Notepad++ notifications to the feature modules:

- **`HL7Lexer`** (`HL7Lexer.{h,cpp}`) — the core parser. Reads delimiters from the MSH
  segment (`parseMSH`) and tokenizes a line into `HL7Token`s (segment id, field/component/
  repeat/subcomponent separators, escape sequences, field values). Everything downstream —
  styling, tree, scrubbing — depends on this tokenizer being correct. It operates on one
  line at a time as `wchar_t`.
- **`ScintillaStyler`** (`ScintillaStyler.{h,cpp}`) — token→color mapping via Scintilla
  `SCI_*` messages, plus `SCN_DWELLSTART` hover tooltips and `detectHL7`. Line reads are
  routed through `SciUtils.h` (length-safe from `SCI_LINELENGTH`).
- **`SegmentDB`** (`SegmentDB.{h,cpp}`) — hard-coded in-memory table of segment/field
  definitions (names, data types, required flags) used for tooltips and the tree. Not
  version-aware; single embedded table.
- **`MessageTreeView`** (`MessageTreeView.{h,cpp}`) — dockable Win32 TreeView panel
  (registered via `NPPM_DMMREGASDCKDLG`) showing segment→field structure; clicking a node
  navigates the editor.
- **`PHIScrubber`** (`PHIScrubber.{h,cpp}`) — maps `(segment, fieldIndex)` to a PHI label
  and generates fake replacement data. The scrub command itself lives in `main.cpp`
  (`cmdScrubPHI`), which drives the lexer + scrubber and applies `SCI_REPLACETARGET` edits.

Later features (v1.1–v1.2) are **header-only** modules (no CMake edit needed since sources
are listed explicitly): **`TriggerEventDB.h`** (MSH-9/EVN-1 trigger-event + MSH-12 version
decode, plus `fieldValueAt`), **`HL7Escape.h`** (escape-sequence decode), **`ConformanceProfile.h`**
(editable per-interface `max`/`values`/`required` rules), **`Validator.h`** (advisory structural
malform checks). Their commands
(`cmdCheckConformance`, `cmdValidate`, `cmdCompareViews`, `cmdPrettyPrint`, `cmdEnableHL7`,
field navigation, folding) live in `main.cpp` and are exposed via `getFuncsArray` with
`Ctrl+Alt+Shift+` hotkeys. Conformance squiggles use Scintilla indicator 18, validation 19.

The v1.3 **settings GUI** (`SettingsDialog.{h,cpp}`, `cmdSettings`, Ctrl+Alt+Shift+P) is one
of three non-header-only modules (also `MllpTransport.{h,cpp}` and `UpdateCheck.{h,cpp}`) —
they need `.rc` dialog templates or external libraries, so they are listed in Dialog resource IDs live in `src/resource.h` (shared by `resource.rc` and
the dialog code): the dockable tree panel keeps dialog ID **1** (empty template, controls built
in code); `IDD_SETTINGS = 2` and `IDD_RULE = 3` are the modal conformance-rule editor and its
single-rule sub-dialog. On save the editor regenerates rule lines from the grid (preserving the
documented header) and `cmdSettings` reloads so Check Conformance updates without a restart. The
Settings dialog also hosts the MLLP network section (in/out via an `MllpConfig&`).

**Conformance profiles are named/switchable:** `PipeHat.profile` (default) plus `PipeHat.<name>.profile`
per interface. `runModal` takes the config dir + an in/out active-profile name (not a single path);
the dialog's profile combo + *New* button manage the files, the active name persists in `PipeHat.ini`
`[Conformance] ActiveProfile`, and `loadProfile`/`activeProfilePath` load the active one. The rule
editor's segment/field inputs are `SegmentDB`-backed dropdowns (passed via `runModal`'s `SegmentDB*`).

### MLLP over TCP (v2.0, unreleased — the plugin's only network feature)

Three layers, each isolated:

- **`MllpProtocol.h`** (header-only, pure — no sockets/UI) — MLLP framing (`<VT>…<FS><CR>`),
  an incremental `StreamParser` (TCP is a stream: handles split *and* coalesced frames), and
  `buildAck`/`parseAck`. When an MSH is split on its field separator, `token[k]` = MSH-(k+1)
  (same off-by-one as everywhere else). Standalone-tested.
- **`MllpTransport.{h,cpp}`** — owns Winsock (`ws2_32`, added to `CMakeLists.txt`). `sendSync`
  (non-blocking connect + timeout so a hung host can't freeze NPP) and `Listener` (background
  accept thread, services one connection at a time, ACKs each message, `stop()` joins the
  thread + `WSACleanup`). UI-agnostic via a handler callback; loopback-tested end to end.
- **`main.cpp` glue** — a hidden `HWND_MESSAGE` window (`mllpWndProc`, created at NPPN_READY)
  is the **only** place inbound buffers and ACK dialogs are touched: worker/listener threads
  `PostMessage` (`WM_MLLP_RECEIVED` / `WM_MLLP_ACK_RESULT`) and the UI thread does the NPP work.
  Config persists to `PipeHat.ini` (`loadMllpConfig`/`saveMllpConfig`). Menu: **Send Message
  (MLLP)** Ctrl+Alt+Shift+M, **Toggle MLLP Listener** Ctrl+Alt+Shift+L (checkmark via
  `NPPM_SETMENUITEMCHECK`). **20 menu items** total.

### v1.3.x additions (post-MLLP)

- **PHI hardening:** `generateFake` is deterministic (seeds from a hash of the original → referential
  integrity; standalone-tested); `cmdScrubPHI` runs an anonymize-mode coverage check (independent
  raw-split pass confirms every PHI field was replaced, fail-closed). MSH-7 added to the date map.
- **M7 incremental styling:** `SCN_MODIFIED` re-styles only the edited line range (MSH-line edit →
  full restyle). Fold/detect only on `linesAdded`.
- **Caret helper `analyzeCaretField`** → HL7 path + field byte range, reused by **Copy Field Path**
  (Ctrl+Alt+Shift+K), **current-field highlight** (indicator **21**, on `SCN_UPDATEUI`+`SC_UPDATE_SELECTION`),
  and **Add Conformance Rule from Field** (seeds the rule editor via `SettingsDialog::runModal`'s seed params).
- **Compare Views** (Ctrl+Alt+Shift+D) replaced clipboard compare: diffs the two Notepad++ views, boxing
  differing fields in both panes with indicator **20** (`indexDocForDiff`).
- **Copy as Rich Text** (Ctrl+Alt+Shift+W): `buildRtf` → `CF_RTF` clipboard.
- **Event log** (`logEvent` → `PipeHat.log`, menu: Open Event Log): PHI-aware metadata only.
- **Check for Updates** (`UpdateCheck.{h,cpp}`, WinHTTP, isolated + in CMake): user-initiated GitHub
  release check on a worker thread → `WM_UPDATE_RESULT`. Links `winhttp`, `shell32`.

**MLLP invariants — do not regress:** networking is OFF by default; binds are loopback-only
unless the user opts in *and* provides an address (`MllpConfig::effectiveBindAddr` fails safe);
a cleartext-PHI confirmation gates first use each session; the listener is stopped and the
window destroyed at **`NPPN_SHUTDOWN`, never in `DllMain`** (joining a thread under loader lock
deadlocks). Default startup opens no sockets.

Data flow: `beNotified` (buffer activated / modified / dwell) → `HL7Lexer.tokenize` →
either `ScintillaStyler` (colors + tooltips) or `MessageTreeView.refresh` (tree). The
scrub command runs its own three-pass lexer sweep over the whole document.

### Vendored headers

`include/npp/` contains **stripped, minimal** copies of the Notepad++/Scintilla headers
(`Scintilla.h`, `PluginInterface.h`, `Notepad_plus_msgs.h`, etc.) — only the constants and
structs this plugin uses. When adding a Scintilla message or NPP notification, add its
`#define` to the local header; do not assume the full upstream API is present. `src/Docking.h`
is a local reimplementation of the NPP docking structs.

## Critical invariants when touching the parser or scrubber

- **Scintilla line reads have no length guard.** `SCI_GETLINE(line, buf)` takes the *line
  number* (not a buffer length) and writes the whole line without NUL-terminating. Fixed
  stack buffers and `strlen()` on the result are unsafe on long lines (e.g. `OBX-5` with an
  embedded document). Prefer `SCI_LINELENGTH` + a sized buffer + the returned byte count.
- **Use `SCI_SETSTYLING` (int style), not `SCI_SETSTYLINGEX` (char* style array)** for
  single-style runs.
- **HL7 field numbering:** MSH-1 *is* the field separator character and MSH-2 *is* the
  encoding characters — the `\` in `^~\&` is the escape delimiter and will confuse a naive
  tokenizer on the MSH line. Any field-index logic must account for this MSH offset.
- **The scrubber must fail closed.** A tokenizer miss must not leave a field silently
  unscrubbed; `cmdScrubPHI` reports a success count, so a parse gap becomes a silent PHI
  leak. Scrub edits also do not empty the Scintilla undo buffer — originals are Ctrl+Z
  recoverable unless `SCI_EMPTYUNDOBUFFER` is called.

See `docs/05-CODE-REVIEW.md` for the full defect inventory and fix ordering.

## Docs vs. reality

`docs/` (00–04) is the original design brief and is **aspirational** — it describes
separate `SegmentParser`/`FieldParser`/`ComponentParser` classes, JSON segment tables, and a
`VersionRegistry` that **do not exist** in the code. The shipped implementation is simpler
and hand-coded (single `HL7Lexer`, compiled-in `SegmentDB`, no per-version field tables).
Trust the source over docs 00–04; trust `docs/05-CODE-REVIEW.md` for current known issues.
