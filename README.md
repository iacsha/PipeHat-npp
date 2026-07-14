# 🎩 PipeHat

**An HL7 v2.x message viewer for Notepad++.** PipeHat turns Notepad++ into a fluent
HL7 workbench: syntax highlighting, hover tooltips with field definitions, a dockable
message-tree panel, and a fail-closed PHI scrubber. Windows-only, x64, Unicode.

Named after the `|^~\&` "pipe-and-hat" encoding characters every HL7 interface engineer
knows by heart.

---

## Features

- **Syntax highlighting** — segments, fields, components, and delimiters are colorized as
  you read, driven by a delimiter-aware HL7 tokenizer (reads the real separators from MSH).
- **Field tooltips** — hover any field to see its segment/field name, data type, and
  required flag, from a built-in segment definition table.
- **Dockable message tree** — a segment → field outline of the message; click a node to
  jump the editor to it.
- **PHI scrubber** — de-identify a message for sharing/testing. Runs **fail-closed**: any
  field the parser can't account for is reported, and a residual-identifier scan warns you
  before you ever treat output as de-identified.

The plugin activates automatically when a buffer's **first line starts with `MSH`**.

---

## Requirements

- Windows x64
- Notepad++ (x64)
- To build from source: CMake 3.15+ and Visual Studio 2019/2022 (MSVC, C++17)

---

## Install (from a release build)

1. Locate your Notepad++ `plugins` directory (typically
   `%ProgramFiles%\Notepad++\plugins` or, for a portable install, `plugins\` next to
   `notepad++.exe`).
2. Create a folder named **`PipeHat`** inside it.
3. Copy **`PipeHat.dll`** into that folder, so the path is
   `…\Notepad++\plugins\PipeHat\PipeHat.dll`.
4. Restart Notepad++.

> The folder name and the DLL name **must both be `PipeHat`** — the dockable panel
> registers under that module name and won't appear if they differ.

Verify the install: open an HL7 message (a file whose first line begins with `MSH`). You
should see colorized segments and a **PipeHat** entry under *Plugins* in the menu bar.

---

## Build from source

Out-of-source build with CMake + MSVC:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
# output: build/Release/PipeHat.dll
```

Then follow the install steps above with the freshly built DLL.

There is no automated test suite in-repo; verification is manual — open an HL7 sample in a
debug Notepad++ build and exercise the menu commands. (A standalone lexer harness is used
during development to verify the tokenizer.)

---

## Usage

- **Open** any HL7 v2.x file (or paste a message and ensure the first line starts with
  `MSH`) — highlighting and tooltips activate automatically.
- **Plugins → PipeHat** menu exposes the commands (message tree, PHI scrub, about).
- **Hover** a field for its definition; **click** a tree node to navigate to it.

### ⚠️ PHI scrubber — read before you rely on it

The scrubber is a **best-effort de-identification aid, not a compliance guarantee.**

- It runs **fail-closed**: if any PHI-mapped field can't be processed, or a residual scan
  finds an identifier, the completion dialog switches to a **warning** — *do not treat the
  output as de-identified* until you've reviewed it.
- Scrubbing **empties the undo buffer** on purpose, so originals are **not** recoverable
  via Ctrl+Z. Keep your own backup of the source message.
- The on-disk original and Notepad++'s `backup\` snapshots may still contain pre-scrub PHI.

Always review scrubbed output before sharing it outside a trusted boundary.

---

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — architecture orientation and the invariants that matter when
  touching the parser or scrubber.
- [`AGENTS.md`](AGENTS.md) — the regression-critical invariant subset for AI coding agents.
- [`docs/05-CODE-REVIEW.md`](docs/05-CODE-REVIEW.md) — defect inventory and fix status.
- [`docs/06-ROADMAP.md`](docs/06-ROADMAP.md) — what's shipped and what's planned
  (trigger-event decoding, HL7 version awareness, message compare/diff, conformance
  profiles, and more).

> Note: `docs/00`–`04` are the original design brief and are **aspirational** — they
> describe classes and layers that don't exist in the shipped code. Trust the source and
> `docs/05`/`06` for current reality.

---

## Status

First working version. Crash-class defects and the fail-open PHI leak found in the initial
review are fixed and build-verified. See the roadmap for what's next.
