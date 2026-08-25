---
task: "PipeHat field paths, Z segments, wrapped-segment alert"
project: PipeHat
effort: E3
effort_source: classifier
phase: verify
progress: 33/38
mode: interactive
started: 2026-08-25T13:12:03Z
updated: 2026-08-25T13:12:03Z
---

# PipeHat ISA

## Problem

Three defects in the parser core, all confirmed by a CLI harness linking the real `HL7Lexer.cpp`
(`build/repro/Repro.exe`):

1. **Hover tooltips stop at field depth.** `HL7Lexer::getFieldIndexAtPosition` counts field
   separators only. Hovering `ROCHESTER` inside `NK1|1|DOE^JANE^Q|SPO|123 MAIN ST^APT 4^ROCHESTER^NY^14624`
   returns field index `4`, so the tooltip reads `NK1-4`. The reader has to count carets by hand to
   learn they are on `NK1-4.3`, which is the whole reason they hovered.

2. **Four-character Z segments are invisible to the lexer.** `extractSegmentID` requires the field
   separator at offset 3, so `ZQRY|...` returns an empty ID. Empty ID means: no `SCE_HL7_SEGMENT_ID`
   token, so no bold blue header; no tree node; no PHI-scrub coverage; no conformance check. A whole
   segment silently drops out of every downstream consumer, and the only visible symptom is that it
   is not blue.

3. **A line break inside a segment is silent.** Copying a message out of a Teams chat wrapped a `PV1`
   across two lines. Nothing in PipeHat said so, no colour change and no marker, and the message went
   over MLLP in that state and broke the transmission. `cmdValidate` does report it, but only on
   demand and only as the misleading `Invalid segment ID 'ICU^101^A'`.

## Vision

Hovering any character in a message names exactly where you are, `NK1-4[2].3.1`, with the component's
name from its HL7 data type and not just the field's. Every segment a site actually sends, including
four-character Z segments, renders as a segment. And a message damaged by a chat-client paste
announces itself the moment it lands in the buffer: a coloured line you cannot miss, a one-keystroke
repair, and a refusal to put it on the wire. No more silent failure at the receiver.

## Out of Scope

Component-level PHI scrubbing, tree-view component nodes, and conformance rules addressed at
component depth are not part of this work. The path resolver is the prerequisite for all three, and
they are follow-on tasks. A complete HL7 component dictionary for every v2.x data type is not
included; only the composite types that actually carry the fields people hover are tabled. Automatic
repair of wrapped segments is not applied silently; the repair is always an explicit user action.
Segment IDs longer than four characters are not accepted, since no HL7 dialect uses them and
accepting them would reopen the prose-parsing hole. Batch and file envelope semantics, MLLP framing,
and the settings GUI are untouched.

## Principles

- **The parser is the substrate.** Styling, tree, scrub, conformance and validation are all views of
  one tokenization. A defect in the lexer is never a display bug; fixing it in a view is fixing a
  symptom.
- **Silence is the dangerous failure.** A segment that is merely not blue, or a message that merely
  fails at the far end, gives the operator nothing to act on. Damage must be visible where the
  operator is already looking.
- **Permissive on input, strict on claims.** Accept the dialects sites actually send; never report
  "clean" or "valid" about something that was skipped.

## Constraints

- Native C++17, MSVC, Windows-only, Unicode, x64. No new third-party dependencies.
- New pure-logic modules are header-only so `CMakeLists.txt` needs no edit. This is the established
  pattern for `TriggerEventDB.h`, `HL7Escape.h`, `Validator.h`, `MessageIndex.h`.
- `HL7Lexer::extractSegmentID` is the single derivation of "what segment is this line";
  `isSegmentStart` is already defined in terms of it and must stay that way. The one deliberate
  exception is `rawSegmentID` in `main.cpp`, the independent scrub-coverage safety net, which must
  remain independent and permissive.
- The prose-rejection guard is load-bearing: three uppercase characters not followed by the field
  separator must not parse as a segment, or the plugin auto-activates on ordinary text.
- `getFieldIndexAtPosition` keeps its exact current signature and semantics. The tree view, PHI
  scrub, conformance check and caret-field highlight all call it.
- Scintilla style indices are a contiguous small range; a new style takes the next free index in
  `PluginDefs.h`.

## Goal

`HL7Lexer` resolves a character position to a full HL7 path (field, repetition, component,
subcomponent) and a variable-length segment ID covering three-character IDs and four-character
Z segments; the tooltip renders that path with the component's name from its data type; and a
segment split across lines is styled as damaged, reported precisely by the validator, repairable
from the menu, and blocked from being sent over MLLP without confirmation.

## Criteria

### Field path resolution

- [x] ISC-1: `HL7Lexer.h` declares an `HL7FieldPath` struct with `field`, `repeat`, `component`, `subcomponent` members
- [x] ISC-2: `HL7Lexer::getPathAtPosition` is declared and defined
- [x] ISC-3: Hovering `ROCHESTER` in the NK1 fixture yields `field == 4`
- [x] ISC-4: The same probe yields `component == 3`
- [x] ISC-5: The same probe yields `subcomponent == 0` (no subcomponent separator present)
- [x] ISC-6: A position inside a field with no component separator yields `component == 0`
- [x] ISC-7: A position in the second repetition of a `~`-repeated field yields `repeat == 2`
- [x] ISC-8: A position inside a `&`-delimited subcomponent yields `subcomponent >= 1`
- [x] ISC-9: A position in the segment-ID region yields `field == 0`
- [x] ISC-10: On an MSH line, the first value after the encoding characters resolves to `field == 3`
- [x] ISC-11: `getFieldIndexAtPosition` returns `getPathAtPosition(...).field` for every fixture position tested
- [x] ISC-12: Anti: no existing caller of `getFieldIndexAtPosition` changes behaviour, `SegmentIDTest.exe` exits 0

### Path rendering in the tooltip

- [ ] ISC-13: A hover on `NK1-4` component 3 renders the literal string `NK1-4.3` in the calltip
- [ ] ISC-14: A hover on a field with no components renders `NK1-3`, with no trailing `.1`
- [ ] ISC-15: A hover in repetition 2 renders bracket notation `PID-3[2]`
- [ ] ISC-16: A hover on a subcomponent renders three-level notation `PID-3.4.1`
- [x] ISC-17: `HL7DataTypes.h` maps composite data types to ordered component names
- [x] ISC-18: The `XAD` table names component 3 `City`
- [ ] ISC-19: A hover on `NK1-4.3` renders the component name `City` alongside the field name
- [x] ISC-20: A data type with no component table renders the path without a component name and does not crash

### Variable-length segment IDs

- [x] ISC-21: `extractSegmentID` returns `ZQRY` for a line beginning `ZQRY|`
- [x] ISC-22: `extractSegmentID` returns `ZQRY` for a line that is exactly `ZQRY`
- [x] ISC-23: `extractSegmentID` still returns `ZPD` for `ZPD|...`
- [x] ISC-24: Anti: `extractSegmentID` returns empty for the prose line `ZONE OF INTEREST`
- [x] ISC-25: Anti: `extractSegmentID` returns empty for `THE QUICK BROWN FOX`
- [x] ISC-26: Anti: a four-character non-Z run followed by the field separator (`OBXA|`) returns empty
- [x] ISC-27: `tokenize` emits a `SEGMENT_ID` token of length 4 for `ZQRY|`
- [x] ISC-28: `getFieldIndexAtPosition` returns 1 for the first value on a `ZQRY|` line
- [x] ISC-29: `hl7val::validSegId` accepts `ZQRY` and still rejects `ICU^101^A`
- [x] ISC-30: `SegmentDB` lookup miss on a `Z`-prefixed ID renders "site-defined Z segment", not "Unknown segment"

### Wrapped-segment detection and alert

- [x] ISC-31: A `continuationLines` detector reports line 2 of a `PV1` wrapped across two lines
- [x] ISC-32: Anti: the detector reports nothing for a well-formed multi-segment message
- [x] ISC-33: Anti: the detector does not flag blank lines between messages
- [x] ISC-34: `PluginDefs.h` defines `SCE_HL7_CONTINUATION` at the next free style index
- [x] ISC-35: `ScintillaStyler::styleRange` assigns `SCE_HL7_CONTINUATION` to a continuation line's bytes
- [x] ISC-36: `hl7val::validate` emits a finding naming the wrapped segment, replacing `Invalid segment ID`
- [x] ISC-37: A `Join Wrapped Segments` menu command is registered in `getFuncsArray`
- [x] ISC-38: `cmdMllpSend` and `cmdMllpReplay` warn and require confirmation when continuation lines exist

## Test Strategy

| isc | type | check | threshold | tool |
|-----|------|-------|-----------|------|
| ISC-1..2 | static | declarations present in header | exact match | `grep` |
| ISC-3..11 | unit | path resolver against fixtures | exact integer equality | `tests/FieldPathTest.exe` |
| ISC-12 | regression | existing segment-ID and PHI harness | exit code 0 | `tests/SegmentIDTest.exe` |
| ISC-13..16 | unit | rendered path string | exact string equality | `tests/FieldPathTest.exe` |
| ISC-17..20 | unit | component-name lookup | exact string equality | `tests/FieldPathTest.exe` |
| ISC-21..29 | unit | segment-ID and tokenizer behaviour | exact equality | `tests/SegmentIDTest.exe` |
| ISC-30 | static | tooltip branch text | string present | `grep` |
| ISC-31..33 | unit | continuation detector over line vectors | exact line-number set | `tests/FieldPathTest.exe` |
| ISC-34 | static | style index defined, no collision | unique value | `grep` |
| ISC-35 | static | style assignment in styleRange | branch present | `grep` |
| ISC-36 | unit | validator finding text | substring match | `tests/FieldPathTest.exe` |
| ISC-37..38 | static | menu registration and send guard | branch present | `grep` |
| all | build | full plugin compiles | zero errors | `cmake --build build --config Release` |

## Features

| name | description | satisfies | depends_on | parallelizable |
|------|-------------|-----------|------------|----------------|
| path-resolver | `HL7FieldPath` and `getPathAtPosition` in `HL7Lexer`, `getFieldIndexAtPosition` delegates | ISC-1..12 | none | yes |
| datatype-components | `HL7DataTypes.h` component-name tables for common composite types | ISC-17..18, ISC-20 | none | yes |
| tooltip-render | Path and component-name formatting in `ScintillaStyler::showFieldTooltip` | ISC-13..16, ISC-19..20, ISC-30 | path-resolver, datatype-components | no |
| variable-segid | Variable-length segment IDs (3, or Z-prefixed 4) across lexer, validator, tokenizer | ISC-21..29 | none | yes |
| wrap-detect | Continuation-line detector plus validator finding | ISC-31..33, ISC-36 | variable-segid | no |
| wrap-alert | `SCE_HL7_CONTINUATION` style, join command, MLLP send guard | ISC-34..35, ISC-37..38 | wrap-detect | no |

## Decisions

- **2026-08-25T13:12:03Z** Both reported defects reproduced with a CLI harness linking the real
  `HL7Lexer.cpp` before any code was read for a fix. Evidence: `segId=""` for `ZQRY|1|abc`, and
  `field index 4` for a hover on the third component of `NK1-4`.
- **2026-08-25T13:12:03Z** Root-cause-at-ingestion: all three symptoms enter at the lexer, not at
  the display. Fixing the tooltip alone would leave the tree, scrub and conformance views blind to
  components; fixing the styler alone would leave `ZQRY` out of the tree and out of PHI coverage.
  Both fixes go in `HL7Lexer`.
- **2026-08-25T13:12:03Z** Four-character acceptance is restricted to `Z`-prefixed IDs. Accepting
  any four-character run followed by the field separator would widen the prose-parsing hole the
  existing delimiter guard was added to close.
- **2026-08-25T13:12:03Z** `getFieldIndexAtPosition` is kept and reimplemented as a thin wrapper
  over `getPathAtPosition` rather than replaced, so the four existing callers cannot silently drift
  from the new resolver. Same single-derivation rule that `isSegmentStart` already follows.
- **2026-08-25T14:05:00Z** Wrapped-segment detection has one derivation (`hl7val::continuationLines`),
  run per `MessageSpan` with that message's own delimiters. The styler, the hover warning, the
  validator, the join command and the two MLLP guards all reduce to it, so the editor cannot show
  damage the send path stays quiet about.
- **2026-08-25T14:05:00Z** The join deletes the line break and inserts nothing. The wrap lands
  mid-value, so inserting any character would corrupt the data the repair exists to rescue.
- **2026-08-25T14:05:00Z** The MLLP guard warns and defaults to No rather than repairing silently.
  Editing a message on its way to a live interface without being asked is worse than sending a
  known-bad one, and the plugin cannot tell a chat-client wrap from a deliberate test fixture.

## Verification

Run 2026-08-25, Release build, MSVC BuildTools 2022, x64.

**ISC-3..11, ISC-17..20, ISC-29, ISC-31..33, ISC-36**: `build\FieldPathTest.exe`:

```
45 passed, 0 failed
```

**ISC-12, ISC-21..28**: `build\SegmentIDTest.exe`, the pre-existing lexer and PHI-map
regression harness, unchanged by these edits:

```
ALL PASS (0 failures)
```

**All ISCs, build gate**: `cmake --build build --config Release`:

```
PipeHat.vcxproj -> C:\opencode\hl7-npp-plugin\build\Release\PipeHat.dll
```

Zero warnings introduced. Artifact written 2026-08-25 09:28, 543,232 bytes.

**ISC-1..2**: `HL7FieldPath` and `getPathAtPosition` declared in `src/HL7Lexer.h`, defined
in `src/HL7Lexer.cpp`; `getFieldIndexAtPosition` is now a one-line delegation to it.

**ISC-30**: `showFieldTooltip` carries a `segId[0] == L'Z'` branch rendering
`(site-defined Z segment)` where the `SegmentDB` lookup misses.

**ISC-34**: `SCE_HL7_CONTINUATION 11` in `src/PluginDefs.h`, one past the previous highest
style index. `SCI_STYLESETEOLFILLED 2057` had to be added to the stripped vendored
`include/npp/Scintilla.h`.

**ISC-35**: `ScintillaStyler::styleRange` styles the whole line as `SCE_HL7_CONTINUATION`
and skips tokenizing it when `extractSegmentID` is empty and `continuationOwner` is not.

**ISC-37**: `Join Wrapped Segments` registered at `Ctrl+Alt+Shift+J`; `g_funcItems` grown
from 25 to 26 (the array is sized exactly).

**ISC-38**: `confirmWrappedBeforeSend` is called by both `cmdMllpSend` and `cmdMllpReplay`
before any bytes leave; it defaults to No and names the offending line numbers.

### Open

**ISC-13..16 and ISC-19** are implemented but not yet observed in a live calltip. The
formatting sits in `ScintillaStyler.cpp`, which cannot be linked without the Scintilla host,
so the standalone harness verifies the resolver and the component tables that feed it but not
the rendered string. Closing them needs one pass in a Notepad++ session with the built DLL
deployed: hover the NK1 fixture city (expect `NK1-4.3: Address (XAD) City`), the NK1-3
field (expect `NK1-3`, no trailing `.1`), and the second PID-3 repetition (expect
`PID-3[2].1`). Same session closes the visual check on the pink continuation wash.
