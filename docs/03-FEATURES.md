# Feature Breakdown by Phase

## Phase 1: MVP — Proper Syntax Highlighting (Week 1)

**Goal:** Replace UDL with a real lexer that respects per-message delimiters.

| # | Feature | Effort | Notes |
|---|---------|--------|-------|
| 1.1 | NPP plugin skeleton | S | `DllMain`, `setInfo`, `getName`, `getFuncsArray` |
| 1.2 | Buffer change hook | S | `beNotified(NPPN_BUFFERACTIVATED, NPPN_FILESAVED)` |
| 1.3 | MSH-1/MSH-2 parser | S | Read delimiter config from first segment |
| 1.4 | Line-level tokenizer | M | Per-line: extract segment ID, split on field sep |
| 1.5 | Scintilla style application | M | `SCI_STARTSTYLING`, `SCI_SETSTYLINGEX`, style array |
| 1.6 | Segment color categories | S | Map segment→color (header/patient/order/financial/pharmacy/custom) |
| 1.7 | Delimiter styling | S | Dim gray for pipes, carets, tildes, ampersands |
| 1.8 | Escape sequence highlighting | S | Teal color for `\F\`, `\S\`, `\T\`, `\R\`, `\E\`, hex escapes |
| 1.9 | File extension auto-detect | S | Register `.hl7`, `.HL7`, `.er7` extensions |
| 1.10 | Installer / README | S | Drop-DLL instructions |

**MVP Deliverable:** Open any `.hl7` file, get context-aware syntax highlighting. Already surpasses the UDL by reading dynamic delimiters from MSH.

---

## Phase 2: Segment Knowledge (Week 2)

**Goal:** Field name tooltips and basic validation.

| # | Feature | Effort | Notes |
|---|---------|--------|-------|
| 2.1 | Embedded segment definitions | M | JSON resource compiled into DLL (MSH, PID, OBR, OBX, NK1, PV1, IN1, ORC first) |
| 2.2 | Scintilla hover hook | M | `SCI_GETMOUSEDWELLDURATION`, `SCI_WORDSTARTPOSITION` → token → lookup |
| 2.3 | Field tooltip | M | Hover over a field → show "PID-5: Patient Name (XPN, Required)" |
| 2.4 | Required field validation | S | Red indicator in margin for missing required fields |
| 2.5 | Data type validation (basic) | S | NM must be numeric, DT/DTM format check, IS must be in table |
| 2.6 | Version auto-detect | S | Read MSH-12, load matching segment definitions |
| 2.7 | Segment definition table (first 15 segments) | M | MSH,PID,PD1,NK1,PV1,PV2,OBR,OBX,ORC,NTE,AL1,DG1,IN1,PR1,GT1 |

**Phase 2 Deliverable:** Hover over any field and see what it means. Red squiggles on broken messages.

---

## Phase 3: Message Navigation (Week 3)

**Goal:** Tree view, folding, field jumping.

| # | Feature | Effort | Notes |
|---|---------|--------|-------|
| 3.1 | Dockable message tree panel | L | `NPPM_DMMREGASDCKDLG`, tree view control |
| 3.2 | Segment-level code folding | M | Scintilla fold points at each segment boundary |
| 3.3 | Tree → editor sync | M | Click tree node → scroll editor to that segment |
| 3.4 | Field jump (Ctrl+G) | S | "Go to PID-5" → scroll to field position |
| 3.5 | Next/Previous segment navigation | S | Ctrl+Down/Ctrl+Up → jump between segment starts |
| 3.6 | Segment count in status bar | S | "23 segments, 142 fields" |
| 3.7 | MSH header summary panel | S | Quick-glance metadata: version, message type, sending app, datetime |

**Phase 3 Deliverable:** Full IDE-style navigation. Tree shows message structure at a glance.

---

## Phase 4: Advanced Features (Week 4+)

| # | Feature | Effort | Notes |
|---|---------|--------|-------|
| 4.1 | Pretty-print / re-indent | M | One segment per line, align field separators |
| 4.2 | Export to CSV/JSON | M | Parse full message → structured output |
| 4.3 | Segment definition for ALL v2.7 segments | L | 100+ segment types, ~1500 fields total |
| 4.4 | v2.3 / v2.5 / v2.6 segment variant tables | L | Same segment, different field positions per version |
| 4.5 | Message validation rules | M | ADT^A01 requires PID, PV1; ORU^R01 requires PID, OBR, OBX |
| 4.6 | Batch file support | M | Handle FHS/BHS/BTS/FTS wrappers |
| 4.7 | Trigger event auto-complete | S | Type "ADT^" → dropdown of valid events |
| 4.8 | Field value picker for coded fields | M | PID-8 (Administrative Sex): dropdown of HL7 Table 0001 values |
| 4.9 | Dark mode support | S | Scintilla style variants for dark NPP themes |
| 4.10 | NPP Plugin Admin submission | S | Package for official plugin list |

---

## Estimated Total Effort

| Phase | Calendar | Dev Hours |
|-------|----------|-----------|
| Phase 1 (MVP) | 1 week | ~20h |
| Phase 2 (Knowledge) | 1 week | ~15h |
| Phase 3 (Navigation) | 1 week | ~20h |
| Phase 4 (Advanced) | 2-3 weeks | ~40h |
| **Total** | **5-6 weeks** | **~95h** |

Phase 1 alone is already a significant upgrade over the existing UDL. Each phase ships independently.
