# HL7 Reader Plugin -- Independent Code Review

> Additive review document. No plugin source was modified during this assessment.
> Scope: `src/`, `include/npp/`, `CMakeLists.txt` (~2,300 LOC). Reviewed 2026-07-14.
> Verdict: **Do not run against real PHI yet.** Two crash-class bugs and a
> silent-PHI-retention design flaw must be fixed first. Architecture is otherwise sound
> and self-contained (no network/file/registry/clipboard egress).

---

## Fix Status -- 2026-07-14

Crash class and undo-leak addressed in this pass (build verified, Release DLL compiles clean):

- ✅ **C1 / H5** -- all `SCI_GETLINE` reads routed through length-safe helpers in `src/SciUtils.h`
  (`getLineUtf8` / `getLineW`), sized from `SCI_LINELENGTH`. No fixed stack buffers or
  `strlen`-on-buffer remain in `src/`.
- ✅ **C2** -- styling now uses `SCI_SETSTYLING` (both sites); highlighter no longer faults.
- ✅ **C5** -- `SCI_EMPTYUNDOBUFFER` called after scrub; originals no longer Ctrl+Z recoverable.
- ➕ **ISC-28 desync** -- styling now backfills every line to its full `SCI_LINELENGTH`, keeping
  the styling cursor byte-aligned.
- ➕ Dead seed-scan loop in `cmdScrubPHI` removed.

- ✅ **C3 / C4** -- tokenizer escape handling reworked so an escape sequence can never
  cross a field separator; a lone/undelimited `\` (including the `\` in MSH-2 `^~\&`) is
  treated as a literal, so field counting is preserved. Fixed in `HL7Lexer::tokenize` and
  `getFieldIndexAtPosition`. Also fixed the MSH off-by-one (MSH-1 *is* the separator, so
  the first value is MSH-2) in the scrubber, tree, and tooltip counters.
- ✅ **Fail-closed scrub** -- `cmdScrubPHI` now counts unprocessable PHI fields and, in
  removal mode, runs `scanResidualPII` (SSN + long-digit-run scan). Any skip or residual
  hit switches the completion dialog to a warning that says *do not treat as
  de-identified*. **Verified with a standalone lexer test** (`scratchpad/lexer_test.cpp`):
  MSH-3/4/5/6/10 map correctly and backslash fields no longer swallow later fields -- all
  assertions pass.

**Still open (next):** M6 (deterministic/dead-seed anonymization), M7 (incremental lexing),
M8 (PHI map coverage). The disk/backup residue warning for C5 is not yet surfaced in UI.
The residual scan does not yet run in anonymize mode (fake data is identifier-shaped by
design) -- a structural coverage check is the planned substitute (see roadmap).

---

## Severity Summary

| # | Severity | Finding | File:line |
|---|----------|---------|-----------|
| C1 | 🔴 Critical → ✅ | `SCI_GETLINE` used with no length guard → stack buffer overflow on long lines | main.cpp; ScintillaStyler.cpp; MessageTreeView.cpp (now via `SciUtils.h`) |
| C2 | 🔴 Critical → ✅ | Syntax styling uses `SCI_SETSTYLINGEX` (pointer msg) with an int → access violation | ScintillaStyler.cpp |
| C3 | 🟠 High → ✅ | PHI scrubbing silently retains PHI when the line miscounts fields | HL7Lexer.cpp (tokenize/getFieldIndexAtPosition); main.cpp |
| C4 | 🟠 High → ✅ | MSH tokenization destroyed by `\&` + MSH off-by-one → MSH PHI never scrubbed | HL7Lexer.cpp; main.cpp; MessageTreeView.cpp |
| C5 | 🔴 Critical → ✅ | Scrub never empties the undo buffer → original PHI recoverable with Ctrl+Z | main.cpp |
| H5 | 🟠 High | `strlen()` on non-NUL-terminated Scintilla buffer → over-read / bogus length | every `SCI_GETLINE` call site |
| M6 | 🟡 Medium | Deterministic LCG + dead seed path → same fake data every run, no referential integrity | PHIScrubber.cpp:60-72,153-165; main.cpp:153-165 |
| M7 | 🟡 Medium | `styleAll()` on every `SCN_MODIFIED` → full re-lex per keystroke (large-file freeze) | main.cpp:366-381 |
| M8 | 🟡 Medium | PHI field map has HIPAA Safe Harbor gaps (see §PHI Coverage) | PHIScrubber.cpp:178-282 |
| L9 | 🔵 Low | About box claims `.hl7` auto-activation; detection is MSH-first-line only | main.cpp:263 vs ScintillaStyler.cpp:225-238 |
| L10 | 🔵 Low | No compiler/linker hardening flags set (`/guard:cf`, `/GS` explicit) | CMakeLists.txt |
| L11 | 🔵 Low | Tree navigation off-by-one: `SCI_GOTOLINE` is 0-based, node stores `li+1` | MessageTreeView.cpp:147,291 |

---

## Critical Findings

### C1 -- `SCI_GETLINE` stack buffer overflow (untrusted input → memory corruption)

Every read of document text uses this pattern:

```cpp
char lbuf[4096];
lbuf[0] = (char)(sizeof(lbuf) - 1);      // no-op: overwritten by GETLINE
fn(ptr, SCI_GETLINE, li, (sptr_t)lbuf);  // writes the ENTIRE line, no length cap
int ll = (int)strlen(lbuf);              // reads past copied bytes (see H5)
```

`SCI_GETLINE(line, char*)` takes the **line number** as `wParam` -- not a buffer
length. Scintilla writes the whole line into the buffer regardless of its size and
does **not** NUL-terminate. The `lbuf[0] = sizeof-1` line is a misapplied convention
(that pattern belongs to length-passing messages like `SCI_GETCURLINE`) and does
nothing here.

**Consequence:** any HL7 line longer than the fixed buffer (4096 or 8192 bytes)
overflows the stack. This is not exotic in HL7 -- an `OBX-5` carrying a base64-encoded
embedded PDF/CDA document routinely exceeds 8 KB on a single line. A crafted or merely
large message corrupts the stack inside Notepad++ → crash at best, controllable
overwrite at worst. **This is the headline security finding: unbounded write from
untrusted file content.**

**Fix:** query `SCI_LINELENGTH` (id 2350, already present in the header) first,
allocate a `std::vector<char>` of that size, capture the byte count from the
`SendMessage` return value, and NUL-terminate at that index. Never use a fixed stack
buffer for document lines.

### C2 -- Syntax styling passes an integer where a pointer is required

```cpp
// ScintillaStyler.cpp:214
sciV(SCI_SETSTYLINGEX, utf8Len, style);   // style is an int (e.g. 1)
```

`SCI_SETSTYLINGEX` (2073) expects `(int length, const char *styles)` -- a pointer to a
per-character style array. The code passes the **style number** as the `lParam`.
Scintilla then dereferences that integer as an address and reads `utf8Len` bytes from,
e.g., address `0x1` → access violation.

The intended message is `SCI_SETSTYLING` (2033) -- `(int length, int style)` -- which is
also defined in the bundled header one line above. As written, styling would fault the
moment any HL7 buffer is colorized, which strongly implies the highlighter path has
never actually run on a live document.

**Fix:** change `SCI_SETSTYLINGEX` → `SCI_SETSTYLING` at both call sites (lines 214,
219). One-word change; it is the difference between "highlighting works" and "N++
crashes on open."

---

## High Findings

### C3 / C4 -- PHI scrubber silently retains PHI (the dangerous class)

The scrubber's safety rests on positionally counting fields (`fieldIdx++` per field
separator) and looking each `segId + fieldIdx` up in a static PHI map. That is only as
correct as the tokenizer, and the tokenizer is fragile:

- **C4 (MSH):** For `MSH|^~\&|EPIC|SENDFAC|...`, the encoding-characters field contains
  `\`, the escape delimiter. The tokenizer (HL7Lexer.cpp:161-171) treats `\` as the
  start of an escape sequence and scans to the *next* `\` -- which doesn't exist on the
  line -- swallowing `&|EPIC|SENDFAC|...` into one token. Field counting collapses, so
  **MSH-3/4/5/6/10 (sending/receiving app + facility + message control ID) are never
  scrubbed.** Those are exactly the routing identifiers an analyst thinks got removed.

- **C3 (general):** The same swallow happens for any field whose data contains a lone
  `\` (malformed but common in real feeds). Every field *after* the stray backslash is
  miscounted, so genuine PHI (name, SSN, DOB) silently slips through with **no error and
  no visual cue.** The "Scrubbed N PHI fields" success dialog actively misleads the user
  into trusting an incomplete result.

**First-principles read:** a de-identifier's one job is a *guarantee* of removal. This
architecture cannot provide it, because its failure mode is silent retention -- strictly
worse than a visible failure, because the user ships the file believing it is clean.
The fix is not a patch to the tokenizer; it is a design change: scrub by an
allowlist/denylist that operates on the *parsed field model with verification*, and
after scrubbing, **re-scan the output for residual identifier patterns** (SSN regex,
long digit runs, known-name dictionary) and refuse to report success if any remain.

### H5 -- `strlen()` on a non-terminated buffer

Because `SCI_GETLINE` doesn't NUL-terminate and the buffers aren't `memset` (except in
`detectHL7`), `strlen(lbuf)` reads past the real line into stack garbage until it hits a
stray zero. That bogus length then feeds `MultiByteToWideChar`, compounding C1. Fix
falls out of the C1 fix (use the returned byte count; never `strlen`).

---

### C5 -- Original PHI survives in the undo buffer (residue channel)

The scrub is a genuine byte replacement (`SCI_REPLACETARGET`, main.cpp:242) -- good, it
is not a display-only mask. But it is wrapped in
`SCI_BEGINUNDOACTION`/`SCI_ENDUNDOACTION`, which **preserves** the pre-scrub text as a
single undo step, and there is no `SCI_EMPTYUNDOBUFFER` anywhere in `src/`. So after the
user runs "Scrub PHI" and sees the success dialog, a single **Ctrl+Z restores every
original identifier**, and the undo history can also be serialized by Notepad++. This is
the classic way a scrubber passes a functional test and still leaks.

Reviewers should also flag two out-of-process residue channels the plugin can't fix but
should *warn* about: (1) the original file on disk is untouched until the user saves,
and even then is overwritten, not securely wiped; (2) Notepad++ writes periodic
snapshots to `%AppData%\Notepad++\backup\` and session files that may already contain the
pre-scrub content. A de-identification tool that stays silent about these gives false
assurance.

**Fix:** after applying replacements, call `SCI_EMPTYUNDOBUFFER`, and surface a dialog
noting on-disk original + N++ backup caveats. The one-question test the advisor pass
crystallized: *after scrub runs, where does the original PHI still exist?* -- answer it in
source before trusting the tool.

### Fail-open framing for C3/C4 (severity driver)

C3 and C4 are not merely "correctness" bugs. On malformed or oversized input the
tokenizer bails and the affected fields are left **unscrubbed and unmarked** -- the
scrubber **fails open** (emits the original) rather than fail-closed (drops/blocks the
field). Silent PHI passthrough on attacker- or feed-shaped input is what makes these
High, not Medium: a green run on a clean sample message will never surface it. Any fix
must make a parse miss fail *closed* -- refuse to report success on any line the parser
could not fully account for.

## Medium Findings

### M6 -- Anonymization is deterministic and has no referential integrity

`fakeRandom()` is a global LCG seeded only by `fakeInit()`, which **is never called** —
the seed-scan block in `main.cpp:153-165` computes a `seed` string and then `break`s
without using it (dead code). So `g_fakeSeed` starts at 0 every run and produces the
*same* fake identities every time. Worse, the same real value (e.g. patient ID
appearing in PID-3 and PV1-19) maps to *different* fakes because replacement is
positional, not value-keyed -- so a de-identified message loses the internal linkage that
makes it useful as test data. For realistic anonymization, hash each original value to
seed its fake and cache original→fake so identical inputs yield identical outputs.

### M7 -- Full re-lex on every keystroke

`SCN_MODIFIED` calls `view->styler.styleAll()` (main.cpp:374), which re-tokenizes the
entire document on each insert/delete. On a multi-MB HL7 log this makes typing unusable.
Style only the modified line range (you already receive `position`/`linesAdded`).

### M8 -- PHI coverage gaps vs HIPAA Safe Harbor

The map is a good start but, judged against the 18 Safe Harbor identifiers, misses:
`PID-7` DOB *is* covered but **PID-8 sex + PID-10 race + PID-22 ethnicity** are not;
**PID-3 with an embedded MRN check digit**, **email fields (e.g. contact XTN with email
type)**, **device/IP-bearing fields**, and free-text **OBX-5 with `TX` type carrying
narrative** (currently scrubbed unconditionally, which may over-scrub numeric results).
`ORC`/`OBR` provider names are covered; **ROL, AIG, AIP** provider segments are not.
Recommend deriving the map from a maintained reference (HAPI/nHapi metadata) rather than
hand curation.

---

## Low Findings

- **L9** -- About box says "activates for .hl7 files"; there is no extension check
  anywhere -- activation is purely "first line starts with MSH" (ScintillaStyler.cpp:234).
  Add real extension/langtype detection or correct the copy.
- **L10** -- `CMakeLists.txt` sets no hardening. Add `/guard:cf`, ensure `/GS`,
  `/DYNAMICBASE`, `/NXCOMPAT`, and `/sdl`. Cheap defense-in-depth for a DLL that parses
  untrusted input.
- **L11** -- `SCI_GOTOLINE` is 0-based; segment nodes store `li+1` (MessageTreeView.cpp:147)
  and navigate with it (line 291), landing one line below the clicked segment.

## What's Right (don't lose these)

- **No egress.** Grep for `LoadLibrary`, `CreateProcess`, `ShellExecute`, sockets,
  `WinHttp`, registry writes, file writes, and clipboard all return zero hits. The plugin
  keeps document content in-process -- appropriate for PHI-adjacent tooling.
- **Undo safety.** Scrubbing is wrapped in `SCI_BEGINUNDOACTION`/`ENDUNDOACTION` and
  applied in reverse order so byte offsets stay valid (main.cpp:238-245). Correct.
- **Double-confirm before scrub.** Mode + backup-reminder dialogs reduce accidental
  destructive edits.
- **Clean separation** of lexer / styler / segment DB / tree / scrubber. The refactor
  surface for the fixes above is small and well-contained.
- **Format-string buffers are correctly sized** (`fakeSSN`, `fakePhone`, `zip`, DOB) —
  tight but verified in-bounds; no overflow there.

---

## Recommended fix order

1. C2 (one-word `SCI_SETSTYLING` fix) -- unblocks the highlighter.
2. C1/H5 (line-length-driven buffered reads) -- closes the overflow across all files.
3. C3/C4 (tokenizer + post-scrub residual scan) -- makes PHI removal trustworthy.
4. M6/M7 -- anonymization quality and editor responsiveness.
5. M8/L9/L10/L11 -- coverage, honesty, hardening, polish.
