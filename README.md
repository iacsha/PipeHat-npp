# 🎩 PipeHat

**An HL7 v2.x message viewer for Notepad++.** PipeHat turns Notepad++ into a fluent
HL7 workbench: syntax highlighting, hover tooltips with field definitions, a dockable
message-tree panel, and a fail-closed PHI scrubber. Windows-only, x64, Unicode.

Named after the `|^~\&` "pipe-and-hat" encoding characters every HL7 interface engineer
knows by heart.

---

## Features

- **Syntax highlighting** -- segments, fields, components, and delimiters are colorized as
  you read, driven by a delimiter-aware HL7 tokenizer (reads the real separators from MSH).
- **Field tooltips** -- hover any field to see its segment/field name, data type, and
  required flag. Coded values are **decoded**: hovering `MSH-9` or `EVN-1` shows what the
  trigger event means (e.g. `ADT^A01` -> *Admit / Visit Notification*, `SIU^S12` -> *New
  Appointment Booking*).
- **Dockable message tree** -- a segment -> field outline of the message with the same
  trigger-event decoding inline; click a node to jump the editor to it. The panel follows
  the active message: it never auto-opens on startup and closes when you close the HL7 file.
- **PHI scrubber** -- de-identify a message for sharing/testing, with HIPAA Safe Harbor
  field coverage (names, IDs, dates, provider/scheduling segments, and more). Runs
  **fail-closed**: any field the parser can't account for is reported, and a residual scan
  (SSN, email, IP, long digit runs) warns you before you ever treat output as de-identified.
- **Conformance checking** -- validate a message against per-interface rules (max field
  length, allowed value sets, required fields) defined in an editable profile. Violations
  are squiggle-underlined and listed -- a pre-flight *"will the receiver accept this?"* check.
- **Structural validation** -- advisory malform detection: missing MSH, invalid segment IDs,
  empty required MSH fields, unterminated escape sequences. Never blocking.
- **Compare views** -- put one message in each of Notepad++'s two split views and run
  Compare Views (`Ctrl+Alt+Shift+D`); every differing field is highlighted **in place in both panes**,
  segment- and field-aware (ignores volatile MSH-7 datetime / MSH-10 control ID).
- **Pretty-print** -- put every segment on its own line (fixes single-line CR-delimited
  messages), and **folding** to collapse detail-segment groups (OBX/NTE under their parent).
- **Version & escape decoding** -- MSH-12 shows the HL7 version + era; escape sequences
  (`\F\ \S\ \T\ \Xhh\` …) are decoded on hover.

The plugin activates automatically on HL7 **content** (first non-blank segment is `MSH`,
`FHS`, or `BHS`) or a **`.hl7` file**. You can also force it on any buffer with
*Enable HL7 Highlighting* (`Ctrl+Alt+Shift+E`).

## Keyboard shortcuts

| Shortcut | Action | Shortcut | Action |
|----------|--------|----------|--------|
| `Ctrl+Alt+Shift+T` | Toggle message tree | `Ctrl+Alt+Shift+V` | Validate message |
| `Ctrl+Alt+Shift+H` | Scrub PHI | `Ctrl+Alt+Shift+D` | Compare the two views |
| `Ctrl+Alt+Shift+C` | Check conformance | `Ctrl+Alt+Shift+R` | Pretty-print / reformat |
| `Ctrl+Alt+Shift+G` | Toggle folding | `Ctrl+Alt+Shift+E` | Enable HL7 highlighting |
| `Ctrl+Alt+Shift+<-` / `->` | Previous / next field | `Ctrl+Alt+Shift+P` | Settings |
| `Ctrl+Alt+Shift+M` | Send message (MLLP) | `Ctrl+Alt+Shift+L` | Toggle MLLP listener |
| `Ctrl+Alt+Shift+K` | Copy field path | `Ctrl+Alt+Shift+W` | Copy as rich text |

All combos include **Shift** deliberately -- plain `Ctrl+Alt+letter` collides with
Notepad++ defaults and gets grabbed by other software (graphics drivers, AltGr
layouts) on some machines.

Any conflicts can be remapped in *Settings -> Shortcut Mapper -> Plugin commands*.

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

> The folder name and the DLL name **must both be `PipeHat`** -- the dockable panel
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

Verification is mostly manual -- open an HL7 sample in a debug Notepad++ build and exercise
the menu commands. The parser and PHI-coverage regressions that matter are pinned by a
standalone harness that links only `HL7Lexer.cpp` + `PHIScrubber.cpp` (no Windows deps) and
exits non-zero on failure:

```powershell
# from a Visual Studio developer prompt, at the repo root
cl /EHsc /std:c++17 /I src tests\SegmentIDTest.cpp src\HL7Lexer.cpp src\PHIScrubber.cpp /Fe:build\SegmentIDTest.exe
build\SegmentIDTest.exe
```

---

## Usage

- **Open** any HL7 v2.x file (or paste a message and ensure the first line starts with
  `MSH`) -- highlighting and tooltips activate automatically.
- **Plugins -> PipeHat** menu exposes the commands (message tree, PHI scrub, about).
- **Hover** a field for its definition; **click** a tree node to navigate to it.

### ⚠️ PHI scrubber -- read before you rely on it

The scrubber is a **best-effort de-identification aid, not a compliance guarantee.**

> ⚠️ **Fixed in v2.0.0 -- re-scrub anything scrubbed by an earlier build.** Segment IDs
> containing a digit (`PV1`, `NK1`, `GT1`, `IN1`, `IN2`, `PD1`, `DG1`, `PR1`, `PV2`) were not
> recognized, so those segments were skipped entirely -- next-of-kin details, guarantor
> **SSN**/DOB, insurance IDs and doctor names survived the scrub, **and the scrub still
> reported clean** because the coverage check shared the same blind spot. `PID` was
> unaffected, which is why the leak was not visible. If you shared output from an earlier
> build, treat it as **not de-identified**.

- It runs **fail-closed**: if any PHI-mapped field can't be processed, or a residual scan
  finds an identifier, the completion dialog switches to a **warning** -- *do not treat the
  output as de-identified* until you've reviewed it. The anonymize-mode coverage check
  derives segments independently of the parser, so a parser gap surfaces as a warning
  rather than a silent skip.
- Scrubbing **empties the undo buffer** on purpose, so originals are **not** recoverable
  via Ctrl+Z. Keep your own backup of the source message.
- The on-disk original and Notepad++'s `backup\` snapshots may still contain pre-scrub PHI.

Always review scrubbed output before sharing it outside a trusted boundary.

### Conformance profiles

`Check Conformance` (`Ctrl+Alt+Shift+C`) validates the active message against rules you define in
`PipeHat.profile`, created on first run in the Notepad++ plugin config folder
(`%AppData%\Notepad++\plugins\config`). Rules are per-interface -- the same field can carry
different limits at different endpoints. Format:

```
PID-8.values=M,F,O,U,A,N   # first component must be one of these
PID-5.max=48               # field must be <= 48 characters
MSH-9.required=true        # field must be present
```

Violating fields are squiggle-underlined in the editor and listed in a summary dialog.

You don't have to hand-edit the file: **Settings** (`Ctrl+Alt+Shift+P`) opens a rule editor --
a `segment / field / max / allowed values / required` grid with Add / Edit / Remove -- that
reads and writes `PipeHat.profile` and reloads it immediately so the next Check Conformance
uses your changes. The file format is unchanged, so hand-editing and the GUI interoperate.

### MLLP send / receive (network) -- off by default

> ⚠️ **MLLP is cleartext.** Messages cross the network **unencrypted** -- PHI included.
> Use it only over loopback or a trusted network. There is no TLS (MLLP/S) yet.

PipeHat can send the active message to an HL7 endpoint and receive inbound messages over
MLLP (HL7's framing for TCP). It ships **disabled** and opens no sockets until you turn it on
in **Settings -> MLLP**.

- **Send Message (MLLP)** (`Ctrl+Alt+Shift+M`) -- frames the active message, sends it to the configured
  host/port on a background thread, and shows the returned ACK/NAK (`MSA-1` + control id).
- **Toggle MLLP Listener** (`Ctrl+Alt+Shift+L`) -- starts/stops an MLLP server. Each inbound
  message is auto-acknowledged (`AA`) and opened in a tab (colored like any HL7 buffer). The menu
  item shows a checkmark while listening.

**Saving received messages is OFF by default.** Inbound messages open as in-memory tabs only --
no PHI touches disk. If you enable *Save received messages to disk* in **Settings -> MLLP**, each
message is written to `%LOCALAPPDATA%\PipeHat\received\` as `<type>_<controlId>_<time>.hl7`
(deliberately under `LOCALAPPDATA`, not the roaming plugin-config folder, so cleartext PHI can't
be carried off the machine by roaming profiles or backup agents). ⚠️ Those files are unencrypted
PHI -- use **Plugins -> PipeHat -> Clear Received Messages** to purge them, and keep the machine
itself encrypted (BitLocker/EFS) if you turn saving on. Note Notepad++'s own periodic-backup
feature, if enabled, may make additional copies of opened files in its `backup\` folder.

The listener **binds to loopback (`127.0.0.1`) only** unless you both tick *Allow binding a
non-loopback interface* and supply a bind address -- and even then a confirmation warns you that
you're exposing an HL7 receiver on your network. The first send or listen each session prompts
a cleartext-PHI confirmation. Network settings persist to `PipeHat.ini`.

---

## Documentation

- [`CLAUDE.md`](CLAUDE.md) -- architecture orientation and the invariants that matter when
  touching the parser or scrubber.
- [`AGENTS.md`](AGENTS.md) -- the regression-critical invariant subset for AI coding agents.
- [`docs/05-CODE-REVIEW.md`](docs/05-CODE-REVIEW.md) -- defect inventory and fix status.
- [`docs/06-ROADMAP.md`](docs/06-ROADMAP.md) -- what's shipped and what's planned
  (trigger-event decoding, HL7 version awareness, message compare/diff, conformance
  profiles, and more).

> Note: `docs/00`–`04` are the original design brief and are **aspirational** -- they
> describe classes and layers that don't exist in the shipped code. Trust the source and
> `docs/05`/`06` for current reality.

---

## Status

**v2.0.0.** Adds **MLLP send/receive** (HL7 over TCP) behind an off-by-default toggle -- the
plugin's first network feature -- and fixes a **silent PHI leak** where segments whose IDs
contain a digit (`PV1`, `NK1`, `GT1`, `IN1`…) were skipped by the scrubber while it still
reported clean (see the scrubber warning above and `docs/05-CODE-REVIEW.md` C6; pinned by
`tests/SegmentIDTest.cpp`). Crash-class defects and the fail-open PHI leak from the initial
review are fixed and build-verified. v1.1 added trigger-event decoding, HIPAA Safe Harbor scrubber coverage,
conformance checking, hotkeys, and smarter panel behavior; v1.2 adds structural validation,
message compare/diff, escape + HL7-version decoding, pretty-print, folding, and broader
activation; v1.3 adds the **Settings GUI** for editing conformance rules without hand-editing
the profile file. Each feature is verified by a standalone test. **MLLP send/receive** (network)
is integrated behind an off-by-default toggle and pending live-endpoint verification before a
v2.0 release. See the roadmap for what's next.

---

## License

[MIT](LICENSE) © 2026 Shawn Iachetta.
