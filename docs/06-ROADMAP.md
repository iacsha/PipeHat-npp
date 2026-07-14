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

## Upcoming fixes (remaining review items)

| ID | Item | Priority | Notes |
|----|------|----------|-------|
| M6 | Anonymization is deterministic (seed-0, dead seed path) + no referential integrity | P1 | Hash each original value to seed its fake; cache original→fake so the same input yields the same output across the message. Restores linkage for usable test data. |
| M7 | Full re-lex on every keystroke | P1 | Style only the modified line range from `SCN_MODIFIED`/`SCN_STYLENEEDED`; unusable on multi-MB logs until fixed. |
| M8 | PHI map coverage gaps vs HIPAA Safe Harbor | P1 | Missing: all date elements except year (MSH-7, EVN, PV1-44/45, OBR-7, OBX-14), age > 89, email/IP fields, ROL/AIG/AIP provider segments. |
| — | Anonymize-mode coverage check | P1 | Residual scan can't run in anonymize mode (fake data is identifier-shaped). Substitute: verify every PHI-mapped field for a segment was actually replaced (structural coverage), warn on any miss. |
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

### P2 — workflow polish

- **Pretty-print / reformat** — expand a packed message to one-field-per-line and back.
- **Segment folding** — the menu command exists; set fold levels per segment so it actually collapses.
- **Copy field path** — right-click a value → copy `PID-5.1` (matches Mirth/BridgeLink channel references).
- **Data-driven segment/PHI tables** — generate from HAPI/nHapi metadata instead of the hand-curated maps (closes M8 by construction).
- **Component/subcomponent tree depth** — expand fields into components in the tree (currently field-level only).

---

## Suggested release slices

- **v1.1 "Trustworthy"** — M6, M8, anonymize coverage check, C5-ui. PHI scrubbing you can rely on.
- **v1.2 "Fluent"** — trigger-event decoding, HL7 version awareness, escape decoding. The daily-driver upgrade.
- **v1.3 "Troubleshooter"** — message compare/diff + validation/malform detection. The interface-engineer power tools.
- **v1.4 "Polish"** — pretty-print, folding, copy-path, incremental lexing (M7), data-driven tables.
