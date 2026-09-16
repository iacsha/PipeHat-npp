---
task: "PipeHat endpoint profiles -- the profile owns the connection"
project: PipeHat
effort: E3
effort_source: classifier
phase: verify
progress: 83/120
mode: interactive
started: 2026-08-25T13:12:03Z
updated: 2026-09-15T00:00:00Z
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

### Endpoint profiles -- the profile owns the connection

- [x] ISC-39: `src/EndpointProfile.h` exists and builds standalone with no Windows headers
- [x] ISC-40: A profile file with no section headers parses entirely as rules (backward compatibility)
- [x] ISC-41: `[Profile]` parses application, engine, messageType, displayName, description, inherits
- [x] ISC-42: `environment` accepts `qa`, `Dev`, `PROD` case-insensitively
- [x] ISC-43: Anti: `environment = Development` is NOT read as `DEV`; it warns and stays Unspecified
- [x] ISC-44: `environmentRank` orders Local < DEV < QA < PROD, Unspecified last
- [x] ISC-45: `[Connection]` parses host, sendPort, listenPort, bindAddr, allowNonLoopback
- [x] ISC-46: A non-numeric or out-of-range port falls back to the default, never to a wild value
- [x] ISC-47: An unparseable `allowNonLoopback` fails closed
- [x] ISC-48: Anti: `enabled` in a `[Connection]` section is refused and warned about
- [x] ISC-49: Anti: `saveReceived` in a `[Connection]` section is refused and warned about
- [x] ISC-50: Anti: a rule line inside an unrecognised section does NOT become active
- [x] ISC-51: `merge` concatenates parent rules before child rules so the child's attribute wins
- [x] ISC-52: `merge` inherits a facet only where the child left it blank
- [x] ISC-53: Anti: `merge` never inherits `allowNonLoopback` from the parent
- [x] ISC-54: Anti: a child with no `[Connection]` resolves to loopback, not to its parent's address
- [x] ISC-55: Anti: `merge` does not inherit the parent's `displayName`
- [x] ISC-56: `resolve` follows a two-level chain and clears `inherits` once resolved
- [x] ISC-57: `resolve` detects an inheritance cycle and still returns a usable profile
- [x] ISC-58: `resolve` caps chain depth at `kMaxInheritDepth` and reports it
- [x] ISC-59: `resolve` reports a missing parent and still loads the child
- [x] ISC-60: `resolve` on a missing profile returns loopback defaults, never invented values
- [x] ISC-61: `displayName` derives `Meditech DFT > IRIS (QA)` from facets
- [x] ISC-62: An explicit `displayName` wins verbatim over the derivation
- [x] ISC-63: With no facets the slug is the display name, and an empty slug reads `Default`
- [x] ISC-64: Anti: `requiresExtraConfirm` returns true only for PROD and never suppresses a confirm
- [x] ISC-65: Anti: no function in `EndpointProfile.h` lets `environment` skip a confirmation
- [x] ISC-66: `serialize` writes a hand-typed `qa` back as canonical `QA`
- [x] ISC-67: `serialize` round-trips every facet and connection field with no warnings
- [x] ISC-68: Anti: a profile with no connection does not gain an empty `[Connection]` on save
- [ ] ISC-69: `SettingsDialog` saves facets and connection alongside rules without dropping sections
- [ ] ISC-70: The settings dialog shows the derived display name live as facets are edited
- [ ] ISC-71: The inherits dropdown excludes the current profile, so self-inheritance needs hand-editing
- [ ] ISC-72: Anti: switching the active profile stops a running MLLP listener
- [ ] ISC-73: The cleartext-PHI confirmation is cached per profile slug per session, not globally
- [ ] ISC-74: A PROD profile asks one extra confirmation before send or listen
- [ ] ISC-75: `Switch Endpoint Profile` appears in the menu and groups entries by application/engine/type
- [ ] ISC-76: Migration writes the old global `[MLLP]` values into a profile that has no `[Connection]`
- [ ] ISC-77: Anti: migration does not re-run against a profile that already has a `[Connection]`
- [x] ISC-78: The full plugin compiles with zero errors on MSVC
- [x] ISC-79: Anti: the migration runs at most once per session, never on a profile switch
- [ ] ISC-80: Anti: switching to a connection-less profile does not inherit the outgoing host/ports
- [ ] ISC-81: Editing the active profile's listenPort or bindAddr in Settings stops the listener
- [ ] ISC-82: Anti: browsing the profile dropdown does not write a `[Connection]` into a visited file
- [ ] ISC-83: The derived-name preview matches the picker label for a profile that inherits
- [x] ISC-84: Anti: no control in IDD_SETTINGS overlaps another or falls outside its groupbox

### Tree depth below the field (LinkedIn request)

- [x] ISC-85: `src/FieldTree.h` exists and builds standalone with no Windows headers
- [x] ISC-86: A field with three repetitions produces three `[n]` nodes
- [x] ISC-87: Anti: a field with one value emits NO `[1]` node
- [x] ISC-88: Components are named from the field's data type via `hl7dt::componentName`
- [x] ISC-89: An untabled or empty data type renders the numeric path alone, never a guess
- [x] ISC-90: Anti: an empty middle component keeps its position and does not renumber the rest
- [x] ISC-91: Anti: a trailing empty component is not dropped
- [x] ISC-92: Subcomponents render as `.n.m`
- [x] ISC-93: A lone component carrying subcomponents still expands as component 1
- [x] ISC-94: Anti: an escaped `\S\` or `\R\` does not create a component or repetition
- [x] ISC-95: Splitting honours the message's own delimiters, not the defaults
- [x] ISC-96: Anti: one runaway field is capped at `kMaxSiblings` with a summary node
- [x] ISC-97: A long value is clipped in the label while the node keeps the full value
- [x] ISC-98: Anti: no component value contains a separator turned into a space
- [ ] ISC-99: `MessageTreeView::refresh` slices raw field text between FIELD_SEP tokens
- [ ] ISC-100: Anti: MSH-2 is listed as a field but never split into components
- [ ] ISC-101: Anti: the last field on a line carries no trailing CR into its label
- [ ] ISC-102: Anti: clicking the last field of a segment navigates to that segment's line
- [ ] ISC-103: Anti: a 480-message batch stops expanding at `valueNodeBudget` rather than stalling

### Settings split, delete, wizard

- [x] ISC-104: Settings is two windows: Profile Settings and Plug-in Settings
- [x] ISC-105: Anti: the profile window contains no control that writes a global switch
- [ ] ISC-106: Anti: changing the profile dropdown writes nothing to disk
- [ ] ISC-107: Leaving a profile with unsaved edits prompts Save / Discard / Cancel
- [ ] ISC-108: Cancel on that prompt restores the combo to the profile actually loaded
- [ ] ISC-109: Cancel on the dialog with unsaved edits asks before discarding
- [ ] ISC-110: Delete names the profile and requires confirmation
- [ ] ISC-111: Anti: Delete refuses to remove the default profile
- [ ] ISC-112: Delete warns and lists profiles that inherit from the target
- [ ] ISC-113: Deleting the active profile falls back to the default
- [ ] ISC-114: New runs a seven-step wizard and writes nothing until the last step
- [ ] ISC-115: Anti: the wizard rejects a profile name containing a dot
- [ ] ISC-116: Anti: the wizard rejects a name that already exists
- [ ] ISC-117: The profile window states whether the global non-loopback permission is on
- [ ] ISC-118: The Plug-in Settings button opens that window and the state line refreshes
- [ ] ISC-119: Clearing the global permission stops a running listener
- [x] ISC-120: Tooltips explain application, engine, environment, inherits and the bind opt-in

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
| ISC-39..68 | unit | endpoint profile parse/merge/resolve/serialize | 104 assertions, exit 0 | `tests/EndpointProfileTest.exe` |
| ISC-69..71 | manual | settings dialog round-trip on a real profile file | sections survive a save | Notepad++ debug session |
| ISC-72..74 | manual | switch with listener up, PROD send, repeat switch | listener stops, confirms fire | Notepad++ debug session |
| ISC-75 | static | menu registration present | branch present | `grep` |
| ISC-79 | static | session `static bool` gate present in the migration | branch present | `grep` |
| ISC-80..83 | manual | switch to a legacy profile, edit port in place, browse the dropdown | file unchanged, listener stopped, label matches | Notepad++ debug session |
| ISC-84 | static | rectangle sweep over every IDD_SETTINGS control | zero overlaps, zero out of bounds | layout checker script |
| ISC-85..98 | unit | field splitting, naming, caps, escapes | 49 assertions, exit 0 | `tests/FieldTreeTest.exe` |
| ISC-99..103 | manual | open a PID with repeats, an MSH, and a 480-message batch | subtree correct, no stall | Notepad++ debug session |
| ISC-104..105 | static | control inventory per dialog template | no global control in IDD_SETTINGS | `rg` over resource.rc |
| ISC-106..119 | manual | browse, edit, switch, cancel, delete, create | prompts fire, nothing written early | Notepad++ debug session |
| ISC-120 | manual | hover each endpoint field | tooltip appears and wraps | Notepad++ debug session |
| ISC-76..77 | manual | upgrade over an existing PipeHat.ini, then restart twice | written once, not twice | Notepad++ debug session |
| all | build | full plugin compiles | zero errors | `cmake --build build --config Release` |
| all | build | both standalone tests under MSVC | exit 0, /W4 clean | `cmd /c tests\runtests.bat` |

## Features

| name | description | satisfies | depends_on | parallelizable |
|------|-------------|-----------|------------|----------------|
| path-resolver | `HL7FieldPath` and `getPathAtPosition` in `HL7Lexer`, `getFieldIndexAtPosition` delegates | ISC-1..12 | none | yes |
| datatype-components | `HL7DataTypes.h` component-name tables for common composite types | ISC-17..18, ISC-20 | none | yes |
| tooltip-render | Path and component-name formatting in `ScintillaStyler::showFieldTooltip` | ISC-13..16, ISC-19..20, ISC-30 | path-resolver, datatype-components | no |
| variable-segid | Variable-length segment IDs (3, or Z-prefixed 4) across lexer, validator, tokenizer | ISC-21..29 | none | yes |
| wrap-detect | Continuation-line detector plus validator finding | ISC-31..33, ISC-36 | variable-segid | no |
| wrap-alert | `SCE_HL7_CONTINUATION` style, join command, MLLP send guard | ISC-34..35, ISC-37..38 | wrap-detect | no |
| endpoint-core | `EndpointProfile.h`: sections, facets, environment enum, merge, resolve, serialize | ISC-39..68 | none | yes |
| endpoint-dialog | Settings dialog endpoint section, derived-name preview, inherits picker | ISC-69..71 | endpoint-core | no |
| endpoint-glue | `main.cpp` load/resolve/migrate, per-profile confirm, switch command | ISC-72..78 | endpoint-core | no |

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

- **2026-09-15T00:00:00Z** Connection settings move into the profile; `enabled` and `saveReceived`
  deliberately stay global in `PipeHat.ini`. Both are switches a profile change would otherwise flip
  as a side effect of a menu click -- one starts networking, the other starts writing cleartext PHI
  to disk. `endpoint::parse` refuses them inside a `[Connection]` section rather than ignoring them,
  so a hand-edited file says why it did not work.
- **2026-09-15T00:00:00Z** `environment` is a label, not a gate. Deconstructing what actually decides
  whether a bind is safe gives `allowNonLoopback` plus `bindAddr`; the environment string has no
  causal relationship to the socket. It may therefore only ADD friction. There is no
  `requiresLessConfirmation()` and there must never be one, or a mistyped label becomes a bypass.
- **2026-09-15T00:00:00Z** The cleartext-PHI acknowledgement is keyed per profile slug per session.
  A single session-wide bool stops describing the endpoint once switching is cheap; re-firing on
  every switch trains the user to click through it. Per profile per session is the only version that
  still carries information when the profile count grows.
- **2026-09-15T00:00:00Z** A `[Connection]` is never inherited. Inheriting one would mean adding
  `inherits = <parent with allowNonLoopback>` silently widens where a child binds, which makes
  inheritance a privilege path. Rules inherit; the address does not.
- **2026-09-15T00:00:00Z** Migration of the old global `[MLLP]` block is made idempotent by
  construction -- it only writes a profile with no `[Connection]`, and writing one removes that
  condition. An ini flag guarding it would re-run and overwrite a hand-edited profile the first time
  the flag was lost.
- **2026-09-15T00:00:00Z** `refined:` the display name derives from facets rather than from the
  filename, so renaming costs no file operation and two profiles may share a label. The slug stays
  the key.
- **2026-09-15T00:00:00Z** Show your math on the E3 delegation floor (2, soft; 1 used): Forge was
  invoked to read the Win32 changes that cannot be compiled on this host. A second Claude-family
  reviewer was not spawned because `codex` is absent here, so Forge already degrades to the same
  model family -- a second one returns correlated opinions rather than independent ones. The real
  independent check is the MSVC build on the work PC, tracked as ISC-78.

- **2026-09-15T00:00:00Z** `refuted:` the migration was believed safe because it only writes a
  profile with no `[Connection]`. That is true but insufficient. `loadProfile` also runs on every
  switch, and there `g_mllp` holds the OUTGOING profile's connection, so the guard let the previous
  endpoint's host and ports be written permanently into any connection-less profile -- and because
  `requiresExtraConfirm` reads the NEW profile's environment, a switch from PROD to a legacy profile
  would keep the production address while dropping the production prompt. Now gated by a
  session-scoped `static bool` so it happens at startup only. The call was not deleted: the startup
  load is the entire upgrade path for a user with `AllowNonLoopback=1` in the ini.
- **2026-09-15T00:00:00Z** `refined:` stopping the listener on a profile-NAME change is not enough.
  An in-place edit of the active profile's `listenPort`, or clearing the global opt-in, moves the
  socket without renaming anything. The comparison is now on `listenPort` and `effectiveBindAddr`
  taken across the dialog and evaluated after `loadProfile`, so it sees the final ANDed values.
- **2026-09-15T00:00:00Z** `refined:` `readEndpoint` no longer sets `hasConnection` unconditionally.
  Browsing the profile dropdown calls it on every combo change, which silently rewrote every file
  the user merely looked at. A `g_connDirty` flag, suppressed while `populateEndpoint` runs because
  `SetDlgItemTextW` fires `EN_CHANGE` exactly like a keystroke, marks a real edit.

- **2026-09-15T00:00:00Z** Tree depth below the field came from a LinkedIn comment asking for a
  repetition sub-tree "like hl7inspector.com". Built as components and subcomponents too, not
  repetitions alone: hl7inspector shows the full depth, and a repetition level whose children are
  still flat joined text would answer the question halfway.
- **2026-09-15T00:00:00Z** `refuted:` the field text shown in the tree was believed to be the field.
  It was the lexer's FIELD_VALUE tokens joined with spaces, so every separator was discarded and
  `DOE^JANE^Q` reached the panel as `DOE JANE Q`. `refresh` now slices the raw line between
  FIELD_SEP tokens. `criterion_now:` ISC-98 asserts no component value contains a
  separator-turned-space.
- **2026-09-15T00:00:00Z** Found while rewriting the loop: the end-of-line field emit passed
  `(LPARAM)(fieldIdx)` where every other node passes `line + 1`, so clicking the last field of a
  segment navigated to whichever line shared that number. The two emit paths are now one lambda so
  they cannot drift again.

- **2026-09-16T00:00:00Z** The build box is `dell`, not the work PC. The work PC has no C++
  toolchain at all -- no Visual Studio, no vswhere, no cmake -- so it can run PipeHat but cannot
  produce it. `dell` carries VS 2022 BuildTools, cmake on PATH, and the original
  `C:\opencode\hl7-npp-plugin` tree. `tests/runtests.bat` was added so the MSVC half of the test
  run is one command rather than a PowerShell quoting exercise.

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

### Release v2.3.0

Tagged `v2.3.0` (`de18554`) 2026-08-25. Tag-triggered workflow run 32873658912 completed
success: full Release build, all six standalone harnesses green, `PipeHat-v2.3.0-x64.zip`
(256,748 bytes) attached to the GitHub release.

Asset verified by extraction rather than by exit code. The zip contains
`PipeHat/PipeHat.dll` (540,160 bytes) plus README, CHANGELOG and LICENSE, laid out so it
unpacks straight into the Notepad++ plugins folder. The DLL is PE machine `0x8664` (x64),
exports all six Notepad++ ABI entry points (`setInfo`, `getName`, `getFuncsArray`,
`beNotified`, `messageProc`, `isUnicode`), and carries the UTF-16 literals
`Join Wrapped Segments`, `Line break inside segment` and `2.3.0`, so the release build is
the feature build and not a stale artifact.

Gap noted, not fixed: the DLL has no `VERSIONINFO` resource, so Windows file properties show
an empty File version. The plugin reports its own version from `HL7_PLUGIN_VERSION`, so
nothing depends on it, but the file looks unversioned to anything that inspects it from
outside.

### Open

**ISC-13..16 and ISC-19** are implemented but not yet observed in a live calltip. The
formatting sits in `ScintillaStyler.cpp`, which cannot be linked without the Scintilla host,
so the standalone harness verifies the resolver and the component tables that feed it but not
the rendered string. Closing them needs one pass in a Notepad++ session with the built DLL
deployed: hover the NK1 fixture city (expect `NK1-4.3: Address (XAD) City`), the NK1-3
field (expect `NK1-3`, no trailing `.1`), and the second PID-3 repetition (expect
`PID-3[2].1`). Same session closes the visual check on the pink continuation wash.
