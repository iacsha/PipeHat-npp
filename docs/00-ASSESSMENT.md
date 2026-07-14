# HL7 Reader Plugin for Notepad++ — Assessment

## Existing Foundation

The [StevenGhyselbrecht repo](https://github.com/StevenGhyselbrecht/notepad-plus-plus-hl7-syntax-highlighting) provides a single UDL XML file (Notepad++'s built-in "User Defined Language" system). It is **not a plugin** — it is a static regex-based syntax highlighter with zero logic.

### What it does
| Feature | Status |
|---------|--------|
| Segment name coloring (MSH, PID, OBR...) | Blue bold (Keywords1) |
| Message type coloring (ADT, ORM, ORU...) | Maroon bold (Keywords2) |
| Event type coloring (A01, A04...) | Maroon bold (Keywords3) |
| Delimiter coloring (pipe, caret, ampersand, tilde) | Red (Operators1) |
| ASCII control char rendering | Delimiters list (00-23) |

### What it does NOT do
- No segment folding/collapsing
- No field name tooltips or hover info
- No message structure tree/outline
- No field-level navigation
- No HL7 version awareness (segments differ across 2.3/2.5/2.7)
- No validation (required segments, field counts, data types)
- No escape sequence decoding (\F\, \S\, \R\, \T\, \E\)
- No pretty-printing or reformatting
- No MSH-driven encoding detection (field separators are configurable per message)

### UDL limitations (why a real plugin is needed)
- UDL has no concept of context — cannot distinguish MSH-3 from PID-3
- No hover/intellisense support
- No side panels or tree views
- No folding beyond bracket matching
- No dynamic lexing (HL7 delimiters are defined in MSH-1 and MSH-2)

## Viability Scorecard

| Factor | Score | Notes |
|--------|-------|-------|
| Grammar complexity | 7/10 | Deterministic but deeply nested (segment→field→repeat→component→subcomponent) |
| Reference data needs | 6/10 | 100+ segment types, 5+ HL7 versions — manageable as embedded JSON |
| Plugin SDK maturity | 8/10 | NPP plugin API is ~20 years old, stable, well-documented |
| C++ vs C# tradeoff | 5/10 | C++ is native but slow to iterate; C# is faster dev but needs bridge |
| User-facing value | 9/10 | Any HL7 analyst using NPP would install this immediately |
| Maintenance burden | 3/10 | HL7 v2 spec is stable; segment tables change slowly |
| **Overall viability** | **7.5/10** | Strong candidate for vibe coding |

## Recommendation

**Build it.** Start with a C++ native plugin (most reliable distribution path for NPP users), phase the features so MVP ships in a week. See `01-ARCHITECTURE.md` for tech stack rationale.

## Key Decisions Required

1. **Plugin SDK:** C++ (native) vs C# (.NET bridge) — see architecture doc
2. **HL7 version:** Target v2.7 first (covers 95% of real-world messages), add v2.3/v2.5 later
3. **Distribution:** Manual DLL drop vs NPP Plugin Admin (requires plugin list submission)
4. **Segment tables:** Embedded JSON resource vs compiled-in C++ maps
