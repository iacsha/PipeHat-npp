# Changelog

All notable changes to PipeHat. Versions follow [semantic versioning](https://semver.org/).

---

## [2.0.0] -- 2026-07-16

The MLLP release. PipeHat goes from a viewer to a live interface tester -- and, with it, opens
a socket for the first time. Also fixes a silent PHI leak that affected every prior version.

### ⚠️ Security -- read this if you have scrubbed PHI with an earlier build

**Segment IDs containing a digit were not recognized, so the PHI scrubber skipped those
segments entirely -- and still reported the scrub as clean.** `PV1`, `NK1`, `GT1`, `IN1`, `IN2`,
`PD1`, `DG1`, `PR1` and `PV2` were affected: next-of-kin names/addresses/phones, guarantor
name/DOB/**SSN**, insurance IDs, and attending/referring/admitting doctors all survived the
scrub. Nine of the scrubber's 24 mapped segments. `PID` is all-alpha and scrubbed correctly,
which is why the leak presented as a working scrubber.

The fail-closed coverage check did not catch it because it derived segment IDs from the same
parser call it was meant to audit, and skipped on the same empty result -- a safety net sharing
a dependency with the thing it audits is not a safety net.

**If you shared scrubbed output from an earlier build, treat it as not de-identified and
re-scrub it with 2.0.0.** Details in `docs/05-CODE-REVIEW.md` (C6).

### Added

- **MLLP send/receive over TCP** -- HL7's Minimal Lower Layer Protocol framing
  (`<VT>` … `<FS><CR>`). **Off by default; opens no sockets until you enable it.**
  - *Send Message (MLLP)* (`Ctrl+Alt+Shift+M`) -- sends the active message on a background
    thread and shows the returned ACK/NAK (MSA-1 + control ID).
  - *Toggle MLLP Listener* (`Ctrl+Alt+Shift+L`) -- MLLP server; inbound messages are
    auto-acknowledged (`AA`) and opened in a colored tab. Checkmark shows listening state.
  - Security posture: loopback-only bind unless you opt in **and** supply a bind address;
    cleartext-PHI confirmation on first use each session; extra confirmation for any
    non-loopback bind. **MLLP is cleartext -- there is no TLS (MLLP/S) yet.**
  - Saving inbound messages to disk is **opt-in and off by default**. When enabled, files go
    to `%LOCALAPPDATA%\PipeHat\received\` -- deliberately Local, not the roaming plugin-config
    folder, so cleartext PHI is not carried off-machine by roaming profiles or backup agents.
    *Clear Received Messages* purges them.
- **Settings GUI** (`Ctrl+Alt+Shift+P`) -- conformance rule editor plus the MLLP network
  section. Named/switchable per-interface profiles (`PipeHat.<name>.profile`) with a profile
  selector; `SegmentDB`-backed segment/field dropdowns.
- **Compare Views** (`Ctrl+Alt+Shift+D`) -- diffs Notepad++'s two split views, boxing differing
  fields in both panes. Replaces the old clipboard compare.
- **Copy Field Path** (`Ctrl+Alt+Shift+K`), **Copy as Rich Text** (`Ctrl+Alt+Shift+W`),
  **Add Conformance Rule from Field**, current-field highlight, event log (`PipeHat.log`,
  PHI-aware metadata only), and user-initiated **Check for Updates**.
- **`tests/SegmentIDTest.cpp`** -- standalone regression harness for segment-ID parsing and PHI
  scrub coverage. Links only `HL7Lexer.cpp` + `PHIScrubber.cpp`; exits non-zero on failure.

### Fixed

- **Silent PHI leak on segment IDs containing digits** (C6, above). `extractSegmentID` now uses
  explicit `A-Z`/`0-9` checks rather than locale-dependent `iswalpha`/`iswalnum`, requires the
  field separator to follow the ID, and special-cases `MSH` (MSH-1 *is* the separator, so `MSH`
  must be accepted before `parseMSH` can discover a non-`|` delimiter). `isSegmentStart` is now
  defined in terms of `extractSegmentID` so the tokenizer and the PHI lookup cannot disagree.
  The scrub coverage check derives its own segment ID and no longer calls into the lexer.
- **False activation on prose** -- any three uppercase characters parsed as a segment, so
  `THE QUICK BROWN FOX` was segment `THE` and `PIDX|…` was `PID`. Now rejected.
- **Syntax colors were lost on tab switch.** Root cause: `SCI_SETLEXER(SCLEX_CONTAINER)` was
  removed in Scintilla 5 (Notepad++ 8.4+) and is a silent no-op, and Notepad++ re-applies the
  buffer's theme on activation, resetting style *definitions*. PipeHat now health-checks styling
  on `SCN_PAINTED` and re-applies it when wiped, with a heal budget that replenishes on healthy
  paints.
- Message tree now closes when the HL7 file closes (`NPPN_FILECLOSED`).

### Changed

- **All hotkeys now include Shift** (`Ctrl+Alt+Shift+…`). Plain `Ctrl+Alt+<letter>` collides
  with Notepad++ defaults and gets grabbed by other software (graphics drivers, AltGr layouts).
- PHI fakes are **deterministic** -- seeded from a hash of the original value, so the same input
  yields the same fake and linkage is preserved across a message.
- Styling is **incremental** -- `SCN_MODIFIED` re-styles only the edited line range.

### Known limitations

- **No TLS (MLLP/S).** Messages cross the network in cleartext.
- Enhanced-mode ACK (MSH-15/16) is not honored -- always application ACK.
- The listener services one connection at a time.
- The message tree is field-level; components/subcomponents are not expanded.
- Segment and PHI tables are hand-curated, not generated from HAPI/nHapi metadata.

### Verification

Built clean (MSVC, Release, x64). `tests/SegmentIDTest.cpp` passes (and fails 27 assertions
against the pre-fix lexer, confirming it catches C6). MLLP protocol 20/20 and transport 12/12
standalone tests pass; send and receive verified **over loopback in Notepad++**. Not yet
verified against a third-party MLLP endpoint (e.g. Mirth Connect) -- see Known limitations.

---

## [1.2.0] -- 2026-07-14

### Added

- Escape-sequence decoding (`\F\ \S\ \T\ \R\ \E\ \.br\ \Xhh\`) on hover (`HL7Escape.h`).
- HL7 version awareness -- MSH-12 decoded to version name + era.
- Structural validation (`Validator.h`) with advisory squiggles -- never blocking.
- Message compare/diff, pretty-print (segments-per-line), and segment folding.
- Broader activation: MSH/FHS/BHS, BOM/blank-line skip, `.hl7` extension, manual enable.

---

## [1.1.0] -- 2026-07-14

### Added

- Trigger-event decoding -- MSH-9 / EVN-1 message type + event in tooltips and tree
  (`TriggerEventDB.h`).
- Conformance profiles -- per-interface `max`/`values`/`required` rules from an editable
  `PipeHat.profile`, with squiggles and a report.
- HIPAA Safe Harbor scrubber coverage: dates and provider segments; residual scan flags
  email + IPv4.
- Message tree follows the active buffer instead of auto-loading at startup.

---

## [1.0.0] -- 2026-07-14

Initial release, after an independent code review and hardening pass.

### Fixed

- Crash: `SCI_SETSTYLINGEX` -> `SCI_SETSTYLING`.
- Crash: unguarded `SCI_GETLINE` reads into fixed stack buffers -> length-safe helpers
  (`SciUtils.h`) sized from `SCI_LINELENGTH`.
- PHI: scrub now empties the undo buffer, so originals are not Ctrl+Z recoverable.
- PHI: escape sequences can no longer cross a field separator; MSH off-by-one corrected
  (MSH-1 *is* the field separator, so the first value is MSH-2).
- PHI: scrub fails closed -- skipped-field count plus a residual SSN/digit scan switch the
  completion dialog to a warning.

### Added

- Build hardening: `/guard:cf /sdl /GS /DYNAMICBASE /NXCOMPAT`.

[2.0.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v2.0.0
[1.2.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.2.0
[1.1.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.1.0
[1.0.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.0.0
