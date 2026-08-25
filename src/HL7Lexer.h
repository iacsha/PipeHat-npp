#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct HL7Delimiters {
    wchar_t fieldSep = L'|';
    wchar_t compSep = L'^';
    wchar_t repeatSep = L'~';
    wchar_t escapeSep = L'\\';
    wchar_t subcompSep = L'&';
};

enum class HL7TokenType {
    SEGMENT_ID,
    FIELD_SEP,
    FIELD_VALUE,
    COMPONENT_SEP,
    REPEAT_SEP,
    ESCAPE_SEQ,
    SUBCOMP_SEP
};

struct HL7Token {
    int startPos;          // wchar_t position in line
    int length;            // wchar_t length
    HL7TokenType type;
};

// Full HL7 address of a character position: SEG-field[repeat].component.subcomponent.
//
// component/subcomponent are 0 when the enclosing level carries no separator at all,
// which is the difference between "NK1-3" and "NK1-3.1". HL7 treats those as the same
// value, but a reader hovering a field with no carets in it does not want a ".1"
// appended -- it reads as though a component was found.
struct HL7FieldPath {
    int field = -1;        // -1 not determinable, 0 = inside the segment ID
    int repeat = 1;        // 1-based repetition (~)
    int component = 0;     // 1-based, 0 = field has no component separator
    int subcomponent = 0;  // 1-based, 0 = component has no subcomponent separator
};

class HL7Lexer {
public:
    HL7Lexer();

    void parseMSH(const wchar_t* line, int lineLen);
    void tokenize(const wchar_t* line, int lineLen, std::vector<HL7Token>& tokens);
    std::wstring extractSegmentID(const wchar_t* line, int lineLen) const;

    // Resolve a character position to its full HL7 path. This is the single
    // derivation; getFieldIndexAtPosition is a thin wrapper over it so the tree,
    // PHI scrub, conformance check and tooltip can never disagree about where a
    // caret is.
    HL7FieldPath getPathAtPosition(const wchar_t* line, int lineLen, int charPos) const;

    // Find which 1-based field index a character position falls within
    // Returns 0 if position is within a segment ID, -1 if not determinable
    int getFieldIndexAtPosition(const wchar_t* line, int lineLen, int charPos) const;

    const HL7Delimiters& delimiters() const { return m_delimiters; }

    // Load delimiters resolved elsewhere -- in practice from MessageIndex, which knows
    // which message a line belongs to. A buffer can hold many messages, each declaring
    // its own separators, so "parseMSH the first MSH and reuse it" is wrong for any
    // multi-message file. Prefer MessageIndex::delimitersFor(line) over parseMSH when
    // walking a buffer.
    void setDelimiters(const HL7Delimiters& d) { m_delimiters = d; }

    void reset();

private:
    HL7Delimiters m_delimiters;
    bool isSegmentStart(const wchar_t* line, int lineLen) const;
};
