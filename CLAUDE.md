# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**PipeHat** — a native C++ Notepad++ plugin (`PipeHat.dll`) that turns Notepad++ into an
HL7 v2.x message viewer: syntax highlighting, hover tooltips with field definitions, a
dockable message tree panel, and a PHI scrubber. Windows-only, Unicode, x64. No runtime
dependencies beyond the Notepad++ / Scintilla host and Win32 common controls. (The CMake
target, DLL, and repo folder still differ in name — target/DLL are `PipeHat`, the repo dir
is `hl7-npp-plugin`.)

## Build

CMake + MSVC (Visual Studio 2019/2022). The build is out-of-source in `build/`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
# output: build/Release/PipeHat.dll
```

To test in Notepad++, copy `PipeHat.dll` into the Notepad++ `plugins/PipeHat/`
directory and restart. The plugin activates when the **first line of a buffer starts
with `MSH`** (there is no file-extension trigger). The dockable panel registration in
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
  `SCI_*` messages, plus `SCN_DWELLSTART` hover tooltips and `detectHL7`. Also the wrapper
  for all Scintilla line reads (`sciGetLine`).
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
malform checks), and **`MessageDiff.h`** (segment/field-aware clipboard diff). Their commands
(`cmdCheckConformance`, `cmdValidate`, `cmdCompareClipboard`, `cmdPrettyPrint`, `cmdEnableHL7`,
field navigation, folding) live in `main.cpp` and are exposed via `getFuncsArray` with
`Ctrl+Alt+` hotkeys. Conformance squiggles use Scintilla indicator 18, validation 19.

The v1.3 **settings GUI** (`SettingsDialog.{h,cpp}`, `cmdSettings`, Ctrl+Alt+S) is the one
recent feature that is **not** header-only — it needs `.rc` dialog templates, so it is listed
in `CMakeLists.txt`. Dialog resource IDs live in `src/resource.h` (shared by `resource.rc` and
the dialog code): the dockable tree panel keeps dialog ID **1** (empty template, controls built
in code); `IDD_SETTINGS = 2` and `IDD_RULE = 3` are the modal conformance-rule editor and its
single-rule sub-dialog. The editor reads/writes the same `PipeHat.profile` `loadProfile()`
parses — on save it regenerates the rule lines from the grid (preserving the documented header)
and `cmdSettings` reloads the profile so Check Conformance updates without a restart. There are
12 menu items as of v1.3.0.

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
separate `SegmentParser`/`FieldParser`/`ComponentParser` classes, JSON segment tables, a
`VersionRegistry`, and an indicator/squiggle layer that **do not exist** in the code. The
shipped implementation is simpler and hand-coded (single `HL7Lexer`, compiled-in `SegmentDB`,
no validation squiggles, no version tables). Trust the source over docs 00–04; trust
`docs/05-CODE-REVIEW.md` for current known issues.
