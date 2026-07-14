# Risk Assessment & Unknowns

## Technical Risks

### R1: Dynamic delimiter parsing breaks Scintilla incremental lexing
**Severity:** Medium | **Likelihood:** High
Scintilla's built-in lexer model assumes static keyword/delimiter sets. HL7 changes delimiters per message. If the user has two messages with different MSH-1 values in the same file, the lexer needs per-message delimiter awareness.
**Mitigation:** Lex at segment granularity, not document granularity. Each line's style is computed from the nearest-preceding MSH-1/MSH-2. Cache delimiter state per line range.

### R2: Notepad++ plugin API limitations on hover/dwell
**Severity:** Low | **Likelihood:** Medium
`SCN_DWELLSTART` and `SCN_DWELLEND` are Scintilla notifications, but NPP's plugin message loop may not forward them reliably in all versions.
**Mitigation:** Test on latest NPP (8.x) first — it's the target. Fall back to right-click context menu tooltip if dwell events are unreliable.

### R3: Segment definition table maintenance
**Severity:** Low | **Likelihood:** High
100+ segment types × 5+ HL7 versions × ~20 fields per segment = ~10,000 field definitions. Manual entry is error-prone.
**Mitigation:** Scrape HL7 definition tables from authoritative sources (hl7-definition.codeplex.com archive, or the hl7apy Python library's JSON exports). Auto-generate the embedded resource.

### R4: C++ build toolchain friction on Windows
**Severity:** Medium | **Likelihood:** Medium
Building NPP plugins requires Visual Studio (MSVC), matching NPP's architecture (32-bit vs 64-bit, ANSI vs Unicode).
**Mitigation:** Use CMake + vcpkg for dependency management. Target x64 Unicode (modern NPP default). Document exact toolchain version.

### R5: Plugin distribution and auto-update
**Severity:** Low | **Likelihood:** Low
NPP Plugin Admin requires plugin submission and review. Users can also manually drop DLLs.
**Mitigation:** Phase 1 ships as manual DLL install. Submit to Plugin Admin after Phase 3 when plugin is stable enough for wide distribution.

## Domain Risks

### R6: HL7 v2.x version proliferation
**Severity:** Medium | **Likelihood:** Certain
Real-world HL7 traffic spans v2.1 through v2.8.2. The same segment (e.g., PID) has different field layouts across versions. PID-12 in v2.3 is "County Code"; in v2.2 it was something else entirely.
**Mitigation:** Start with v2.7 (most recent widely-deployed version). Add v2.5 (second most common). Punt on versions before v2.3 — their install base is minimal. Detect version from MSH-12 and load the correct table.

### R7: Non-standard HL7 dialects
**Severity:** High | **Likelihood:** High
Every hospital system has custom Z-segments, non-standard delimiter choices, and creative interpretations of the spec. The plugin must be lenient in what it accepts.
**Mitigation:** Validation is advisory, never blocking. Z-segments get generic styling. Unknown segment IDs display as "Unknown segment" in tooltips rather than erroring. The parser must never crash on malformed input.

### R8: Encoding (non-ASCII characters)
**Severity:** Medium | **Likelihood:** Medium
HL7 messages can contain UTF-8, ISO 8859-1, or other encodings. Patient names with diacritics (Müller, José) are common.
**Mitigation:** Scintilla handles multi-byte encodings natively. Ensure the lexer uses character-aware (not byte-aware) offset calculations.

## Unknowns Requiring Investigation

1. **NPP 8.x plugin API changes:** Has the plugin interface changed from 7.x? Need to check the 8.x SDK.
2. **High-DPI awareness:** NPP 8.x supports per-monitor DPI. Does the dockable panel API handle this correctly?
3. **Scintilla 5.x lexer model:** NPP 8.x ships Scintilla 5.x. Verify the ILexer5 interface for multi-threaded lexing.
4. **Performance on large files:** Can we lex a 50MB HL7 log file interactively? Need to benchmark incremental vs full-document lexing.
5. **Coexistence with other plugins:** Does the plugin conflict with XML Tools, Compare, or other popular NPP plugins that also hook Scintilla?
6. **HL7 field definition source licensing:** Can we legally embed segment definitions derived from the HL7 standard? (The standard itself is copyrighted by HL7 International. Field *names* and *positions* are likely factual data and not copyrightable, but this needs legal review.)

## Pre-Build Investigation Tasks

- [ ] Install NPP 8.x SDK, verify compile chain
- [ ] Build a hello-world plugin (DLL + menu item + message box)
- [ ] Test Scintilla `SCN_DWELLSTART` notification forwarding
- [ ] Test dockable panel registration API
- [ ] Benchmark: lex 10MB of HL7 text with a naive line-split loop
- [ ] Research: HL7 field definition data sources (hl7apy, HAPI, NHapi)
- [ ] Review: NPP Plugin Admin submission requirements
