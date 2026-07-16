#include "HL7Lexer.h"
#include <cwctype>

namespace {

// Segment-ID character classes. Deliberately NOT iswalpha/iswalnum: those are
// locale-dependent and accept lowercase and accented Unicode letters, so prose
// ("The quick brown fox") parses as segment "The".
inline bool isSegAlpha(wchar_t c) { return c >= L'A' && c <= L'Z'; }
inline bool isSegAlnum(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'); }

} // namespace

HL7Lexer::HL7Lexer() {
}

void HL7Lexer::reset() {
    m_delimiters = HL7Delimiters{};
}

void HL7Lexer::parseMSH(const wchar_t* line, int lineLen) {
    if (!line || lineLen < 8) return;

    // MSH segment: "MSH|^~\&|..."
    // MSH-1 is the character immediately after "MSH"
    if (lineLen < 4) return;
    if (line[0] != L'M' || line[1] != L'S' || line[2] != L'H') return;

    // Field separator is character at position 3
    m_delimiters.fieldSep = line[3];

    // Encoding characters are at positions 4-7 (after field sep after MSH)
    // The MSH segment is special: "MSH" + fieldSep + encodingChars + fieldSep + ...
    // So encoding chars are at positions 4,5,6,7
    if (lineLen >= 8) {
        m_delimiters.compSep = line[4];
        m_delimiters.repeatSep = line[5];
        m_delimiters.escapeSep = line[6];
        m_delimiters.subcompSep = line[7];
    }
}

std::wstring HL7Lexer::extractSegmentID(const wchar_t* line, int lineLen) const {
    if (!line || lineLen < 3) return L"";

    int start = 0;

    // Skip whitespace
    while (start < lineLen && std::iswspace(line[start])) start++;

    // A segment ID is exactly three characters: an uppercase letter followed by two
    // uppercase-alphanumerics. The digits are NOT optional trivia -- PV1, NK1, GT1,
    // IN1/IN2, PD1, DG1 and PR1 all carry heavy PHI, and rejecting them here makes
    // cmdScrubPHI skip the whole line (it bails on an empty segment ID).
    if (start + 3 > lineLen) return L"";

    wchar_t a = line[start];
    wchar_t b = line[start + 1];
    wchar_t c = line[start + 2];

    if (!isSegAlpha(a) || !isSegAlnum(b) || !isSegAlnum(c)) return L"";

    // MSH is self-defining: MSH-1 *is* the field separator, so whatever character
    // follows "MSH" is the delimiter by definition. It must be accepted without a
    // delimiter check -- that acceptance is what lets parseMSH discover a
    // non-'|' separator before m_delimiters holds anything trustworthy.
    if (a == L'M' && b == L'S' && c == L'H') return L"MSH";

    // Every other segment ID must be delimited by the field separator (or end the
    // line). Without this, any three uppercase characters -- "THE QUICK BROWN FOX"
    // -- parse as a segment and can auto-activate the plugin on prose.
    if (start + 3 < lineLen && line[start + 3] != m_delimiters.fieldSep) return L"";

    return std::wstring{ a, b, c };
}

// Defined in terms of extractSegmentID so the two can never disagree. They
// previously duplicated the character test, and tokenize() (isSegmentStart) drifting
// from the PHI lookup (extractSegmentID) is exactly the kind of split that hides bugs.
bool HL7Lexer::isSegmentStart(const wchar_t* line, int lineLen) const {
    return !extractSegmentID(line, lineLen).empty();
}

int HL7Lexer::getFieldIndexAtPosition(const wchar_t* line, int lineLen, int charPos) const {
    if (!line || lineLen == 0 || charPos < 0 || charPos >= lineLen) return -1;

    // Skip leading whitespace
    int pos = 0;
    while (pos < lineLen && std::iswspace(line[pos])) pos++;

    // MSH-1 is the field separator itself, so the first value after it is MSH-2.
    bool isMSH = (pos + 3 <= lineLen &&
                  line[pos] == L'M' && line[pos + 1] == L'S' && line[pos + 2] == L'H');

    // If position is in the segment ID area
    if (isSegmentStart(line + pos, lineLen - pos) && charPos >= pos && charPos < pos + 3) {
        return 0; // 0 means segment ID
    }

    // If position is at the first field separator (MSH special case)
    if (isSegmentStart(line + pos, lineLen - pos)) {
        pos += 3; // skip segment ID
        if (pos < lineLen && line[pos] == m_delimiters.fieldSep) {
            pos++; // skip first field sep
        }
    }

    // Count fields (1-based). For MSH the first value is MSH-2, so start at 2.
    int fieldCount = isMSH ? 2 : 1;

    while (pos < lineLen) {
        if (charPos < pos) break; // Past our position

        wchar_t ch = line[pos];

        if (ch == m_delimiters.fieldSep) {
            if (charPos == pos) return fieldCount; // On the separator, return prev field
            fieldCount++;
            pos++;
        } else if (ch == m_delimiters.escapeSep) {
            // Mirror tokenize(): an escape never crosses a field separator. Scan for
            // a closing escape, but stop at a field separator or EOL so field
            // counting stays correct on MSH-2 ("^~\&") and stray-backslash data.
            int scan = pos + 1;
            bool closed = false;
            while (scan < lineLen) {
                wchar_t sc = line[scan];
                if (sc == m_delimiters.fieldSep || sc == L'\r' || sc == L'\n') break;
                if (sc == m_delimiters.escapeSep) { closed = true; break; }
                scan++;
            }
            pos = closed ? (scan + 1) : (pos + 1);
        } else if (ch == L'\r' || ch == L'\n') {
            break;
        } else {
            pos++;
        }
    }

    return fieldCount;
}

void HL7Lexer::tokenize(const wchar_t* line, int lineLen, std::vector<HL7Token>& tokens) {
    tokens.clear();
    if (!line || lineLen == 0) return;

    int pos = 0;

    // Skip leading whitespace
    while (pos < lineLen && std::iswspace(line[pos])) {
        pos++;
    }

    // Check if this line is a segment start
    bool isSegment = isSegmentStart(line + pos, lineLen - pos);

    if (isSegment) {
        // Emit segment ID token (3 chars)
        tokens.push_back({ pos, 3, HL7TokenType::SEGMENT_ID });
        pos += 3;

        // The character after the segment ID is the field separator
        if (pos < lineLen && line[pos] == m_delimiters.fieldSep) {
            tokens.push_back({ pos, 1, HL7TokenType::FIELD_SEP });
            pos++;
        }
    }

    // Tokenize remaining content as field values and separators
    int componentDepth = 0;

    while (pos < lineLen) {
        wchar_t ch = line[pos];

        if (ch == m_delimiters.fieldSep) {
            tokens.push_back({ pos, 1, HL7TokenType::FIELD_SEP });
            componentDepth = 0;
            pos++;

        } else if (ch == m_delimiters.compSep) {
            tokens.push_back({ pos, 1, HL7TokenType::COMPONENT_SEP });
            componentDepth = 1;
            pos++;

        } else if (ch == m_delimiters.repeatSep) {
            tokens.push_back({ pos, 1, HL7TokenType::REPEAT_SEP });
            componentDepth = 0;
            pos++;

        } else if (ch == m_delimiters.subcompSep) {
            tokens.push_back({ pos, 1, HL7TokenType::SUBCOMP_SEP });
            componentDepth = 2;
            pos++;

        } else if (ch == m_delimiters.escapeSep) {
            // A valid HL7 escape sequence is \...\ and NEVER contains a field
            // separator. Scan for a closing escape char, but stop at a field
            // separator or EOL. If none is found first, this backslash is not a
            // real escape (e.g. the '\' in the MSH-2 encoding chars "^~\&", or a
            // stray backslash in data) -- treat it as a literal value character so
            // field boundaries are never miscounted. Fail-closed: field structure
            // is preserved no matter how malformed the escape is.
            int scan = pos + 1;
            bool closed = false;
            while (scan < lineLen) {
                wchar_t sc = line[scan];
                if (sc == m_delimiters.fieldSep || sc == L'\r' || sc == L'\n') break;
                if (sc == m_delimiters.escapeSep) { closed = true; break; }
                scan++;
            }
            if (closed) {
                int escapeLen = (scan + 1) - pos;
                tokens.push_back({ pos, escapeLen, HL7TokenType::ESCAPE_SEQ });
                pos = scan + 1;
            } else {
                tokens.push_back({ pos, 1, HL7TokenType::FIELD_VALUE });
                pos++;
            }

        } else if (ch == L'\r' || ch == L'\n') {
            // End of line -- skip
            pos++;

        } else {
            // Field value -- accumulate until next delimiter
            int valueStart = pos;
            while (pos < lineLen) {
                wchar_t c = line[pos];
                if (c == m_delimiters.fieldSep ||
                    c == m_delimiters.compSep ||
                    c == m_delimiters.repeatSep ||
                    c == m_delimiters.subcompSep ||
                    c == m_delimiters.escapeSep ||
                    c == L'\r' || c == L'\n') {
                    break;
                }
                pos++;
            }
            int valueLen = pos - valueStart;
            if (valueLen > 0) {
                tokens.push_back({ valueStart, valueLen, HL7TokenType::FIELD_VALUE });
            }
        }
    }
}
