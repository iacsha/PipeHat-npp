# AGENTS.md — PipeHat

Guidance for AI coding agents (OpenCode, etc.) working in this repo. Read before editing.
Claude Code also reads `CLAUDE.md` (fuller architecture notes); this file is the short,
regression-critical subset. Keep both in sync when invariants change.

## What this is

**PipeHat** — a native C++ Notepad++ plugin (`PipeHat.dll`, x64, Unicode) for HL7 v2.x:
syntax highlighting, field tooltips, a dockable message tree, and a PHI scrubber. Activates
when a buffer's first line starts with `MSH`.

Build: `cmake -S . -B build -A x64 && cmake --build build --config Release` →
`build/Release/PipeHat.dll`. No tests in-repo; a standalone lexer harness lives outside the
repo. Docs: `docs/05-CODE-REVIEW.md` (defects + fix status), `docs/06-ROADMAP.md`.

## Non-negotiable invariants (these caused real bugs — do not regress)

1. **Never read a Scintilla line into a fixed stack buffer.** `SCI_GETLINE` takes the *line
   number* (not a length), writes the whole line, and does **not** NUL-terminate — fixed
   buffers overflow and `strlen()` over-reads. Always use the helpers in `src/SciUtils.h`
   (`getLineUtf8` / `getLineW`), which size from `SCI_LINELENGTH`.
2. **Single-style runs use `SCI_SETSTYLING` (int style), never `SCI_SETSTYLINGEX`** (that
   one wants a `char*` style array; passing an int faults).
3. **HL7 escape sequences must never cross a field separator.** When scanning `\...\`, stop
   at the field separator / EOL; an undelimited `\` (including the `\` in MSH-2 `^~\&`) is a
   literal, not an escape. Getting this wrong silently miscounts fields.
4. **MSH is off-by-one: MSH-1 IS the field separator, so the first value is MSH-2.** Every
   field counter (scrubber, tree, tooltip) starts one higher for `MSH`.
5. **The PHI scrubber must fail CLOSED.** A field the parser can't account for must be
   *reported*, never silently left unscrubbed. `cmdScrubPHI` counts skips and runs a
   residual identifier scan; keep that path intact. Silent PHI retention is the worst
   possible failure for this tool.
6. **Scrub empties the undo buffer** (`SCI_EMPTYUNDOBUFFER`) so originals aren't Ctrl+Z
   recoverable. Don't remove it.
7. **Never crash on malformed input.** Real-world HL7 has custom Z-segments, odd delimiters,
   and dialect quirks. Validation is advisory, never blocking.

## Conventions

- Vendored headers in `include/npp/` are **stripped** — only constants this plugin uses. Add
  a `#define` there before using a new Scintilla message / NPP notification.
- The dock panel's `pszModuleName` must equal the deployed DLL name (`HL7_PLUGIN_DLL` =
  `PipeHat.dll`).
- `docs/00–04` are the original design brief and are **aspirational** — several classes they
  describe don't exist. Trust the source and `docs/05`/`docs/06`.
