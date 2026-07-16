// Standalone regression harness for HL7 segment-ID detection and PHI scrub coverage.
//
// Guards the defect where extractSegmentID required three ALPHA characters, so every
// segment carrying a digit (PV1, NK1, GT1, IN1/IN2, PD1, DG1, PR1, PV2) returned an
// empty ID. cmdScrubPHI bails on an empty segment ID, so those lines were skipped
// entirely -- next-of-kin names, guarantor SSN/DOB and insurance IDs survived a scrub
// that reported "no coverage gaps". Nine of the scrubber's 24 mapped segments were
// affected; PID is all-alpha, which is why the leak looked like a working scrubber.
//
// Build (from repo root, in a VS developer prompt):
//   cl /EHsc /std:c++17 /I src tests\SegmentIDTest.cpp src\HL7Lexer.cpp src\PHIScrubber.cpp /Fe:build\SegmentIDTest.exe
//   build\SegmentIDTest.exe
//
// Exits non-zero on any failure so it can gate a release.

#include "HL7Lexer.h"
#include "PHIScrubber.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) {
        std::printf("  [pass] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s\n", what.c_str());
        g_failures++;
    }
}

static void checkSegId(HL7Lexer& lex, const std::wstring& line,
                       const std::wstring& expect, const std::string& what) {
    std::wstring got = lex.extractSegmentID(line.c_str(), (int)line.size());
    bool ok = (got == expect);
    if (!ok) {
        std::printf("  [FAIL] %s (expected \"%ls\", got \"%ls\")\n",
                    what.c_str(), expect.c_str(), got.c_str());
        g_failures++;
    } else {
        std::printf("  [pass] %s\n", what.c_str());
    }
}

// Mirrors the segment gate + field walk in cmdScrubPHI: derive the segment ID, bail if
// empty, tokenize, and count FIELD_SEP tokens to index fields. Returns the (segment,
// field) pairs the scrubber would actually replace.
static std::vector<std::pair<std::wstring, int>> fieldsScrubbed(
        HL7Lexer& lex, const PHIScrubber& phi, const std::vector<std::wstring>& msg) {
    std::vector<std::pair<std::wstring, int>> out;

    for (const auto& wl : msg) {
        if (wl.empty()) continue;
        if (lex.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH")
            lex.parseMSH(wl.c_str(), (int)wl.size());
    }

    for (const auto& wl : msg) {
        if (wl.empty()) continue;
        std::wstring segId = lex.extractSegmentID(wl.c_str(), (int)wl.size());
        if (segId.empty()) continue;  // the line that silently dropped NK1/GT1/PV1

        std::vector<HL7Token> tokens;
        lex.tokenize(wl.c_str(), (int)wl.size(), tokens);

        int fieldIdx = (segId == L"MSH") ? 1 : 0;
        for (const auto& tok : tokens) {
            if (tok.type == HL7TokenType::FIELD_SEP) {
                fieldIdx++;
            } else if (tok.type == HL7TokenType::FIELD_VALUE && tok.length > 0) {
                if (phi.isPHI(segId, fieldIdx))
                    out.push_back({ segId, fieldIdx });
            }
        }
    }
    return out;
}

static bool scrubbed(const std::vector<std::pair<std::wstring, int>>& v,
                     const std::wstring& seg, int field) {
    for (const auto& p : v)
        if (p.first == seg && p.second == field) return true;
    return false;
}

int main() {
    int section = 0;

    std::printf("\n[%d] Segment IDs containing digits are recognized\n", ++section);
    {
        HL7Lexer lex;
        checkSegId(lex, L"PID|1||123456^^^MRN||DOE^JOHN", L"PID", "PID (all alpha, the case that always worked)");
        checkSegId(lex, L"PV1|1|I|ICU^101^A||||1234^WELBY^MARCUS", L"PV1", "PV1 (attending/referring/admitting doctors)");
        checkSegId(lex, L"NK1|1|DOE^JANE|SPO||555-0100", L"NK1", "NK1 (next-of-kin name, address, phone)");
        checkSegId(lex, L"GT1|1||DOE^JOHN||||19700101|||||123-45-6789", L"GT1", "GT1 (guarantor SSN and DOB)");
        checkSegId(lex, L"IN1|1|PLAN|ACME01|ACME INSURANCE", L"IN1", "IN1 (insurance company and policy IDs)");
        checkSegId(lex, L"IN2|1|123456789", L"IN2", "IN2");
        checkSegId(lex, L"PD1|||CLINIC^^^^^^XX|1234^WELBY^MARCUS", L"PD1", "PD1 (primary care provider)");
        checkSegId(lex, L"DG1|1|I10|E11.9^Type 2 diabetes", L"DG1", "DG1");
        checkSegId(lex, L"PR1|1|I10P|0016070^BYPASS", L"PR1", "PR1");
        checkSegId(lex, L"PV2|||||||||20240101", L"PV2", "PV2");
        checkSegId(lex, L"ZAL|1|CUSTOM", L"ZAL", "ZAL (Z-segment, scrubbed wholesale)");
    }

    std::printf("\n[%d] Non-segments are rejected (no false activation on prose)\n", ++section);
    {
        HL7Lexer lex;
        checkSegId(lex, L"The quick brown fox jumps", L"", "lowercase prose is not segment \"The\"");
        checkSegId(lex, L"THE QUICK BROWN FOX", L"", "uppercase prose without a field separator");
        checkSegId(lex, L"1PID|1||", L"", "segment ID must start with a letter, not a digit");
        checkSegId(lex, L"PIDX|1||", L"", "four-character ID is not a segment");
        checkSegId(lex, L"", L"", "empty line");
        checkSegId(lex, L"PI", L"", "short line");
        checkSegId(lex, L"PID", L"PID", "bare segment ID at end of line is valid");
    }

    std::printf("\n[%d] MSH is self-defining (MSH-1 IS the field separator)\n", ++section);
    {
        HL7Lexer lex;
        checkSegId(lex, L"MSH|^~\\&|SEND|FAC|RECV|FAC|20240101120000||ADT^A01|MSG1|P|2.5",
                   L"MSH", "MSH with the conventional '|' separator");

        // A non-'|' separator is legal HL7. MSH must be accepted before parseMSH has
        // run, or the separator can never be discovered and the whole file fails.
        HL7Lexer odd;
        checkSegId(odd, L"MSH!^~\\&!SEND!FAC", L"MSH", "MSH with a non-'|' separator, before parseMSH");
        odd.parseMSH(L"MSH!^~\\&!SEND!FAC", 17);
        check(odd.delimiters().fieldSep == L'!', "parseMSH discovers the '!' field separator");
        checkSegId(odd, L"PID!1!!123456", L"PID", "PID delimited by the discovered '!' separator");
        checkSegId(odd, L"PID|1||123456", L"", "PID with the wrong separator is rejected");
    }

    std::printf("\n[%d] tokenize() agrees with extractSegmentID on what is a segment\n", ++section);
    {
        // isSegmentStart is private, so probe it through its observable effect: a line
        // the lexer accepts must emit a SEGMENT_ID token. When the two disagreed,
        // digit-bearing segments got no SEGMENT_ID token, which both dropped their
        // colouring and shifted every field index on the line by one.
        HL7Lexer lex;
        auto emitsSegmentId = [&lex](const wchar_t* l) {
            std::vector<HL7Token> tokens;
            int n = (int)std::wstring(l).size();
            lex.tokenize(l, n, tokens);
            for (const auto& t : tokens)
                if (t.type == HL7TokenType::SEGMENT_ID) return true;
            return false;
        };

        const wchar_t* lines[] = { L"PID|1||X", L"NK1|1|DOE^JANE", L"GT1|1||DOE^JOHN",
                                   L"PV1|1|I|ICU", L"The quick brown fox",
                                   L"THE QUICK BROWN FOX", L"MSH|^~\\&|A|B" };
        for (const wchar_t* l : lines) {
            int n = (int)std::wstring(l).size();
            bool tokenized = emitsSegmentId(l);
            bool extracted = !lex.extractSegmentID(l, n).empty();
            std::wstring w(l);
            check(tokenized == extracted,
                  std::string("agreement on line: ") + std::string(w.begin(), w.end()));
        }
    }

    std::printf("\n[%d] PHI scrub coverage: the digit-bearing segments are reached\n", ++section);
    {
        HL7Lexer lex;
        PHIScrubber phi;

        // A realistic ADT^A01 carrying PHI in every previously-skipped segment.
        std::vector<std::wstring> msg = {
            L"MSH|^~\\&|SEND|FAC|RECV|FAC|20240101120000||ADT^A01|MSG00001|P|2.5",
            L"EVN|A01|20240101120000",
            L"PID|1||123456^^^MRN||DOE^JOHN^Q||19700101|M|||1 MAIN ST^^ROCHESTER^NY^14624||555-0100|||||ACCT9|123-45-6789",
            L"PD1|||CLINIC^^^^^^XX|1234^WELBY^MARCUS",
            L"NK1|1|DOE^JANE^R|SPO|2 MAIN ST^^ROCHESTER^NY^14624|555-0101",
            L"PV1|1|I|ICU^101^A||||1234^WELBY^MARCUS|5678^CASEY^BEN|||||||||9012^KILDARE^JAMES||V9999",
            L"DG1|1|I10|E11.9^Type 2 diabetes",
            L"GT1|1||DOE^JOHN^Q||3 MAIN ST^^ROCHESTER^NY^14624|555-0102||19700101||||123-45-6789",
            L"IN1|1|PLAN01|ACME01|ACME INSURANCE",
        };

        auto hit = fieldsScrubbed(lex, phi, msg);

        check(scrubbed(hit, L"PID", 5),  "PID-5  patient name        (the case that always worked)");
        check(scrubbed(hit, L"PID", 19), "PID-19 patient SSN");
        check(scrubbed(hit, L"PD1", 4),  "PD1-4  primary care provider");
        check(scrubbed(hit, L"NK1", 2),  "NK1-2  next-of-kin name");
        check(scrubbed(hit, L"NK1", 4),  "NK1-4  next-of-kin address");
        check(scrubbed(hit, L"NK1", 5),  "NK1-5  next-of-kin phone");
        check(scrubbed(hit, L"PV1", 7),  "PV1-7  attending doctor");
        check(scrubbed(hit, L"PV1", 8),  "PV1-8  referring doctor");
        check(scrubbed(hit, L"PV1", 17), "PV1-17 admitting doctor");
        check(scrubbed(hit, L"PV1", 19), "PV1-19 visit number");
        check(scrubbed(hit, L"GT1", 3),  "GT1-3  guarantor name");
        check(scrubbed(hit, L"GT1", 5),  "GT1-5  guarantor address");
        check(scrubbed(hit, L"GT1", 8),  "GT1-8  guarantor DOB");
        check(scrubbed(hit, L"GT1", 12), "GT1-12 guarantor SSN       (leaked before this fix)");
        check(scrubbed(hit, L"IN1", 3),  "IN1-3  insurance company ID");
        check(scrubbed(hit, L"IN1", 4),  "IN1-4  insurance company name");
    }

    std::printf("\n%s (%d failure%s)\n\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
