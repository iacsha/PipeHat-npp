#pragma once
//
// MessageIndex -- the authoritative map of "which message is this line in, and what
// delimiters does that message use".
//
// WHY THIS EXISTS
//
// A buffer is not one message. Interface engineers open logs and batch files holding
// hundreds. Before this, every consumer (styler, tree, scrub, conformance, validate)
// ran its own "scan for the first MSH, parseMSH, done" loop -- eight independent
// derivations of the same fact -- and then applied THAT message's delimiters to the
// entire buffer. A mixed-vendor log whose 200th message declares different encoding
// characters was silently mis-parsed.
//
// Eight consumers independently deriving one fact is exactly the shape of C6 (see
// docs/05-CODE-REVIEW.md), where the scrub pass and its own safety net each derived a
// segment ID and were free to disagree. So the fix is not "teach each consumer about
// boundaries" -- it is to compute boundaries ONCE, here, and have everyone ask.
//
// Pure and header-only (like MllpProtocol.h): no Scintilla, no Win32. `build` is
// templated over a line accessor so it runs against the editor in the plugin and
// against a vector<wstring> in tests/MessageIndexTest.cpp.
//
#include "HL7Lexer.h"
#include "TriggerEventDB.h"

#include <string>
#include <vector>

namespace hl7 {

// One message: an MSH line and every line up to (not including) the next boundary.
struct MessageSpan {
    int startLine = 0;          // the MSH line
    int endLine = 0;            // inclusive
    HL7Delimiters delims;       // read from THIS message's MSH, never a neighbour's
    std::wstring type;          // MSH-9 raw, e.g. "ADT^A01"
    std::wstring controlId;     // MSH-10

    bool contains(int line) const { return line >= startLine && line <= endLine; }
    int lineCount() const { return endLine - startLine + 1; }
};

// Batch envelope segments. These sit OUTSIDE any message span: FHS/BHS open a file or
// batch, BTS/FTS close one. Treating a trailer as part of the preceding message would
// hand its fields to the PHI map under that message's delimiters.
inline bool isEnvelopeSegment(const std::wstring& segId) {
    return segId == L"FHS" || segId == L"BHS" || segId == L"BTS" || segId == L"FTS";
}

class MessageIndex {
public:
    // getLine(i) -> std::wstring for line i. Templated so the plugin passes a
    // Scintilla reader and the test passes a vector.
    template <class GetLine>
    void build(int lineCount, GetLine getLine) {
        m_spans.clear();
        if (lineCount <= 0) return;

        HL7Lexer lex;              // delimiter state walks forward with the scan
        int openIdx = -1;          // index into m_spans of the span still being closed

        for (int li = 0; li < lineCount; li++) {
            std::wstring wl = getLine(li);
            if (wl.size() < 3) continue;

            // extractSegmentID special-cases MSH and returns it regardless of the
            // delimiters currently loaded -- MSH-1 IS the field separator, so an MSH
            // is recognizable before we know anything about this message. That is what
            // makes a forward single-pass scan possible at all.
            std::wstring segId = lex.extractSegmentID(wl.c_str(), (int)wl.size());
            if (segId.empty()) continue;

            if (segId == L"MSH") {
                if (openIdx >= 0) m_spans[openIdx].endLine = li - 1;

                lex.parseMSH(wl.c_str(), (int)wl.size());   // delimiters for THIS message

                MessageSpan s;
                s.startLine = li;
                s.endLine   = lineCount - 1;                // provisional; closed below
                s.delims    = lex.delimiters();
                // hl7trig::fieldValueAt already honors the MSH off-by-one (MSH-1 IS
                // the field separator), so MSH-9/MSH-10 land correctly.
                s.type      = hl7trig::fieldValueAt(wl, s.delims.fieldSep, true, 9);
                s.controlId = hl7trig::fieldValueAt(wl, s.delims.fieldSep, true, 10);
                m_spans.push_back(s);
                openIdx = (int)m_spans.size() - 1;
                continue;
            }

            // A trailer closes the message that precedes it; the trailer line itself
            // belongs to no message.
            if (openIdx >= 0 && (segId == L"BTS" || segId == L"FTS")) {
                m_spans[openIdx].endLine = li - 1;
                openIdx = -1;
            }
        }

        if (openIdx >= 0) m_spans[openIdx].endLine = lineCount - 1;

        // A message opened on the buffer's last line, or immediately followed by a
        // trailer, can compute endLine < startLine. Clamp so contains()/lineCount()
        // never go negative.
        for (auto& s : m_spans)
            if (s.endLine < s.startLine) s.endLine = s.startLine;
    }

    size_t count() const { return m_spans.size(); }
    bool empty() const { return m_spans.empty(); }
    const std::vector<MessageSpan>& spans() const { return m_spans; }

    const MessageSpan* at(size_t i) const {
        return i < m_spans.size() ? &m_spans[i] : nullptr;
    }

    // The message containing `line`, or nullptr when the line is envelope/preamble.
    const MessageSpan* messageAt(int line) const {
        int i = indexAt(line);
        return i < 0 ? nullptr : &m_spans[(size_t)i];
    }

    // 0-based index of the message containing `line`; -1 if outside every message.
    int indexAt(int line) const {
        for (size_t i = 0; i < m_spans.size(); i++)
            if (m_spans[i].contains(line)) return (int)i;
        return -1;
    }

    // Delimiters to use when reading `line`. Falls back to the nearest PRECEDING
    // message for envelope/preamble lines, and to HL7 defaults when there is none.
    // Callers must never reach for "the first message's delimiters" again -- that
    // habit is the bug this class exists to remove.
    HL7Delimiters delimitersFor(int line) const {
        if (const MessageSpan* s = messageAt(line)) return s->delims;
        const MessageSpan* best = nullptr;
        for (const auto& s : m_spans) {
            if (s.startLine > line) break;
            best = &s;
        }
        return best ? best->delims : HL7Delimiters{};
    }

    // Navigation. Both return -1 when there is nowhere to go.
    int nextStartLine(int fromLine) const {
        for (const auto& s : m_spans)
            if (s.startLine > fromLine) return s.startLine;
        return -1;
    }
    int prevStartLine(int fromLine) const {
        int found = -1;
        for (const auto& s : m_spans) {
            if (s.startLine >= fromLine) break;
            found = s.startLine;
        }
        // Sitting inside message N (but past its MSH) should step back to N's own
        // header first, which the loop above already yields.
        return found;
    }

private:
    std::vector<MessageSpan> m_spans;
};

} // namespace hl7
