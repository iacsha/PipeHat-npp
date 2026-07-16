// Standalone harness for MessageIndex -- message boundaries and per-message delimiters.
//
// Guards the gap where PipeHat had no concept of a message boundary: every consumer
// found the FIRST MSH, took delimiters from it, and applied them to the whole buffer.
// A log whose later messages declare different encoding characters was silently
// mis-parsed. Section [3] is that exact case.
//
// Build (from repo root, in a VS developer prompt):
//   cl /EHsc /std:c++17 /I src tests\MessageIndexTest.cpp src\HL7Lexer.cpp /Fe:build\MessageIndexTest.exe
//   build\MessageIndexTest.exe
//
// Exits non-zero on any failure so it can gate a release.

#include "MessageIndex.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    std::printf(cond ? "  [pass] %s\n" : "  [FAIL] %s\n", what.c_str());
    if (!cond) g_failures++;
}

static void checkInt(int got, int expect, const std::string& what) {
    if (got == expect) {
        std::printf("  [pass] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s (expected %d, got %d)\n", what.c_str(), expect, got);
        g_failures++;
    }
}

static void checkStr(const std::wstring& got, const std::wstring& expect, const std::string& what) {
    if (got == expect) {
        std::printf("  [pass] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s (expected \"%ls\", got \"%ls\")\n", what.c_str(),
                    expect.c_str(), got.c_str());
        g_failures++;
    }
}

static hl7::MessageIndex indexOf(const std::vector<std::wstring>& lines) {
    hl7::MessageIndex idx;
    idx.build((int)lines.size(), [&lines](int i) { return lines[(size_t)i]; });
    return idx;
}

int main() {
    int section = 0;

    std::printf("\n[%d] Single message\n", ++section);
    {
        std::vector<std::wstring> m = {
            L"MSH|^~\\&|SEND|FAC|RECV|FAC|20240101120000||ADT^A01|MSG001|P|2.5",
            L"EVN|A01|20240101120000",
            L"PID|1||123456^^^MRN||DOE^JOHN",
        };
        auto idx = indexOf(m);
        checkInt((int)idx.count(), 1, "one message found");
        checkInt(idx.at(0)->startLine, 0, "starts at line 0");
        checkInt(idx.at(0)->endLine, 2, "ends at the last line");
        checkStr(idx.at(0)->type, L"ADT^A01", "MSH-9 captured");
        checkStr(idx.at(0)->controlId, L"MSG001", "MSH-10 captured");
        checkInt(idx.indexAt(2), 0, "PID line maps to message 0");
    }

    std::printf("\n[%d] Many messages in one buffer (the log-file case)\n", ++section);
    {
        std::vector<std::wstring> m = {
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|MSG001|P|2.5",
            L"PID|1||111",
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A03|MSG002|P|2.5",
            L"PID|1||222",
            L"PV1|1|I",
            L"MSH|^~\\&|A|B|C|D|20240101||ORU^R01|MSG003|P|2.5",
            L"OBX|1|ST|X||val",
        };
        auto idx = indexOf(m);
        checkInt((int)idx.count(), 3, "three messages found");
        checkInt(idx.at(0)->endLine, 1, "message 0 ends before the next MSH");
        checkInt(idx.at(1)->startLine, 2, "message 1 starts at its own MSH");
        checkInt(idx.at(1)->endLine, 4, "message 1 spans to the line before message 2");
        checkInt(idx.at(2)->endLine, 6, "last message runs to EOF");
        checkStr(idx.at(2)->controlId, L"MSG003", "third control id");
        checkInt(idx.indexAt(4), 1, "PV1 on line 4 belongs to message 1");
        checkInt(idx.indexAt(6), 2, "OBX on line 6 belongs to message 2");
    }

    std::printf("\n[%d] Per-message delimiters (the silent mis-parse this fixes)\n", ++section);
    {
        // Message 2 legally declares '!' as its field separator. The old code read
        // delimiters from the first MSH only and applied '|' to the entire buffer.
        std::vector<std::wstring> m = {
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|MSG001|P|2.5",
            L"PID|1||111",
            L"MSH!^~\\&!A!B!C!D!20240101!!ADT^A04!MSG002!P!2.5",
            L"PID!1!!222",
        };
        auto idx = indexOf(m);
        checkInt((int)idx.count(), 2, "both messages found despite differing separators");
        check(idx.at(0)->delims.fieldSep == L'|', "message 0 keeps '|'");
        check(idx.at(1)->delims.fieldSep == L'!', "message 1 uses its own '!'");
        checkStr(idx.at(1)->type, L"ADT^A04", "MSH-9 parsed with the '!' separator");
        checkStr(idx.at(1)->controlId, L"MSG002", "MSH-10 parsed with the '!' separator");
        check(idx.delimitersFor(3).fieldSep == L'!', "line 3 resolves to '!', not the first message's '|'");
        check(idx.delimitersFor(1).fieldSep == L'|', "line 1 still resolves to '|'");
    }

    std::printf("\n[%d] Batch envelope (FHS/BHS/BTS/FTS sit outside messages)\n", ++section);
    {
        std::vector<std::wstring> m = {
            L"FHS|^~\\&|SEND|FAC",
            L"BHS|^~\\&|SEND|FAC",
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|MSG001|P|2.5",
            L"PID|1||111",
            L"BTS|1",
            L"FTS|1",
        };
        auto idx = indexOf(m);
        checkInt((int)idx.count(), 1, "envelope segments do not create messages");
        checkInt(idx.at(0)->startLine, 2, "message starts at MSH, not FHS");
        checkInt(idx.at(0)->endLine, 3, "message ends before BTS -- the trailer is not its field data");
        checkInt(idx.indexAt(0), -1, "FHS belongs to no message");
        checkInt(idx.indexAt(4), -1, "BTS belongs to no message");
        check(hl7::isEnvelopeSegment(L"FHS") && hl7::isEnvelopeSegment(L"FTS"), "envelope ids recognized");
        check(!hl7::isEnvelopeSegment(L"PID"), "PID is not an envelope segment");
    }

    std::printf("\n[%d] Navigation\n", ++section);
    {
        std::vector<std::wstring> m = {
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|MSG001|P|2.5",
            L"PID|1||111",
            L"MSH|^~\\&|A|B|C|D|20240101||ADT^A03|MSG002|P|2.5",
            L"PID|1||222",
        };
        auto idx = indexOf(m);
        checkInt(idx.nextStartLine(0), 2, "next from message 0 lands on message 1's MSH");
        checkInt(idx.nextStartLine(2), -1, "no next past the last message");
        checkInt(idx.prevStartLine(3), 2, "prev from inside message 1 lands on its own header");
        checkInt(idx.prevStartLine(2), 0, "prev from message 1's header lands on message 0");
        checkInt(idx.prevStartLine(0), -1, "no prev before the first message");
    }

    std::printf("\n[%d] Degenerate input never crashes or spans negatively\n", ++section);
    {
        auto empty = indexOf({});
        checkInt((int)empty.count(), 0, "empty buffer");
        check(empty.delimitersFor(0).fieldSep == L'|', "defaults when there is no message at all");

        auto none = indexOf({ L"just some prose", L"more prose" });
        checkInt((int)none.count(), 0, "prose produces no messages");

        auto tail = indexOf({ L"PID|1||111", L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|M|P|2.5" });
        checkInt((int)tail.count(), 1, "MSH on the final line");
        checkInt(tail.at(0)->endLine, 1, "final-line message does not span backwards");
        checkInt(tail.at(0)->lineCount(), 1, "spans exactly one line");
        checkInt(tail.indexAt(0), -1, "the orphan PID above it belongs to no message");

        auto trailerOnly = indexOf({ L"MSH|^~\\&|A|B|C|D|20240101||ADT^A01|M|P|2.5", L"BTS|1" });
        checkInt(trailerOnly.at(0)->endLine, 0, "MSH immediately followed by a trailer clamps to itself");
        checkInt(trailerOnly.at(0)->lineCount(), 1, "clamped span is one line, never negative");
    }

    std::printf("\n%s (%d failure%s)\n\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
