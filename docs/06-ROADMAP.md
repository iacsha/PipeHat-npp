# PipeHat -- Roadmap

> HL7 v2.x plugin for Notepad++. Status as of 2026-07-16 (**v2.0.0 released**).
> Companion to `CHANGELOG.md` (release history), `05-CODE-REVIEW.md` (defect inventory)
> and `01-ARCHITECTURE.md`.

## What's actually left after v2.0.0

Everything above the line in the sections below is shipped. The remaining work, honestly
scoped:

| Item | Priority | Notes |
|------|----------|-------|
| Live third-party MLLP test | **P0** | Send/receive is verified over loopback in NPP, not yet against a real endpoint (Mirth/BridgeLink). The one claim in the docs that rests on inference rather than a test. |
| ~~Multi-message file support~~ | ✅ done | MessageIndex is the single source of truth for boundaries + per-message delimiters; all eight former parseMSH sites migrated (parseMSH now has exactly one caller). Tree groups by message, Ctrl+Alt+Shift+PgDn/PgUp navigate. Verified in NPP on a 5-message file including a '!'-delimited message. Remaining: extract-message-to-new-tab. |
| ~~Replay a file to an endpoint~~ + MSH-10/MSH-7 refresh | ✅ done | Replay All Messages (Ctrl+Alt+Shift+Y): one MLLP frame per message, per-message ACK, accepted/rejected/noAck/failed report. Offers MSH-10/MSH-7 refresh (MessageRefresh.h, standalone-tested). Remaining: rate limiting (sends back to back). |
| **Field population profiler** | **P1** | See below. Now unblocked -- MessageIndex supplies the per-message iteration it needs. |
| C5-ui -- disk/backup residue warning | P1 | The last unshipped item from the original PHI review: warn that the on-disk original and Notepad++'s `backup\` snapshots may retain pre-scrub PHI. |
| **Data-driven segment/PHI tables** | **P1** | Generate `SegmentDB`/PHI maps from HAPI/nHapi metadata instead of hand-curating. **C6 is the argument**: nine PHI-bearing segments were unreachable for months because the tables are hand-written and nothing cross-checks them against the spec. This removes the bug class, not the instance, and closes M8 by construction. Pair with a coverage audit -- C6 was found by luck, and we do not currently know what else is missing from the PHI map. |
| Encoding doctor | P2 | See below. |
| Embedded document extraction (OBX-5) | P2 | See below. |
| Field editing helpers | P2 | See below. |
| HL7 standard tables (0001, 0003, 0076...) | P2 | See below. |
| Z-segment PHI mapping in the GUI | P2 | See below. |
| Enhanced-mode ACK (MSH-15/16) | P2 | Parsed but not honored; always application ACK. |
| Listener concurrency | P2 | Services one connection at a time. |
| **TLS / MLLP-S** | **P1** (may become P0) | Promoted from P2 on 2026-07-16. The largest remaining security gap: PHI crosses the wire in cleartext. Sequenced *after* the live third-party MLLP test, because adding TLS before the plain path is proven means debugging two unknowns at once -- **but if the target endpoint is TLS-only, this becomes the P0**, since the live test cannot run without it. Design and reasoning below. Interim answer that works today: **stunnel**. |
| Component/subcomponent tree depth | P2 | Tree is field-level only. |
| Conformance in tooltips + tree problem-list | P2 | Violations currently surface only as squiggles + a report dialog. |
| Auto-download + swap update | P2 | A loaded DLL can't overwrite itself -- needs a helper/restart step. Opt-in check-on-startup also open. |
| `nppPluginList` listing | P2 | Would make PipeHat installable via Notepad++'s PluginAdmin. |
| Dockable live log panel | P2 | Event log is a file today (menu: Open Event Log). |

**Closed as already done** (this doc had drifted from the code): L9 `.hl7` activation
(`currentPathHasHl7Ext`), L11 tree-nav off-by-one, and retiring the fixed-buffer
`ScintillaStyler::sciGetLine` -- all verified present/absent in the source on 2026-07-16.

---

## Proposed features (2026-07-16 planning pass)

### Multi-message file support -- P1

**PipeHat currently has no concept of a message boundary.** `MessageTreeView::refresh` finds
the *first* MSH, takes delimiters from it, then flattens every segment in the buffer into one
tree. `FHS`/`BHS` appear only in `detectHL7` for activation -- they are never used as batch
structure.

Interface engineers do not get one message, they get a log or batch file with hundreds. Today
that is a flat tree of thousands of segments with no grouping, and -- more seriously --
**delimiters read from the first MSH are applied to the whole file**, so a mixed-vendor log
where message #200 declares different encoding characters is silently mis-parsed. This is a
correctness gap hiding inside a missing feature, which is why it outranks the rest.

Scope: message navigator (next/prev, "message 12 of 480"), per-message tree grouping,
per-message delimiter scope, extract-message-to-new-tab, and real `FHS`/`BHS`/`BTS`/`FTS`
batch-envelope handling.

**Open design decision:** per-message lexer state vs. re-parsing MSH at each boundary. The
tokenizer is currently one-line-at-a-time and stateless apart from `m_delimiters`, so the
cheap version is "re-`parseMSH` when a boundary is crossed" -- but every caller that scans
line-by-line (styler, tree, scrub, conformance, validate) would need to honor boundaries, or
they will disagree. Given C6, **a split between what the lexer thinks and what a consumer
thinks is exactly the bug class to design out up front.**

### Replay a file to an endpoint (+ control-ID refresh) -- P1

Send a *file* of messages to a host at N msg/sec rather than one message. Most of the
transport work is already done by v2.0.0's MLLP layer; this turns PipeHat into a
regression/load harness for a Mirth channel.

**The prerequisite matters more than the feature: refresh MSH-10 and MSH-7 on send.**
Receivers deduplicate on control ID -- replaying the same message with the same MSH-10 means
a real engine may accept it once and silently drop the rest, which looks exactly like a broken
interface. Worth shipping as its own command even before replay exists.


### Coverage-check independence -- resolved 2026-07-16

When the scrub moved onto MessageIndex, the anonymize coverage check briefly took its field
separator from that same index -- reintroducing the C6 shape in the safety net, since a wrong
boundary would make both passes split identically, agree, and report clean. The concrete risk is
real: an OBX-5 embedded document whose payload contains a line starting with `MSH|` would forge a
boundary.

The check now tracks the separator itself: MSH-1 IS the field separator, so the character after
`MSH` can be read straight off the line with no help from the lexer or the index. Both failure
directions are covered -- too-narrow splitting cries wolf (which trains you to ignore the check),
shared-wrong splitting reports false cleanliness (the leak).

### TLS / MLLP-S -- P1 (promoted from P2, 2026-07-16)

MLLP is cleartext: PHI crosses the network unencrypted. This is the biggest remaining security
gap in the product, and healthcare networks increasingly require TLS for HL7 -- especially any
path that is not inside a hospital LAN or a VPN.

**The trap, and the reason this must not be rushed.** Encryption is not the hard part;
**certificate validation is**. A build that says "TLS" while accepting any certificate is *worse
than the cleartext we ship today*, because today's cleartext announces itself and gets a warning
dialog. A TLS checkbox next to an unvalidated connection is a lie the user cannot see through.

This bites immediately in practice: **test HL7 endpoints almost universally use self-signed
certificates**, so the first thing a user hits is a validation failure, and the tempting fix is a
"skip certificate validation" checkbox that then stays ticked forever. That is how a TLS feature
rots into decoration. If we offer that escape hatch it must be gated exactly like the
cleartext-PHI confirmation: off by default, explicit, loud, re-confirmed per session, never
silent, and logged.

**Use SChannel, not OpenSSL.** PipeHat's "no runtime dependencies beyond the NPP/Scintilla host"
property is a real virtue -- install is *copy one DLL*. OpenSSL means shipping DLLs, owning a CVE
surface, and tracking upstream advisories for a plugin whose only update path is "check GitHub".
SChannel ships with Windows, uses the system trust store (so an organization's internal CA already
works), and inherits OS patching. The cost is that SSPI is genuinely unpleasant --
`InitializeSecurityContext` loops and manual buffer management, roughly 500-800 lines of fiddly
code. Worth it. `MllpTransport` is already isolated behind `sendSync` / `Listener`, which is the
seam to slot it into; `MllpProtocol.h` (pure framing) should not change at all.

**Client first, server later.** These are not equal work:
- **Sender (Send + Replay)** is the common case and much simpler: connect, validate the server
  cert, send. No provisioning. This is what points at a TLS-enabled Mirth channel.
- **Listener** needs a certificate, which forces "where does it come from?" -- cert store picker,
  self-signed generation, PFX import -- plus real key handling. PipeHat is a *test tool*: far more
  often the client than the server.
- **Mutual TLS (client certificate) is a real requirement** at many healthcare endpoints, not an
  afterthought. It is phase two of the sender, ahead of any listener work.

**Shape:** a security dropdown in Settings -> MLLP -- `None` / `TLS` / `TLS + client certificate`
-- with hostname verification on by default and a separate, loudly-gated "accept self-signed"
opt-in for test endpoints.

**Interim answer, documented today: stunnel.** Fronting MLLP with stunnel is the standard way to
add TLS to a protocol that lacks it, and many shops already do it. Point PipeHat at
`localhost:2575` and let stunnel terminate TLS outbound. Zero code, works now, and it is the right
advice for anyone who needs this before the feature lands.

### Field population profiler -- P1

Run across N messages and report which fields are *ever* populated, how often, and with what
value distribution ("does this vendor actually send PID-19? what values appear in PV1-2?").
That is the interface-discovery question on every new integration, and it is pure analysis over
machinery that already exists. Depends on multi-message support.

### Encoding doctor -- P2

CR vs LF vs CRLF segment terminators, BOM, non-printable characters, and an MSH-18 charset that
disagrees with the actual bytes. Perennial real-world interface pain, and squarely an editor's
job.

### Embedded document extraction -- P2

`OBX-5` carrying a base64 PDF/RTF/CDA: extract and save/open it. The codebase already treats
this as the pathological long-line case (see `CLAUDE.md`), so it may as well be a feature.
Note this is also the *only* legitimate way other document formats enter PipeHat -- as
payloads inside v2, not as peer formats (see Scope boundaries).

### Field editing helpers -- P2

Insert/delete a field with correct pipe counting; escape/unescape a value. Hand-editing HL7 is
miserable precisely *because* you are counting pipes, and this is the thing a text-editor
plugin is uniquely positioned to fix.

### HL7 standard tables -- P2

Built-in HL7 tables (0001 gender, 0003 event type, 0076 message type, ...) to decode coded
values beyond MSH-9/EVN-1, and to let a conformance rule reference a table instead of a
hand-typed value list. Natural companion to data-driven tables.

### Z-segment PHI mapping in the GUI -- P2

Z-segments are currently scrubbed wholesale (`isZSegment` -> replace every field). Safe, but
blunt: it destroys non-PHI Z data you may need to keep, and Z-segment layouts are site-specific
by definition, so the mapping belongs in the editable profile rather than compiled in.

---

## Scope boundaries -- considered and declined

Recorded so they are not re-litigated. Revisit only with a specific reason.

**FHIR (v2 -> FHIR conversion / FHIR resource view)** -- deferred 2026-07-16, *not* rejected on
merit. It is the most strategically interesting item on this page and the closest to the real
problem (legacy v2 and modern FHIR fragmentation). It is also a different product: a resource
model, a serializer, and a mapping layer, none of which reuse the delimiter tokenizer that is
PipeHat's engine. Decision: out of scope **at this time**; if taken up, it defines a next act
rather than a next feature.

**X12 EDI (837/835/270/271)** -- the only declined format with a real architectural case.
Structurally it is the same shape as HL7 v2: self-declaring delimiters in the ISA envelope
(as MSH does), segment ID + delimited elements + components. Tokenizer, tree, dictionary
tooltips, conformance rules, and PHI mapping keyed on (segment, element) would all map nearly
1:1. Declined anyway because:
- **Identity.** PipeHat is named after `|^~\&`. "HL7 v2.x for Notepad++" is a crisp promise;
  "healthcare EDI viewer" is a different product with a different name.
- **Licensing.** X12 implementation guides (TR3s) are commercially licensed -- their content
  cannot simply be embedded the way HL7 v2 structure can.
- **Audience.** v2 is clinical interface work; X12 is revenue cycle. The overlap is thinner
  than the structural similarity suggests.
- **C6.** Our hand-curated tables already have unknown gaps. Doubling the table surface before
  fixing *how tables are produced* is backwards.
If it ever happens, the right shape is a sibling plugin sharing the tokenizer -- not a PipeHat
mode.

**HL7 v2 XML encoding** -- same data model, entirely different parser (nothing reuses the
tokenizer), and Notepad++ already highlights XML. Cheap and honest alternative: *detect* v2 XML
and say so, rather than mis-highlighting it as delimited v2.

**CDA / C-CDA** -- XML, template-driven, an order of magnitude larger than v2 structure. A
different tool. (Reachable as an OBX-5 payload via embedded document extraction, which is the
right level of support.)

**CSV / fixed-width flat files** -- common as interface counterparts, but generic: there is no
data dictionary, no segment grammar, and no PHI map to bring. Existing CSV plugins do this
better, and PipeHat would add nothing but a menu entry.

**ASTM E1381/E1394** -- lab-instrument framing, structurally close to MLLP, but niche and
declining. Revisit only on a concrete request.

**DICOM** -- binary, not text. Not an editor's problem.

## Legend

- ✅ Done · 🔧 In progress · ⏭️ Next · 💡 Proposed
- Priority: **P0** safety/correctness · **P1** high-value · **P2** polish

---

## Shipped (v1.0.x hardening)

| Item | What | Status |
|------|------|--------|
| Crash: styling | `SCI_SETSTYLINGEX` -> `SCI_SETSTYLING` | ✅ |
| Crash: line reads | Length-safe reads via `SciUtils.h` (`SCI_LINELENGTH`-sized) | ✅ |
| PHI leak: undo | `SCI_EMPTYUNDOBUFFER` after scrub | ✅ |
| PHI leak: parser | Escape sequences can't cross field separators; MSH off-by-one fixed | ✅ |
| PHI fail-closed | Skipped-field count + residual SSN/digit scan -> warning dialog | ✅ |
| Build hardening | `/guard:cf /sdl /GS /DYNAMICBASE /NXCOMPAT` | ✅ |
| Brand | Renamed to **PipeHat** | ✅ |

---

## Shipped (v1.1.0)

| Item | What | Status |
|------|------|--------|
| Trigger-event decoding | MSH-9 / EVN-1 message-type + event decode in tooltips and tree (`TriggerEventDB.h`) | ✅ |
| M8 (partial) | Safe Harbor dates (admit/discharge/observation/event/dx/procedure) + provider segments (ROL/AIP/AIG/AIL/PRD/PV1-52); residual scan now flags email + IPv4 | ✅ |
| Conformance profiles | Per-interface `max` / `values` / `required` rules from editable `PipeHat.profile`; `Check Conformance` squiggles + report (`ConformanceProfile.h`) | ✅ |
| Navigation hotkeys | Ctrl+Alt+ T/H/C/F and Ctrl+Alt+<-/-> field nav | ✅ |
| Panel behavior | Tree no longer auto-loads on startup; follows the active buffer and closes with the HL7 message | ✅ |

**Still open from these areas:** M8 remainder (age > 89, anonymize-mode structural coverage
check), conformance in hover tooltips + tree problem-list.

---

## Shipped (v1.2.0)

| Item | What | Status |
|------|------|--------|
| Escape decoding | `HL7Escape.h` -- `\F\ \S\ \T\ \R\ \E\ \.br\ \Xhh\` decoded on hover | ✅ |
| HL7 version awareness | MSH-12 -> version name + era in tooltip/tree (`versionName`) | ✅ |
| Validation / malform | `Validator.h` + Validate command (Ctrl+Alt+V); advisory squiggles | ✅ |
| Message compare/diff | `MessageDiff.h` + Compare Views (Ctrl+Alt+Shift+D); side-by-side view comparison with field-level highlighting in both panes | ✅ |
| Pretty-print | Segments-per-line reformat (Ctrl+Alt+R) | ✅ |
| Segment folding | `setFoldLevels` -- detail segments fold under their parent | ✅ |
| Broader activation | MSH/FHS/BHS + BOM/blank-line skip; `.hl7` ext; manual Enable (Ctrl+Alt+E) | ✅ |
| Small fixes | Tree-nav off-by-one (L11); retired unused `sciGetLine` | ✅ |

**Note:** *Version awareness* here is detection + surfacing of MSH-12, not per-version field
tables (that remains the P2 data-driven-tables item). *Validation* is the built-in structural
half; *conformance profiles* (v1.1) are the configurable half.

---

## Shipped (v1.3.0)

| Item | What | Status |
|------|------|--------|
| Settings GUI | `SettingsDialog.{h,cpp}` -- modal editor for conformance rules (Plugins > PipeHat > Settings, Ctrl+Alt+P). ListView grid `segment / field / max / allowed values / required`; Add/Edit/Remove + a single-rule editor sub-dialog; reads and writes `PipeHat.profile` and reloads the profile on save so Check Conformance updates without a restart | ✅ |

**Note:** The GUI is the source of truth on save -- rule lines are regenerated from
the grid (the documented header comment block is preserved). The file format is unchanged,
so hand-editing still works and round-trips through the dialog.

---

## Shipped -- MLLP send / receive over TCP (v2.0.0)

The plugin's first network feature, behind an off-by-default toggle. Send and receive are
verified over loopback in Notepad++; a third-party-endpoint test (Mirth) is still outstanding.

| Layer | What | Status |
|-------|------|--------|
| Protocol | `MllpProtocol.h` (header-only, pure) -- MLLP framing, incremental stream de-framer, `buildAck`/`parseAck` | ✅ 20/20 standalone test |
| Transport | `MllpTransport.{h,cpp}` -- Winsock sender (non-blocking connect + timeout) and background-thread listener (accept loop, per-connection service, clean stop); UI-agnostic | ✅ 12/12 loopback test |
| Integration | `main.cpp` -- hidden message-only window marshals inbound -> new buffer (UI thread) and ACK results -> dialog; menu items **Send Message (MLLP)** (Ctrl+Alt+Shift+M) and **Toggle MLLP Listener** (Ctrl+Alt+Shift+L); config in `PipeHat.ini` | ✅ loopback send/receive verified in NPP |

**Security model (implemented):** OFF by default; loopback-only bind unless the
user opts in *and* supplies a bind address (`MllpConfig::effectiveBindAddr` fails
safe); one-time-per-session cleartext-PHI confirmation; extra confirmation on any
non-loopback bind; listener stopped + marshaling window destroyed at
`NPPN_SHUTDOWN` (never in `DllMain` -- loader lock). Default startup opens **no
sockets**.

**Still to do:** live send/receive test against a real MLLP endpoint (e.g. a Mirth
listener); enhanced-mode ACK (MSH-15/16) is not honored (always application ACK);
one connection serviced at a time; no TLS (MLLP/S) -- flagged as cleartext.

---

## Upcoming fixes (remaining review items)

| ID | Item | Priority | Notes |
|----|------|----------|-------|
| M6 | ✅ **done** -- fakes are now deterministic + referentially consistent | P1 | `generateFake` seeds its RNG from a hash of the original value, so the same input always yields the same fake (linkage preserved across the message). Statelessly deterministic -- no cache needed. Standalone-tested (6/6). |
| M7 | ✅ **done** -- incremental styling | P1 | `SCN_MODIFIED` now re-styles only the edited line range (`styleRange`), not the whole document; fold/detection work runs only when lines are added/removed. Editing the MSH delimiter line falls back to a full restyle (delimiters affect every line). |
| M8 | ⚙️ **mostly done** -- date + provider coverage; MSH-7 added | P1 | Safe Harbor date elements (MSH-7, EVN, PV1-44/45, OBR-7/8/14, OBX-14, DG1-5, PR1-5, SCH-11) and provider segments (ROL/AIP/AIG/AIL/PRD/CTD/PV1-52) mapped; residual scan flags email/IPv4. Fake DOBs keep age ≤ 89. Remaining: explicit age fields aren't standard-mapped (no fixed HL7 age field). |
| -- | ✅ **done** -- anonymize-mode coverage check | P1 | After scrubbing, an independent raw-split pass verifies every PHI-mapped non-empty field was actually replaced; a mismatch with the tokenizer warns (fail-closed). Fills the gap where the residual scan can't run on identifier-shaped fakes. |
| C5-ui | Disk/backup residue warning | P2 | Warn that the on-disk original + Notepad++ `backup\` snapshots may retain pre-scrub PHI. |

---

## Advancement features

### P1 -- makes PipeHat the tool people install

**Trigger-event / code-value decoding** 💡
Hover or tree-label decode of coded values, not just field names:
- **ADT** `A01`–`A62` (A01 Admit, A02 Transfer, A03 Discharge, A04 Register, A08 Update, A11 Cancel Admit, …)
- **SIU** `S12`–`S26` (S12 New Appt, S13 Reschedule, S14 Modify, S15 Cancel, S17 Delete, …)
- **ORM/ORU/MDM/DFT** common events; also `EVN-1` event type and other ID-table fields.
- Implementation: two small `code -> meaning` tables keyed by message type; parse the `^`-delimited MSH-9 and append the decode in `showFieldTooltip` and the tree node label. Low effort, high daily value.

**HL7 version awareness** 💡
Read MSH-12, load the matching field table (PID/PV1/OBX layouts differ across 2.3/2.5/2.7). Biggest single correctness upgrade for tooltips + tree. Pairs with M8 (data-driven tables).

**Escape-sequence decoding** 💡
Render `\F\ \S\ \T\ \R\ \E\` and `\Xhh\` as their literal characters on hover. The tokenizer already isolates escape tokens correctly (post-C3/C4 fix), so this is a display layer.

**Message compare / diff** 💡 *(engineer-requested)*
Grab a working message and a non-working message, align by segment + field, and highlight the differences -- the core interface-troubleshooting workflow.
- Segment-aware alignment (match on segment ID + set-id / sequence, not raw text diff).
- Field-level highlight: present-vs-missing, value-changed, extra/absent segment.
- Ignore-list for expected-to-differ fields (MSH-7 datetime, MSH-10 control ID).
- UI: two-buffer compare (reuse Notepad++'s split view) + a diff summary in the tree panel.
- Natural pairing with **malform detection** below (diff *and* validate in one pass).

**Validation / malform detection** 💡 *(engineer-requested)*
Advisory, never blocking (per R7 -- must never crash on real-world dialects):
- Structural: missing required segments, required-field-empty, wrong field counts.
- Encoding: MSH-1/MSH-2 sanity, unterminated escapes, bad segment IDs.
- Datatype: non-numeric in NM, malformed DTM/TS dates.
- Surface as squiggle indicators (header reserves `INDIC_SQUIGGLE`) + a problems list in the tree panel. Z-segments and unknown IDs are informational, not errors.

**Configurable conformance profiles** 💡 *(engineer-requested)*
The general case of malform detection: user-editable, per-interface rule sets that PipeHat
checks live and flags. Turns PipeHat from a viewer into a **pre-flight conformance checker**
("will the receiver accept this message?"). Rule types per field / component:
- **Max length** -- warn/highlight when data exceeds the limit (the classic "receiver truncates
  or rejects past N chars"). Default lengths come from the HL7 data-type table; the profile overrides.
- **Allowed value set (enumeration)** -- field must be one of a listed set; warn otherwise.
  Site-specific: one endpoint wants gender `M/F/U` (HL7 table 0001), another `Male/Female`,
  another a binary set. Also covers coded fields generally (event types, order status, etc.).
- **Required / optional override** and **datatype** per field, layered on the built-in `SegmentDB`.
- **Per-interface profiles** -- the same field has different rules at different endpoints, so rules
  are *not* hardcoded. Ship as an editable config file (JSON/YAML), selectable per message.
  Pairs with compare/diff: diff a message against a *profile*, not just another message.
- This is the practical form of an HL7 conformance profile. Highest-leverage upgrade for the
  daily "why did the receiving system reject this?" troubleshooting loop. Foundation for the
  data-driven tables in P2.

**MLLP send / receive over TCP/IP** 💡 *(engineer-requested)* -- **major, v2.0**
Turn PipeHat from a viewer into a live interface tester using HL7's Minimal Lower Layer
Protocol (MLLP) framing (`<VT>` 0x0B … message … `<FS>` 0x1C `<CR>` 0x0D):
- **Send** the active message to a host:port and display the returned ACK/NAK (parse MSA-1).
- **Listen** as an MLLP server on a port, capturing inbound messages into new buffers and
  returning an ACK.
- **Security posture change -- must be explicit.** PipeHat currently has *zero network egress*;
  this is the first feature that opens a socket. Requirements: **off by default**, explicit
  per-action confirmation, a visible "listening/connected" indicator, bind to loopback unless
  the user opts into a specific interface, and a clear warning that **PHI will cross the wire in
  cleartext** unless TLS (MLLP/S) is configured. Threading: the listener runs on a background
  thread and must marshal buffer creation back to the UI thread.
- Sits behind its own build flag / settings toggle so the default distribution stays
  egress-free for users who want a pure offline viewer.

### P2 -- workflow polish

- **Conformance rules GUI** ✅ *shipped v1.3.0* -- modal editor (`SettingsDialog`, Ctrl+Alt+P)
  listing rules as `segment | field | max | allowed-values | required` with add/edit/remove and
  input validation, saving back to `PipeHat.profile`.
  - ✅ **"Add rule from current field"** -- menu: *Add Conformance Rule from Field* seeds the rule
    editor from the field at the caret (segment + field pre-filled).
  - ✅ **SegmentDB-backed dropdowns** -- the rule editor's segment/field inputs are dropdowns
    populated from `SegmentDB` (picking a segment refreshes its field list); still editable for Z-segments.
  - ✅ **Named / switchable profiles** -- a profile selector + *New* in the dialog manages
    `PipeHat[.name].profile` files; the active profile persists in `PipeHat.ini` `[Conformance]`
    and drives `loadProfile`. Per-interface rule sets, switchable without hand-editing files.
- **Pretty-print / reformat** -- expand a packed message to one-field-per-line and back.
- **Segment folding** -- the menu command exists; set fold levels per segment so it actually collapses.
- **Copy field path** ✅ **done** -- Copy Field Path (Ctrl+Alt+K) copies the HL7 path at the caret
  (`PID-5.1.2`, component/subcomponent-aware, MSH off-by-one honored), matching Mirth/BridgeLink
  references; transient calltip confirms. (Right-click context-menu entry still a possible nicety.)
- **Copy as rich text (RTF)** ✅ **done** -- Copy as Rich Text (Ctrl+Alt+W) serializes the colored
  tokens to RTF and puts `CF_RTF` (+ plain `CF_UNICODETEXT`) on the clipboard, so pasting into
  Word/Outlook keeps the syntax colors and alternating field shading. No NppExport dependency.
- **Data-driven segment/PHI tables** -- generate from HAPI/nHapi metadata instead of the hand-curated maps (closes M8 by construction).
- **Component/subcomponent tree depth** -- expand fields into components in the tree (currently field-level only).
- **HL7Soup-style color coding** ✅ **done** -- richer palette with alternating field-value shading
  (v1.3.x) *and* a current-field highlight that boxes the field under the caret on caret move.
- **MLLP / event log** ✅ **done** -- timestamped `PipeHat.log` in the config folder (menu: Open
  Event Log). Records MLLP listener start/stop, inbound (type/control id/segment count + ACK),
  outbound send + ACK/NAK/connect result, and scrub/conformance/validation run metadata. PHI-aware:
  metadata only (counts, control ids, host:port, result codes) -- never field values or bodies.
  (A dockable live log panel remains a possible nicety.)
- **Auto-update / update prompt** ✅ **done (check)** -- Check for Updates (menu) queries the GitHub
  Releases API (`UpdateCheck.{h,cpp}`, WinHTTP, off the UI thread), compares the latest tag against
  `HL7_PLUGIN_VERSION`, and either links to the release or reports "up to date". User-initiated only --
  never automatic, no telemetry. Verified against the live API. Still open: auto-*download*+swap
  (a loaded DLL can't overwrite itself -- needs a helper/restart step), an opt-in check-on-startup,
  and listing in the official `nppPluginList` for PluginAdmin management.

---

## Suggested release slices

- **v1.1 "Trustworthy"** -- M6, M8, anonymize coverage check, C5-ui. PHI scrubbing you can rely on.
- **v1.2 "Fluent"** -- trigger-event decoding, HL7 version awareness, escape decoding. The daily-driver upgrade.
- **v1.3 "Troubleshooter"** -- message compare/diff + validation/malform detection. The interface-engineer power tools.
- **v1.4 "Polish"** -- pretty-print, folding, copy-path, incremental lexing (M7), data-driven tables.
