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
  required flag. Coded values are **decoded**: hovering `MSH-9` or `EVN-1` shows what the
  trigger event means (e.g. `ADT^A01` → *Admit / Visit Notification*, `SIU^S12` → *New
  Appointment Booking*).
- **Dockable message tree** — a segment → field outline of the message with the same
  trigger-event decoding inline; click a node to jump the editor to it. The panel follows
  the active message: it never auto-opens on startup and closes when you close the HL7 file.
- **PHI scrubber** — de-identify a message for sharing/testing, with HIPAA Safe Harbor
  field coverage (names, IDs, dates, provider/scheduling segments, and more). Runs
  **fail-closed**: any field the parser can't account for is reported, and a residual scan
  (SSN, email, IP, long digit runs) warns you before you ever treat output as de-identified.
- **Conformance checking** — validate a message against per-interface rules (max field
  length, allowed value sets, required fields) defined in an editable profile. Violations
  are squiggle-underlined and listed — a pre-flight *"will the receiver accept this?"* check.
- **Structural validation** — advisory malform detection: missing MSH, invalid segment IDs,
  empty required MSH fields, unterminated escape sequences. Never blocking.
- **Compare / diff** — segment- and field-aware diff of the current message against the
  clipboard, opened in a new tab (ignores volatile MSH-7 datetime / MSH-10 control ID).
- **Pretty-print** — put every segment on its own line (fixes single-line CR-delimited
  messages), and **folding** to collapse detail-segment groups (OBX/NTE under their parent).
- **Version & escape decoding** — MSH-12 shows the HL7 version + era; escape sequences
  (`\F\ \S\ \T\ \Xhh\` …) are decoded on hover.

The plugin activates automatically on HL7 **content** (first non-blank segment is `MSH`,
`FHS`, or `BHS`) or a **`.hl7` file**. You can also force it on any buffer with
*Enable HL7 Highlighting* (`Ctrl+Alt+E`).

## Keyboard shortcuts

| Shortcut | Action | Shortcut | Action |
|----------|--------|----------|--------|
| `Ctrl+Alt+T` | Toggle message tree | `Ctrl+Alt+V` | Validate message |
| `Ctrl+Alt+H` | Scrub PHI | `Ctrl+Alt+D` | Compare with clipboard |
| `Ctrl+Alt+C` | Check conformance | `Ctrl+Alt+R` | Pretty-print / reformat |
| `Ctrl+Alt+F` | Toggle folding | `Ctrl+Alt+E` | Enable HL7 highlighting |
| `Ctrl+Alt+←` / `→` | Previous / next field | | |

Any conflicts can be remapped in *Settings → Shortcut Mapper → Plugin commands*.

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

### Conformance profiles

`Check Conformance` (`Ctrl+Alt+C`) validates the active message against rules you define in
`PipeHat.profile`, created on first run in the Notepad++ plugin config folder
(`%AppData%\Notepad++\plugins\config`). Rules are per-interface — the same field can carry
different limits at different endpoints. Format:

```
PID-8.values=M,F,O,U,A,N   # first component must be one of these
PID-5.max=48               # field must be <= 48 characters
MSH-9.required=true        # field must be present
```

Violating fields are squiggle-underlined in the editor and listed in a summary dialog.

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

**v1.2.0.** Crash-class defects and the fail-open PHI leak from the initial review are fixed
and build-verified. v1.1 added trigger-event decoding, HIPAA Safe Harbor scrubber coverage,
conformance checking, hotkeys, and smarter panel behavior; v1.2 adds structural validation,
message compare/diff, escape + HL7-version decoding, pretty-print, folding, and broader
activation. Each feature is verified by a standalone test. See the roadmap for what's next.

---

## License

[MIT](LICENSE) © 2026 Shawn Iachetta.
