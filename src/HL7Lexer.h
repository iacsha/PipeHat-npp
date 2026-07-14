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

class HL7Lexer {
public:
    HL7Lexer();

    void parseMSH(const wchar_t* line, int lineLen);
    void tokenize(const wchar_t* line, int lineLen, std::vector<HL7Token>& tokens);
    std::wstring extractSegmentID(const wchar_t* line, int lineLen) const;

    // Find which 1-based field index a character position falls within
    // Returns 0 if position is within a segment ID, -1 if not determinable
    int getFieldIndexAtPosition(const wchar_t* line, int lineLen, int charPos) const;

    const HL7Delimiters& delimiters() const { return m_delimiters; }
    void reset();

private:
    HL7Delimiters m_delimiters;
    bool isSegmentStart(const wchar_t* line, int lineLen) const;
};
