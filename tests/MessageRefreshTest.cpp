// Standalone harness for MessageRefresh -- MSH-7 / MSH-10 rewriting before send.
//
// Receivers dedupe on MSH-10, so a replay that reuses control ids is accepted once
// and silently dropped thereafter -- a test that lies. These assertions pin the
// off-by-one (MSH-N is token[N-1]) and the refusal to touch MSH-1, which IS the
// field separator.
//
// Build (from repo root, in a VS developer prompt):
//   cl /EHsc /std:c++17 /I src tests\MessageRefreshTest.cpp /Fe:build\MessageRefreshTest.exe
//   build\MessageRefreshTest.exe
//
// Exits non-zero on any failure so it can gate a release.

#include "MessageRefresh.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    std::printf(cond ? "  [pass] %s\n" : "  [FAIL] %s\n", what.c_str());
    if (!cond) g_failures++;
}

static void checkStr(const std::string& got, const std::string& expect, const std::string& what) {
    if (got == expect) {
        std::printf("  [pass] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s\n         expected: %s\n         got     : %s\n",
                    what.c_str(), expect.c_str(), got.c_str());
        g_failures++;
    }
}

static const char* MSH =
    "MSH|^~\\&|EPIC|MERCY|PIPEHAT|LAB|20260716093000||ADT^A01|CTRL0001|P|2.5";

int main() {
    int section = 0;

    std::printf("\n[%d] setMshField honors the MSH off-by-one (MSH-N is token[N-1])\n", ++section);
    {
        checkStr(hl7refresh::getMshField(MSH, 7),  "20260716093000", "MSH-7 reads the datetime");
        checkStr(hl7refresh::getMshField(MSH, 9),  "ADT^A01",        "MSH-9 reads the message type");
        checkStr(hl7refresh::getMshField(MSH, 10), "CTRL0001",       "MSH-10 reads the control id");
        checkStr(hl7refresh::getMshField(MSH, 2),  "^~\\&",          "MSH-2 reads the encoding chars");
        checkStr(hl7refresh::getMshField(MSH, 12), "2.5",            "MSH-12 reads the version");

        std::string r = hl7refresh::setMshField(MSH, 10, "NEW999");
        checkStr(hl7refresh::getMshField(r, 10), "NEW999", "MSH-10 written back");
        checkStr(hl7refresh::getMshField(r, 9),  "ADT^A01", "neighbouring MSH-9 untouched");
        checkStr(hl7refresh::getMshField(r, 11), "P",       "neighbouring MSH-11 untouched");
    }

    std::printf("\n[%d] MSH-1 is the field separator and must never be rewritten\n", ++section);
    {
        checkStr(hl7refresh::setMshField(MSH, 1, "X"), MSH, "setMshField(1) is a no-op");
        checkStr(hl7refresh::setMshField(MSH, 0, "X"), MSH, "setMshField(0) is a no-op");
        checkStr(hl7refresh::getMshField(MSH, 1), "", "getMshField(1) returns nothing, not '|'");
    }

    std::printf("\n[%d] refresh rewrites MSH-7 and MSH-10 only\n", ++section);
    {
        std::string msg = std::string(MSH) + "\r"
                          "EVN|A01|20260716093000\r"
                          "PID|1||100001^^^MRN||DOE^JOHN\r";
        std::string out = hl7refresh::refresh(msg, "20260101000000", "REPLAY001");

        auto segs = mllp::segments(out);
        check(segs.size() == 3, "segment count preserved");
        checkStr(hl7refresh::getMshField(segs[0], 7),  "20260101000000", "MSH-7 refreshed");
        checkStr(hl7refresh::getMshField(segs[0], 10), "REPLAY001",      "MSH-10 refreshed");
        checkStr(segs[1], "EVN|A01|20260716093000", "EVN untouched (its own datetime is data)");
        checkStr(segs[2], "PID|1||100001^^^MRN||DOE^JOHN", "PID untouched");
        check(out.back() == '\r', "message is CR-terminated for the wire");
    }

    std::printf("\n[%d] Empty stamp / id leaves that field alone\n", ++section);
    {
        std::string msg = std::string(MSH) + "\r";
        std::string idOnly = hl7refresh::refresh(msg, "", "ONLYID");
        checkStr(hl7refresh::getMshField(mllp::segments(idOnly)[0], 7), "20260716093000",
                 "empty stamp preserves MSH-7");
        checkStr(hl7refresh::getMshField(mllp::segments(idOnly)[0], 10), "ONLYID",
                 "control id still applied");

        std::string stampOnly = hl7refresh::refresh(msg, "20200101000000", "");
        checkStr(hl7refresh::getMshField(mllp::segments(stampOnly)[0], 10), "CTRL0001",
                 "empty control id preserves MSH-10");
    }

    std::printf("\n[%d] Non-'|' separators (MSH-1 is whatever follows MSH)\n", ++section);
    {
        const char* bang = "MSH!^~\\&!MEDITECH!HIGHLAND!PIPEHAT!LAB!20260716094500!!ADT^A08!CTRL0004!P!2.4";
        checkStr(hl7refresh::getMshField(bang, 10), "CTRL0004", "MSH-10 read with '!' separator");
        std::string r = hl7refresh::setMshField(bang, 10, "NEW4");
        checkStr(hl7refresh::getMshField(r, 10), "NEW4", "MSH-10 written with '!' separator");
        check(r.find('|') == std::string::npos, "no '|' leaked into a '!'-delimited message");
    }

    std::printf("\n[%d] Short MSH pads rather than truncating\n", ++section);
    {
        const char* shortMsh = "MSH|^~\\&|APP|FAC";      // only through MSH-4
        std::string r = hl7refresh::setMshField(shortMsh, 10, "PADDED");
        checkStr(hl7refresh::getMshField(r, 10), "PADDED", "MSH-10 set on a short MSH");
        checkStr(hl7refresh::getMshField(r, 3), "APP", "existing MSH-3 survives padding");
        checkStr(hl7refresh::getMshField(r, 7), "", "intervening fields are empty, not garbage");
    }

    std::printf("\n[%d] Degenerate input is returned unchanged, never mangled\n", ++section);
    {
        checkStr(hl7refresh::refresh("", "S", "C"), "", "empty message");
        checkStr(hl7refresh::refresh("PID|1||X\r", "S", "C"), "PID|1||X\r", "no MSH -> unchanged");
        checkStr(hl7refresh::setMshField("PID|1", 10, "X"), "PID|1", "non-MSH segment untouched");
        checkStr(hl7refresh::setMshField("MS", 10, "X"), "MS", "truncated segment untouched");
    }

    std::printf("\n[%d] Control ids are unique per sequence and fit MSH-10 (<= 20 chars)\n", ++section);
    {
        std::string stamp = "20260716093000";
        std::string a = hl7refresh::makeControlId(stamp, 1);
        std::string b = hl7refresh::makeControlId(stamp, 2);
        check(a != b, "different sequence -> different control id");
        check(a.size() <= 20, "control id fits MSH-10 (" + std::to_string(a.size()) + " chars)");
        check(hl7refresh::nowStamp().size() == 14, "nowStamp is YYYYMMDDHHMMSS");
    }

    std::printf("\n%s (%d failure%s)\n\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
