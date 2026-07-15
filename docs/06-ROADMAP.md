# PipeHat — Roadmap

> HL7 v2.x plugin for Notepad++. Status as of 2026-07-14.
> Companion to `05-CODE-REVIEW.md` (defect inventory) and `01-ARCHITECTURE.md`.

## Legend

- ✅ Done · 🔧 In progress · ⏭️ Next · 💡 Proposed
- Priority: **P0** safety/correctness · **P1** high-value · **P2** polish

---

## Shipped (v1.0.x hardening)

| Item | What | Status |
|------|------|--------|
| Crash: styling | `SCI_SETSTYLINGEX` → `SCI_SETSTYLING` | ✅ |
| Crash: line reads | Length-safe reads via `SciUtils.h` (`SCI_LINELENGTH`-sized) | ✅ |
| PHI leak: undo | `SCI_EMPTYUNDOBUFFER` after scrub | ✅ |
| PHI leak: parser | Escape sequences can't cross field separators; MSH off-by-one fixed | ✅ |
| PHI fail-closed | Skipped-field count + residual SSN/digit scan → warning dialog | ✅ |
| Build hardening | `/guard:cf /sdl /GS /DYNAMICBASE /NXCOMPAT` | ✅ |
| Brand | Renamed to **PipeHat** | ✅ |

---

## Shipped (v1.1.0)

| Item | What | Status |
|------|------|--------|
| Trigger-event decoding | MSH-9 / EVN-1 message-type + event decode in tooltips and tree (`TriggerEventDB.h`) | ✅ |
| M8 (partial) | Safe Harbor dates (admit/discharge/observation/event/dx/procedure) + provider segments (ROL/AIP/AIG/AIL/PRD/PV1-52); residual scan now flags email + IPv4 | ✅ |
| Conformance profiles | Per-interface `max` / `values` / `required` rules from editable `PipeHat.profile`; `Check Conformance` squiggles + report (`ConformanceProfile.h`) | ✅ |
| Navigation hotkeys | Ctrl+Alt+ T/H/C/F and Ctrl+Alt+←/→ field nav | ✅ |
| Panel behavior | Tree no longer auto-loads on startup; follows the active buffer and closes with the HL7 message | ✅ |

**Still open from these areas:** M8 remainder (age > 89, anonymize-mode structural coverage
check), conformance in hover tooltips + tree problem-list.

---

## Shipped (v1.2.0)

| Item | What | Status |
|------|------|--------|
| Escape decoding | `HL7Escape.h` — `\F\ \S\ \T\ \R\ \E\ \.br\ \Xhh\` decoded on hover | ✅ |
| HL7 version awareness | MSH-12 → version name + era in tooltip/tree (`versionName`) | ✅ |
| Validation / malform | `Validator.h` + Validate command (Ctrl+Alt+V); advisory squiggles | ✅ |
| Message compare/diff | `MessageDiff.h` + Compare-with-clipboard (Ctrl+Alt+D); segment/field aligned, new-tab report | ✅ |
| Pretty-print | Segments-per-line reformat (Ctrl+Alt+R) | ✅ |
| Segment folding | `setFoldLevels` — detail segments fold under their parent | ✅ |
| Broader activation | MSH/FHS/BHS + BOM/blank-line skip; `.hl7` ext; manual Enable (Ctrl+Alt+E) | ✅ |
| Small fixes | Tree-nav off-by-one (L11); retired unused `sciGetLine` | ✅ |

**Note:** *Version awareness* here is detection + surfacing of MSH-12, not per-version field
tables (that remains the P2 data-driven-tables item). *Validation* is the built-in structural
half; *conformance profiles* (v1.1) are the configurable half.

---

## Shipped (v1.3.0)

| Item | What | Status |
|------|------|--------|
| Settings GUI | `SettingsDialog.{h,cpp}` — modal editor for conformance rules (Plugins > PipeHat > Settings, Ctrl+Alt+P). ListView grid `segment / field / max / allowed values / required`; Add/Edit/Remove + a single-rule editor sub-dialog; reads and writes `PipeHat.profile` and reloads the profile on save so Check Conformance updates without a restart | ✅ |

**Note:** The GUI is the source of truth on save — rule lines are regenerated from
the grid (the documented header comment block is preserved). The file format is unchanged,
so hand-editing still works and round-trips through the dialog.

---

## In progress — MLLP send / receive over TCP (v2.0, unreleased)

The plugin's first network feature. Built and integrated behind an off-by-default
toggle; needs live in-Notepad++ verification before release.

| Layer | What | Status |
|-------|------|--------|
| Protocol | `MllpProtocol.h` (header-only, pure) — MLLP framing, incremental stream de-framer, `buildAck`/`parseAck` | ✅ 20/20 standalone test |
| Transport | `MllpTransport.{h,cpp}` — Winsock sender (non-blocking connect + timeout) and background-thread listener (accept loop, per-connection service, clean stop); UI-agnostic | ✅ 12/12 loopback test |
| Integration | `main.cpp` — hidden message-only window marshals inbound → new buffer (UI thread) and ACK results → dialog; menu items **Send Message (MLLP)** (Ctrl+Alt+M) and **Toggle MLLP Listener** (Ctrl+Alt+L); config in `PipeHat.ini` | ⏳ built; manual NPP test pending |

**Security model (implemented):** OFF by default; loopback-only bind unless the
user opts in *and* supplies a bind address (`MllpConfig::effectiveBindAddr` fails
safe); one-time-per-session cleartext-PHI confirmation; extra confirmation on any
non-loopback bind; listener stopped + marshaling window destroyed at
`NPPN_SHUTDOWN` (never in `DllMain` — loader lock). Default startup opens **no
sockets**.

**Still to do:** live send/receive test against a real MLLP endpoint (e.g. a Mirth
listener); enhanced-mode ACK (MSH-15/16) is not honored (always application ACK);
one connection serviced at a time; no TLS (MLLP/S) — flagged as cleartext.

---

## Upcoming fixes (remaining review items)

| ID | Item | Priority | Notes |
|----|------|----------|-------|
| M6 | ✅ **done** — fakes are now deterministic + referentially consistent | P1 | `generateFake` seeds its RNG from a hash of the original value, so the same input always yields the same fake (linkage preserved across the message). Statelessly deterministic — no cache needed. Standalone-tested (6/6). |
| M7 | ✅ **done** — incremental styling | P1 | `SCN_MODIFIED` now re-styles only the edited line range (`styleRange`), not the whole document; fold/detection work runs only when lines are added/removed. Editing the MSH delimiter line falls back to a full restyle (delimiters affect every line). |
| M8 | ⚙️ **mostly done** — date + provider coverage; MSH-7 added | P1 | Safe Harbor date elements (MSH-7, EVN, PV1-44/45, OBR-7/8/14, OBX-14, DG1-5, PR1-5, SCH-11) and provider segments (ROL/AIP/AIG/AIL/PRD/CTD/PV1-52) mapped; residual scan flags email/IPv4. Fake DOBs keep age ≤ 89. Remaining: explicit age fields aren't standard-mapped (no fixed HL7 age field). |
| — | ✅ **done** — anonymize-mode coverage check | P1 | After scrubbing, an independent raw-split pass verifies every PHI-mapped non-empty field was actually replaced; a mismatch with the tokenizer warns (fail-closed). Fills the gap where the residual scan can't run on identifier-shaped fakes. |
| C5-ui | Disk/backup residue warning | P2 | Warn that the on-disk original + Notepad++ `backup\` snapshots may retain pre-scrub PHI. |
| L9 | Real `.hl7` / langtype activation | P2 | Currently MSH-first-line only; About text now honest, but extension trigger still absent. |
| L11 | Tree navigation off-by-one | P2 | `SCI_GOTOLINE` is 0-based; segment nodes store `li+1`. |
| — | Retire unused `ScintillaStyler::sciGetLine` | P2 | The old fixed-buffer wrapper is now unused; remove to prevent reintroduction. |

---

## Advancement features

### P1 — makes PipeHat the tool people install

**Trigger-event / code-value decoding** 💡
Hover or tree-label decode of coded values, not just field names:
- **ADT** `A01`–`A62` (A01 Admit, A02 Transfer, A03 Discharge, A04 Register, A08 Update, A11 Cancel Admit, …)
- **SIU** `S12`–`S26` (S12 New Appt, S13 Reschedule, S14 Modify, S15 Cancel, S17 Delete, …)
- **ORM/ORU/MDM/DFT** common events; also `EVN-1` event type and other ID-table fields.
- Implementation: two small `code → meaning` tables keyed by message type; parse the `^`-delimited MSH-9 and append the decode in `showFieldTooltip` and the tree node label. Low effort, high daily value.

**HL7 version awareness** 💡
Read MSH-12, load the matching field table (PID/PV1/OBX layouts differ across 2.3/2.5/2.7). Biggest single correctness upgrade for tooltips + tree. Pairs with M8 (data-driven tables).

**Escape-sequence decoding** 💡
Render `\F\ \S\ \T\ \R\ \E\` and `\Xhh\` as their literal characters on hover. The tokenizer already isolates escape tokens correctly (post-C3/C4 fix), so this is a display layer.

**Message compare / diff** 💡 *(engineer-requested)*
Grab a working message and a non-working message, align by segment + field, and highlight the differences — the core interface-troubleshooting workflow.
- Segment-aware alignment (match on segment ID + set-id / sequence, not raw text diff).
- Field-level highlight: present-vs-missing, value-changed, extra/absent segment.
- Ignore-list for expected-to-differ fields (MSH-7 datetime, MSH-10 control ID).
- UI: two-buffer compare (reuse Notepad++'s split view) + a diff summary in the tree panel.
- Natural pairing with **malform detection** below (diff *and* validate in one pass).

**Validation / malform detection** 💡 *(engineer-requested)*
Advisory, never blocking (per R7 — must never crash on real-world dialects):
- Structural: missing required segments, required-field-empty, wrong field counts.
- Encoding: MSH-1/MSH-2 sanity, unterminated escapes, bad segment IDs.
- Datatype: non-numeric in NM, malformed DTM/TS dates.
- Surface as squiggle indicators (header reserves `INDIC_SQUIGGLE`) + a problems list in the tree panel. Z-segments and unknown IDs are informational, not errors.

**Configurable conformance profiles** 💡 *(engineer-requested)*
The general case of malform detection: user-editable, per-interface rule sets that PipeHat
checks live and flags. Turns PipeHat from a viewer into a **pre-flight conformance checker**
("will the receiver accept this message?"). Rule types per field / component:
- **Max length** — warn/highlight when data exceeds the limit (the classic "receiver truncates
  or rejects past N chars"). Default lengths come from the HL7 data-type table; the profile overrides.
- **Allowed value set (enumeration)** — field must be one of a listed set; warn otherwise.
  Site-specific: one endpoint wants gender `M/F/U` (HL7 table 0001), another `Male/Female`,
  another a binary set. Also covers coded fields generally (event types, order status, etc.).
- **Required / optional override** and **datatype** per field, layered on the built-in `SegmentDB`.
- **Per-interface profiles** — the same field has different rules at different endpoints, so rules
  are *not* hardcoded. Ship as an editable config file (JSON/YAML), selectable per message.
  Pairs with compare/diff: diff a message against a *profile*, not just another message.
- This is the practical form of an HL7 conformance profile. Highest-leverage upgrade for the
  daily "why did the receiving system reject this?" troubleshooting loop. Foundation for the
  data-driven tables in P2.

**MLLP send / receive over TCP/IP** 💡 *(engineer-requested)* — **major, v2.0**
Turn PipeHat from a viewer into a live interface tester using HL7's Minimal Lower Layer
Protocol (MLLP) framing (`<VT>` 0x0B … message … `<FS>` 0x1C `<CR>` 0x0D):
- **Send** the active message to a host:port and display the returned ACK/NAK (parse MSA-1).
- **Listen** as an MLLP server on a port, capturing inbound messages into new buffers and
  returning an ACK.
- **Security posture change — must be explicit.** PipeHat currently has *zero network egress*;
  this is the first feature that opens a socket. Requirements: **off by default**, explicit
  per-action confirmation, a visible "listening/connected" indicator, bind to loopback unless
  the user opts into a specific interface, and a clear warning that **PHI will cross the wire in
  cleartext** unless TLS (MLLP/S) is configured. Threading: the listener runs on a background
  thread and must marshal buffer creation back to the UI thread.
- Sits behind its own build flag / settings toggle so the default distribution stays
  egress-free for users who want a pure offline viewer.

### P2 — workflow polish

- **Conformance rules GUI** ✅ *shipped v1.3.0* — modal editor (`SettingsDialog`, Ctrl+Alt+P)
  listing rules as `segment | field | max | allowed-values | required` with add/edit/remove and
  input validation, saving back to `PipeHat.profile`.
  - ✅ **"Add rule from current field"** — menu: *Add Conformance Rule from Field* seeds the rule
    editor from the field at the caret (segment + field pre-filled).
  - ⏳ Still open: `SegmentDB`-backed segment/field dropdowns in the rule editor, and per-interface
    profile switching (multiple named profile files).
- **Pretty-print / reformat** — expand a packed message to one-field-per-line and back.
- **Segment folding** — the menu command exists; set fold levels per segment so it actually collapses.
- **Copy field path** ✅ **done** — Copy Field Path (Ctrl+Alt+K) copies the HL7 path at the caret
  (`PID-5.1.2`, component/subcomponent-aware, MSH off-by-one honored), matching Mirth/BridgeLink
  references; transient calltip confirms. (Right-click context-menu entry still a possible nicety.)
- **Copy as rich text (RTF)** ✅ **done** — Copy as Rich Text (Ctrl+Alt+W) serializes the colored
  tokens to RTF and puts `CF_RTF` (+ plain `CF_UNICODETEXT`) on the clipboard, so pasting into
  Word/Outlook keeps the syntax colors and alternating field shading. No NppExport dependency.
- **Data-driven segment/PHI tables** — generate from HAPI/nHapi metadata instead of the hand-curated maps (closes M8 by construction).
- **Component/subcomponent tree depth** — expand fields into components in the tree (currently field-level only).
- **HL7Soup-style color coding** ✅ **done** — richer palette with alternating field-value shading
  (v1.3.x) *and* a current-field highlight that boxes the field under the caret on caret move.
- **MLLP / event log** ✅ **done** — timestamped `PipeHat.log` in the config folder (menu: Open
  Event Log). Records MLLP listener start/stop, inbound (type/control id/segment count + ACK),
  outbound send + ACK/NAK/connect result, and scrub/conformance/validation run metadata. PHI-aware:
  metadata only (counts, control ids, host:port, result codes) — never field values or bodies.
  (A dockable live log panel remains a possible nicety.)
- **Auto-update / update prompt** ✅ **done (check)** — Check for Updates (menu) queries the GitHub
  Releases API (`UpdateCheck.{h,cpp}`, WinHTTP, off the UI thread), compares the latest tag against
  `HL7_PLUGIN_VERSION`, and either links to the release or reports "up to date". User-initiated only —
  never automatic, no telemetry. Verified against the live API. Still open: auto-*download*+swap
  (a loaded DLL can't overwrite itself — needs a helper/restart step), an opt-in check-on-startup,
  and listing in the official `nppPluginList` for PluginAdmin management.

---

## Suggested release slices

- **v1.1 "Trustworthy"** — M6, M8, anonymize coverage check, C5-ui. PHI scrubbing you can rely on.
- **v1.2 "Fluent"** — trigger-event decoding, HL7 version awareness, escape decoding. The daily-driver upgrade.
- **v1.3 "Troubleshooter"** — message compare/diff + validation/malform detection. The interface-engineer power tools.
- **v1.4 "Polish"** — pretty-print, folding, copy-path, incremental lexing (M7), data-driven tables.
