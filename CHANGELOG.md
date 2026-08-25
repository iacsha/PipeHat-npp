# Changelog

All notable changes to PipeHat. Versions follow [semantic versioning](https://semver.org/).

---

## [2.3.0] -- 2026-08-25

### Added

- **Hover tooltips now resolve the full field path, not just the field.** Hovering
  the city in `NK1-4` reports `NK1-4.3`, with the repetition in brackets when the
  field repeats (`PID-3[2].1`) and the subcomponent when there is one
  (`PID-3.4.2`). Where the field's data type is a known composite, the component's
  name is shown beside it (`XAD.3` renders as `City`), from a new header-only
  `HL7DataTypes.h` covering 26 composite types.

  The resolution happens in the lexer, in a new `HL7Lexer::getPathAtPosition`, not
  in the tooltip. `getFieldIndexAtPosition` is now a one-line wrapper over it, so
  the tree, the PHI scrubber, the conformance check and the caret-field highlight
  cannot drift from what the tooltip says. A field with no component separator
  reports no component rather than a misleading `.1`.

- **Four-character `Z` segments are recognized as segments.** `ZQRY` was being
  treated as message text: no bold blue header, no tree node, and no PHI-scrub
  coverage on that line. Widened in `extractSegmentID` and `validSegId`, and
  deliberately only for a `Z` prefix, so the prose-rejection guard that keeps
  `THE QUICK BROWN FOX` and `OBXA|1` from being read as segments stays intact.

- **Mid-segment line breaks are detected, flagged and repairable.** Pasting a
  message out of a chat client wraps long segments; because a line break *is* the
  segment terminator on the wire, the tail of a wrapped `PV1` is sent as its own
  bogus segment and the receiver rejects or mis-parses the message.

  Three surfaces: continuation lines are styled with a pink EOL-filled wash so the
  damage is visible without running anything; hovering one says which segment it
  belongs to; **Join Wrapped Segments** (`Ctrl+Alt+Shift+J`) deletes the breaks
  under a single undo action. The join inserts nothing in place of the break, as
  the wrap landed mid-value and any inserted character would corrupt the data it
  is meant to rescue. **Send Message (MLLP)** and **Replay All Messages (MLLP)**
  now warn before transmitting a document that contains one, defaulting to "no".

  The validator names the wrapped segment (`Line break inside segment PV1`) rather
  than reporting the tail as an invalid segment ID, which was the misleading
  message it produced before. Detection runs per message with that message's own
  delimiters, so a `!`-delimited message is not reported as entirely wrapped.

  **26 menu items** total.

- **Check for Updates now covers provider packages, not just the plugin.** A
  `.provider` may declare `version`, `updatecheck` (`owner/repo`) and an optional
  `updateurl`; one menu action reports the plugin and every package that declared
  both a version and a repo, in a single dialog. Packages that declare neither are
  not checked and say so, so a quiet report is distinguishable from a failed one.

  **It reports and opens a page. It does not download or install, and that is
  deliberate.** A `.provider` names a *command PipeHat executes*, so an updater
  that fetched and activated one would let a remote artifact decide what runs on
  a workstation with PHI access. Reading a version string cannot execute
  anything; fetching a payload and activating it can. Downloading is a different
  feature with a different risk class and it needs pinned hashes, a staging
  folder that is never the live one, and a user-performed install step before it
  is defensible. Do not "finish" this by adding a downloader.

  Mechanically it reuses what was already there: `fetchLatestTag` unchanged,
  `isNewerVersion` unchanged, still user-initiated, still off the UI thread, and
  the provider list is snapshotted on the UI thread so the worker never touches
  `g_providers` while `loadProviders()` might run. A malformed `updatecheck` (no
  slash, or containing spaces) is rejected at parse rather than carried into a
  lookup or handed to `ShellExecute` as if it were a link. At most five pages
  open at once.

  17 assertions added to `tests/TransformProviderTest.cpp` section [7], covering
  the half-configured cases that gate the network call: a version with nowhere to
  look, and a repo with nothing to compare against. Both stay silent.

## [2.2.0] -- 2026-08-07

Everything below was found or hardened by running the plugin against real
Notepad++ installs on two machines, one of them a locked-down work PC. Three of
the four defects fixed here were invisible from the development box.

### Fixed

- **Transform results were silently discarded when the marshaling window was missing.**
  Every async result in the plugin -- transform, MLLP send/replay, update check -- is
  posted to a hidden `HWND_MESSAGE` window created once at `NPPN_READY`. If that creation
  failed, `g_hMllpWnd` stayed null and each worker hit `if (target) ... else delete o;`,
  throwing the outcome away. The symptom is the worst kind: the keypress does *nothing* --
  no output, no error dialog, and a log containing `invoked` with no matching `ok` /
  `exit N` / `timed out`. Observed live on 2026-08-07; only confirmable by enumerating the
  running process's message-only windows.
  - All four async sites now go through `marshalWindow()`, which retries the (idempotent)
    creation instead of trusting a one-shot at startup.
  - Creation failure now logs a `WND` entry with the `RegisterClass` / `CreateWindowEx`
    error codes, so the root cause is visible next time instead of invisible.
  - `runProvider` refuses with an explicit dialog rather than starting a transform whose
    result has nowhere to go. The `else delete o;` silent-drop branch is gone.

### Changed

- **Transforming a non-HL7 tab now says so.** Previously the buffer was handed to the
  provider regardless, so the user got the *engine's* parser error back -- a stack trace
  about a missing `MSH`, or nothing at all -- which reads as a PipeHat defect rather than
  "that isn't an HL7 message". `runProvider` now checks first and names the actual problem,
  pointing at `Ctrl+Alt+Shift+E` for HL7 that detection legitimately missed (leading junk
  lines, header-less fragments). Answering *Yes* still runs it, so no capability is lost.
  Detection is re-run live instead of trusting the cached `view.isHL7`, which is only
  refreshed on buffer activation and would be stale for a message pasted into an open tab.

### Added

- **External transform providers.** *Transform with...* (`Ctrl+Alt+Shift+X`) pipes the active
  message to a configured command and puts the result in the other view, then runs Compare
  Views so every changed field is boxed in both panes. *Transform Again* (`Ctrl+Alt+Shift+A`)
  re-runs the last provider -- that is the key you hold down while learning a transformation
  language.
  - **PipeHat stays vendor-neutral.** It knows one contract and no vendors: `stdin` carries the
    message in, `stdout` carries the result out, `stderr` carries diagnostics, exit `0` means
    success. That is a UNIX filter, and it is the smallest thing that can express every engine
    worth supporting. The engine-specific half lives in the user's own wrapper script, so
    adding InterSystems IRIS, Mirth, Rhapsody, Cloverleaf, or Saxon never touches this repo.
  - Providers are declared in `PipeHat.providers` in the plugin config dir, same
    `key.attr=value` shape as `PipeHat.profile`: `<name>.command` (required), `.workdir`,
    `.timeout` (default 15000 ms), `.desc`. A documented, fully commented-out default is
    written on first run. Providers with no `command` are dropped rather than offered as a
    menu entry that can only fail.
  - The picker is a `TrackPopupMenu` at the cursor -- no new dialog template, no new resource
    IDs. One configured provider skips the picker entirely.
  - A non-zero exit shows `stderr` in the message box, because for someone learning a
    transformation language the compile error *is* the output. Nothing from `stderr` reaches
    `PipeHat.log`; it can contain message content and the log is metadata-only.
  - **Drop-in provider packages.** Beyond the hand-edited `PipeHat.providers`, PipeHat now
    reads `providers\*.provider` and `providers\<package>\*.provider`. Inside a `.provider`
    file, `${DIR}` expands to that file's own folder and a relative `workdir` resolves
    against it, so a package contains no absolute paths and no username and works wherever
    it is unzipped. **Install is "copy a folder", uninstall is "delete it"** -- the plugin
    experience, without PipeHat growing a DLL loader, an ABI, or a versioning burden. A
    provider that crashes still cannot take Notepad++ with it, and providers can still be
    written in any language.
  - Name collisions resolve to the **first** source: `PipeHat.providers` beats a drop-in
    that reuses a name. Silently overriding a file the user edited by hand is the wrong
    default.
  - **New module `TransformProvider.h`** (header-only, no NPP or Scintilla dependency), plus
    `tests/TransformProviderTest.cpp` -- 42 standalone assertions covering config parsing,
    the process round trip, a >256 KB payload, a missing executable, a non-zero exit, a
    timeout, three-source discovery, `${DIR}` expansion, relative workdir resolution,
    collision precedence, and a missing config directory.

### Invariants -- do not regress

- **The provider runs on a worker thread**, never the UI thread, and the result is marshaled
  back through the existing hidden `HWND_MESSAGE` window (`WM_TRANSFORM_RESULT`). A hung
  engine must never freeze Notepad++; `timeoutMs` terminates it.
- **stdin write, stdout read, and stderr read each get their own thread.** Doing any two in
  sequence on one thread deadlocks the moment the child fills a ~64 KB pipe buffer the parent
  is not draining -- which is exactly what an engine printing a stack trace does. The process
  wait stays on the calling thread so the timeout can actually fire. `TransformProviderTest`
  section [4] is what proves this, and it is not decorative.
- **Parent pipe ends are marked non-inheritable.** If the child inherits our read end, EOF
  never arrives and the drain threads hang forever.

---

## [2.1.0] -- 2026-07-16

Multi-message files, and MLLP replay that frames per message.

### Added

- **Multi-message file support.** A buffer may hold many messages (a log or batch file), and
  **each message is now parsed with its own MSH delimiters** -- a `!`-separated message sitting
  after a `|`-separated one reads correctly. Previously every consumer found the *first* MSH and
  applied its delimiters to the whole buffer, silently mis-parsing the rest.
  - The tree groups by message (`12/480  ADT^A01  [MSG012]`); a single-message buffer keeps the
    flat tree it always had.
  - Envelope segments (`FHS`/`BHS`/`BTS`/`FTS`) sit outside any message.
  - **Next / Previous Message** (`Ctrl+Alt+Shift+PgDn` / `PgUp`) step between messages and report
    "Message 12 of 480".
- **Replay All Messages (MLLP)** (`Ctrl+Alt+Shift+Y`) -- sends every message in the buffer as its
  **own MLLP frame with its own ACK**, reporting accepted / rejected / no-ACK / failed. *Send
  Message* frames the whole buffer as one message; a real receiver frames and ACKs per message,
  so this is the difference between an interface test and an echo.
  - Offers to **refresh MSH-10 (control id) and MSH-7 (datetime)** per message, and recommends it:
    receivers deduplicate on control id, so replaying captured messages with their original ids
    gets them accepted once and **silently discarded** on later runs -- a test that reports
    success while delivering nothing.
- `tests/MessageIndexTest.cpp` and `tests/MessageRefreshTest.cpp` -- standalone harnesses (70
  assertions between them), exit non-zero on failure.

### Fixed

- **Per-message delimiters across every consumer.** `HL7Lexer::parseMSH` now has exactly one
  caller in the codebase (`MessageIndex`); the styler, tree, PHI scrub, conformance check,
  validator, view diff, caret analysis, and RTF export all resolve delimiters through it. Eight
  independent derivations of one fact was the same shape as the C6 leak.
  - The **PHI scrub** was the sharp end: wrong delimiters shift field indices, and a shifted
    index means the PHI map is consulted for the wrong field.
  - **Validation** was distinct -- it called `parseMSH` on *every* MSH with no break, so a
    multi-message file was validated end to end with the **last** message's delimiters. Each
    message is now validated independently, which is also the more meaningful unit.
- **Scrub coverage check independence.** The anonymize-mode check now derives its field separator
  directly from the MSH line (MSH-1 *is* the separator), owing nothing to the lexer or the message
  index. A safety net that shares a dependency with the thing it audits is not a safety net -- an
  `OBX-5` embedded document containing a line starting with `MSH|` could forge a boundary and make
  both passes agree on the wrong answer.

### Known limitations

- Replay sends back to back; **no rate limiting** yet.
- No extract-message-to-new-tab.
- Everything in 2.0.0's Known limitations still applies -- notably **no TLS (MLLP/S)**.

### Verification

Release DLL builds clean (MSVC, Release, x64), no warnings, 18 hotkeys with no collisions.
`SegmentIDTest`, `MessageIndexTest` and `MessageRefreshTest` all exit 0. Multi-message parsing,
tree grouping, navigation and PHI scrub verified in Notepad++ against a 5-message file containing
a `!`-delimited message (161 fields scrubbed, 0 skipped, 0 coverage misses). MLLP send/receive
verified over loopback; **still not verified against a third-party endpoint** (e.g. Mirth Connect).

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

[2.3.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v2.3.0
[2.2.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v2.2.0
[2.1.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v2.1.0
[2.0.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v2.0.0
[1.2.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.2.0
[1.1.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.1.0
[1.0.0]: https://github.com/iacsha/PipeHat-npp/releases/tag/v1.0.0
