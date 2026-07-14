# Architecture Decision — HL7 Reader Plugin

## Plugin SDK: C++ Native (Recommended)

### Rationale

| Criterion | C++ Native | C# (.NET Bridge) |
|-----------|-----------|-------------------|
| Distribution | Single DLL, drop into `plugins/` | Requires .NET runtime + bridge DLL |
| NPP Plugin Admin | Directly submittable | Not supported |
| Performance | Native Scintilla access | Marshaling overhead per paint |
| Scintilla integration | `SCI_*` messages, indicators, markers | Wrapper layer, limited indicator API |
| Iteration speed | Slower compile cycle | Faster (hot reload possible) |
| UI complexity | Raw Win32 (painful) | WinForms/WPF (easy) |
| User install friction | Zero (single file) | Medium (checking .NET version) |
| Debugging | Standard VS debugger | Mixed-mode debugging |

**Winner: C++** — distribution simplicity trumps dev speed for an NPP plugin. The UI is modest (tooltip + dockable tree panel), so Win32 pain is bounded.

### Alternative: C# for prototyping, C++ for release
If we want fast iteration, build the lexer + segment tables in C# first to validate the parser, then port the core to C++. The segment definition tables are portable JSON.

## Component Architecture

```
┌─────────────────────────────────────────────────┐
│                  Notepad++ Host                   │
├─────────────────────────────────────────────────┤
│  Scintilla Editor         │  Plugin Host (NPPN)  │
│  (text buffer, lexer,     │  (menu, toolbar,     │
│   indicators, markers)     │   dockable panels)   │
└──────────┬────────────────┴──────────┬──────────┘
           │                           │
┌──────────▼───────────────────────────▼──────────┐
│              HL7ReaderPlugin.dll                  │
├──────────────────────────────────────────────────┤
│  Plugin Entry (NPPN bridge)                      │
│  ├─ commandMenu()        → menu registration     │
│  ├─ setInfo()            → plugin metadata       │
│  └─ beNotified()         → buffer/language hooks │
├──────────────────────────────────────────────────┤
│  Lexer Engine                                    │
│  ├─ HL7Lexer            → tokenize(text, MSH)    │
│  ├─ Token types: SEGMENT, FIELD_SEP, COMP_SEP,   │
│  │   REPEAT_SEP, SUBCOMP_SEP, ESCAPE_SEQ, VALUE  │
│  └─ ScintillaStyleMap   → token→style mapping    │
├──────────────────────────────────────────────────┤
│  Message Parser                                  │
│  ├─ SegmentParser       → raw text → segments[]  │
│  ├─ FieldParser         → segment → fields[]     │
│  └─ ComponentParser     → field → components[]   │
├──────────────────────────────────────────────────┤
│  Segment Knowledge Base                          │
│  ├─ SegmentDefs (JSON)  → MSH→{fields:[...]}     │
│  ├─ DataTypeDefs (JSON) → ST→String, DT→Date...  │
│  └─ VersionRegistry     → 2.3, 2.5, 2.7 tables  │
├──────────────────────────────────────────────────┤
│  UI Layer                                        │
│  ├─ TooltipProvider     → hover → field info     │
│  ├─ MessageTreePanel    → dockable tree view     │
│  ├─ CodeFolding         → segment-level folding  │
│  └─ IndicatorManager    → error/warning squiggles│
└──────────────────────────────────────────────────┘
```

## Lexer Pipeline

```
Raw HL7 text
    │
    ▼
[MSH Detection] ── read MSH-1 (field sep), MSH-2 (encoding chars)
    │
    ▼
[Line Tokenizer] ── per-line: extract segment ID, split on field sep
    │
    ▼
[Style Application] ── Scintilla SCI_STARTSTYLING / SCI_SETSTYLINGEX
    │
    ▼
[Indicator Overlay] ── red squiggles on parse errors, blue on repeats
```

### Token Types

| Token | Style | Example |
|-------|-------|---------|
| `SEGMENT_ID` | Bold, color by category | `MSH`, `PID`, `OBR` |
| `FIELD_SEP` | Dim gray | `|` |
| `COMPONENT_SEP` | Dim gray | `^` |
| `REPEAT_SEP` | Dim gray | `~` |
| `SUBCOMP_SEP` | Dim gray | `&` |
| `ESCAPE_SEQ` | Teal | `\F\`, `\S\` |
| `FIELD_VALUE` | Default text | `Müller^Hans` |
| `FIELD_EMPTY` | Faint | `||` (two adjacent pipes) |
| `VERSION_ID` | Italic (in MSH-12) | `2.5.1` |

### Segment Color Categories

| Category | Color | Segments |
|----------|-------|----------|
| Header | Dark blue bold | `MSH`, `BHS`, `FHS` |
| Patient | Blue | `PID`, `PD1`, `NK1`, `GT1` |
| Order/Observation | Green | `OBR`, `OBX`, `ORC`, `OM1-OM6` |
| Financial | Orange | `IN1-IN3`, `FT1`, `PR1` |
| Pharmacy | Purple | `RXA`, `RXD`, `RXE`, `RXG`, `RXO`, `RXR` |
| Custom/Z-segments | Gray italic | `Z*` |

## Performance Budget

| Operation | Target | Notes |
|-----------|--------|-------|
| Lexer on buffer change | <16ms (1 frame) | Incremental re-lex of changed lines only |
| Full document parse | <500ms for 10K lines | Background thread, tree updates batched |
| Tooltip show | <50ms | Cached field lookup, hot path |
| Memory (segment tables) | <2MB | Embedded JSON, mmap'd |
| DLL size | <500KB | Static link, no external deps beyond NPP SDK |
