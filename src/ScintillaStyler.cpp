#include "ScintillaStyler.h"
#include "HL7Lexer.h"
#include "SegmentDB.h"
#include "PluginDefs.h"
#include "SciUtils.h"
#include "TriggerEventDB.h"
#include "HL7Escape.h"
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>

ScintillaStyler::ScintillaStyler() {}

void ScintillaStyler::init(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect) {
    m_hScintilla = hScintilla;
    m_sciFn = fnDirect;
    m_sciPtr = ptrDirect;
}

sptr_t ScintillaStyler::sci(unsigned int msg, uptr_t wParam, sptr_t lParam) {
    if (m_sciFn) return m_sciFn(m_sciPtr, msg, wParam, lParam);
    return 0;
}

void ScintillaStyler::sciV(unsigned int msg, uptr_t wParam, sptr_t lParam) {
    sci(msg, wParam, lParam);
}

int ScintillaStyler::sciGetLength() { return (int)sci(SCI_GETLENGTH); }
int ScintillaStyler::sciGetLineCount() { return (int)sci(SCI_GETLINECOUNT); }
int ScintillaStyler::sciGetLineEnd(int line) { return (int)sci(SCI_GETLINEENDPOSITION, line); }

void ScintillaStyler::defineStyles() {
    sciV(SCI_STYLECLEARALL);

    sciV(SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);
    sciV(SCI_STYLESETBACK, STYLE_DEFAULT, 0xFFFFFF);
    sciV(SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
    sciV(SCI_STYLESETFONT, STYLE_DEFAULT, (sptr_t)"Consolas");

    sciV(SCI_STYLESETFORE, SCE_HL7_SEGMENT_ID, 0x0000CC);
    sciV(SCI_STYLESETBOLD, SCE_HL7_SEGMENT_ID, 1);

    sciV(SCI_STYLESETFORE, SCE_HL7_FIELD_SEP, 0x888888);
    sciV(SCI_STYLESETFORE, SCE_HL7_COMPONENT_SEP, 0xAAAAAA);
    sciV(SCI_STYLESETFORE, SCE_HL7_REPEAT_SEP, 0xBBBBBB);
    sciV(SCI_STYLESETFORE, SCE_HL7_ESCAPE_SEP, 0x888888);
    sciV(SCI_STYLESETFORE, SCE_HL7_SUBCOMP_SEP, 0xCCCCCC);

    sciV(SCI_STYLESETFORE, SCE_HL7_ESCAPE_SEQ, 0x008888);
    sciV(SCI_STYLESETBOLD, SCE_HL7_ESCAPE_SEQ, 1);

    sciV(SCI_STYLESETFORE, SCE_HL7_FIELD_VALUE, 0x000000);

    enableTooltips(true);
}

static std::string toUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), &result[0], len, nullptr, nullptr);
    return result;
}

void ScintillaStyler::enableTooltips(bool enable) {
    sciV(SCI_SETMOUSEDWELLTIME, enable ? 400 : 10000000);
}

void ScintillaStyler::showFieldTooltip(int position, HL7Lexer& lexer, SegmentDB& segDB) {
    if (!m_hScintilla || position < 0) return;

    int line = (int)sci(SCI_LINEFROMPOSITION, position);

    std::string lineU8 = getLineUtf8(m_sciFn, m_sciPtr, line);
    std::wstring wline = utf8ToW(lineU8);
    int wlen = (int)wline.size();
    if (wlen <= 0) return;

    std::wstring segId = lexer.extractSegmentID(wline.c_str(), wlen);
    if (segId.empty()) return;

    // Get UTF-8 byte position within the line, clamped to the (EOL-stripped) content
    int lineStart = (int)sci(SCI_POSITIONFROMLINE, line);
    int byteOffset = position - lineStart;
    if (byteOffset < 0) byteOffset = 0;
    if (byteOffset > (int)lineU8.size()) byteOffset = (int)lineU8.size();

    // Convert byte offset to wchar_t offset
    int wcharOffset = MultiByteToWideChar(CP_UTF8, 0, lineU8.data(), byteOffset, nullptr, 0);
    if (wcharOffset < 0) wcharOffset = byteOffset; // fallback

    int fieldIdx = lexer.getFieldIndexAtPosition(wline.c_str(), wlen, wcharOffset);

    char tipText[512];
    const HL7SegmentDef* segDef = segDB.lookup(segId);

    if (fieldIdx <= 0) {
        // Hovering over segment ID
        if (segDef) {
            snprintf(tipText, sizeof(tipText), "%s \xE2\x96\xB6 %s",
                     toUtf8(segId).c_str(), toUtf8(segDef->name).c_str());
        } else {
            snprintf(tipText, sizeof(tipText), "%s \xE2\x96\xB6 Unknown segment",
                     toUtf8(segId).c_str());
        }
    } else {
        const HL7FieldDef* fieldDef = segDB.lookupField(segId, fieldIdx);
        if (fieldDef) {
            snprintf(tipText, sizeof(tipText), "%s-%d: %s  [%s%s]",
                     toUtf8(segId).c_str(), fieldIdx,
                     toUtf8(fieldDef->name).c_str(),
                     toUtf8(fieldDef->dataType).c_str(),
                     fieldDef->required ? ", Required" : "");
        } else if (segDef) {
            snprintf(tipText, sizeof(tipText), "%s-%d: (field %d of %s)",
                     toUtf8(segId).c_str(), fieldIdx,
                     fieldIdx, toUtf8(segDef->name).c_str());
        } else {
            snprintf(tipText, sizeof(tipText), "%s-%d", toUtf8(segId).c_str(), fieldIdx);
        }

        // Decode coded values (MSH-9 message type / trigger event, EVN-1 event) onto
        // a second tooltip line so the reader sees "A01" *and* what it means.
        std::wstring decoded = hl7trig::decodeField(segId, fieldIdx, wline,
                                                    lexer.delimiters().fieldSep,
                                                    lexer.delimiters().compSep);
        if (!decoded.empty()) {
            std::string d = toUtf8(decoded);
            strncat_s(tipText, sizeof(tipText), "\r\n", _TRUNCATE);
            strncat_s(tipText, sizeof(tipText), d.c_str(), _TRUNCATE);
        }

        // If the field carries HL7 escape sequences, show the decoded text. Skip
        // MSH-1/MSH-2 (the delimiter-definition fields, where '\' is data, not an escape).
        const HL7Delimiters& dl = lexer.delimiters();
        bool isMSH = (segId == L"MSH");
        if (!(isMSH && fieldIdx <= 2)) {
            std::wstring raw = hl7trig::fieldValueAt(wline, dl.fieldSep, isMSH, fieldIdx);
            if (hl7esc::containsEscape(raw, dl.escapeSep)) {
                std::wstring dec = hl7esc::decode(raw, dl.escapeSep, dl.fieldSep,
                                                  dl.compSep, dl.repeatSep, dl.subcompSep);
                if (dec != raw) {
                    std::string d = toUtf8(L"Decoded: " + dec);
                    strncat_s(tipText, sizeof(tipText), "\r\n", _TRUNCATE);
                    strncat_s(tipText, sizeof(tipText), d.c_str(), _TRUNCATE);
                }
            }
        }
    }

    if (tipText[0]) {
        sciV(SCI_CALLTIPSHOW, position + 1, (sptr_t)tipText);
    }
}

void ScintillaStyler::hideTooltip() {
    sciV(SCI_CALLTIPCANCEL);
}

void ScintillaStyler::styleAll() {
    int length = sciGetLength();
    styleRange(0, length);
}

void ScintillaStyler::styleRange(int startPos, int endPos) {
    if (endPos <= startPos) return;

    int startLine = (int)sci(SCI_LINEFROMPOSITION, startPos);
    int endLine = (int)sci(SCI_LINEFROMPOSITION, endPos);

    HL7Lexer lexer;
    bool mshFound = false;

    for (int line = 0; line <= sciGetLineCount(); line++) {
        std::wstring wline = getLineW(m_sciFn, m_sciPtr, line);
        if (wline.empty()) continue;
        if (lexer.extractSegmentID(wline.c_str(), (int)wline.size()) == L"MSH") {
            lexer.parseMSH(wline.c_str(), (int)wline.size());
            mshFound = true;
            break;
        }
    }

    if (!mshFound) return;

    sciV(SCI_STARTSTYLING, startPos);

    for (int line = startLine; line <= endLine; line++) {
        // Total bytes on the line including EOL — everything must be styled so the
        // styling cursor stays byte-aligned with the document.
        int lineBytes = (int)sci(SCI_LINELENGTH, line);
        int styledBytes = 0;

        std::wstring wline = getLineW(m_sciFn, m_sciPtr, line);
        if (!wline.empty()) {
            std::vector<HL7Token> tokens;
            lexer.tokenize(wline.c_str(), (int)wline.size(), tokens);

            for (const auto& token : tokens) {
                int style = SCE_HL7_DEFAULT;
                switch (token.type) {
                    case HL7TokenType::SEGMENT_ID:   style = SCE_HL7_SEGMENT_ID; break;
                    case HL7TokenType::FIELD_SEP:    style = SCE_HL7_FIELD_SEP; break;
                    case HL7TokenType::COMPONENT_SEP: style = SCE_HL7_COMPONENT_SEP; break;
                    case HL7TokenType::REPEAT_SEP:   style = SCE_HL7_REPEAT_SEP; break;
                    case HL7TokenType::ESCAPE_SEQ:   style = SCE_HL7_ESCAPE_SEQ; break;
                    case HL7TokenType::SUBCOMP_SEP:  style = SCE_HL7_SUBCOMP_SEP; break;
                    case HL7TokenType::FIELD_VALUE:  style = SCE_HL7_FIELD_VALUE; break;
                }

                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wline.c_str() + token.startPos, token.length, nullptr, 0, nullptr, nullptr);
                if (utf8Len <= 0) utf8Len = token.length;

                sciV(SCI_SETSTYLING, utf8Len, style);
                styledBytes += utf8Len;
            }
        }

        // Style any remaining bytes (inter-token gaps + EOL) as default so the
        // cumulative styled length matches the document byte length exactly.
        if (lineBytes > styledBytes) {
            sciV(SCI_SETSTYLING, lineBytes - styledBytes, SCE_HL7_DEFAULT);
        }
    }
}

bool ScintillaStyler::detectHL7(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect) {
    (void)hScintilla;
    std::string content = getLineUtf8(fnDirect, ptrDirect, 0);
    return content.size() >= 3 && content.compare(0, 3, "MSH") == 0;
}
