// Standalone regression test for the field-path resolver, variable-length segment
// IDs, the data-type component tables and the wrapped-segment detector.
//
// Build (from a vcvars64 shell, repo root):
//   cl /nologo /EHsc /std:c++17 /I src tests\FieldPathTest.cpp src\HL7Lexer.cpp ^
//      /Fe:build\FieldPathTest.exe /Fo:build\
//
// Links only HL7Lexer.cpp -- no Windows or Scintilla dependency. Exits non-zero on
// failure. Run it after touching HL7Lexer, Validator.h or HL7DataTypes.h.

#include "HL7Lexer.h"
#include "Validator.h"
#include "HL7DataTypes.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static void ok(bool cond, const char* what) {
    if (cond) { g_pass++; return; }
    g_fail++;
    std::printf("FAIL: %s\n", what);
}

static void eqi(int got, int want, const char* what) {
    if (got == want) { g_pass++; return; }
    g_fail++;
    std::printf("FAIL: %s (got %d, want %d)\n", what, got, want);
}

static void eqw(const std::wstring& got, const std::wstring& want, const char* what) {
    if (got == want) { g_pass++; return; }
    g_fail++;
    std::printf("FAIL: %s (got '%ls', want '%ls')\n", what, got.c_str(), want.c_str());
}

// Position of the first character of `needle` in `hay`.
static int at(const std::wstring& hay, const wchar_t* needle) {
    size_t p = hay.find(needle);
    return p == std::wstring::npos ? -1 : (int)p;
}

static HL7FieldPath pathAt(HL7Lexer& lx, const std::wstring& line, const wchar_t* needle) {
    return lx.getPathAtPosition(line.c_str(), (int)line.size(), at(line, needle));
}

int main() {
    HL7Lexer lx;

    // ---------------------------------------------------------------- [1] paths
    {
        const std::wstring nk1 =
            L"NK1|1|DOE^JANE^Q|SPO|123 MAIN ST^APT 4^ROCHESTER^NY^14624";

        HL7FieldPath p = pathAt(lx, nk1, L"ROCHESTER");
        eqi(p.field, 4, "ISC-3 NK1 city field");
        eqi(p.component, 3, "ISC-4 NK1 city component");
        eqi(p.subcomponent, 0, "ISC-5 NK1 city subcomponent");
        eqi(p.repeat, 1, "NK1 city repeat");

        // A field with no component separator stays at field depth.
        HL7FieldPath q = pathAt(lx, nk1, L"SPO");
        eqi(q.field, 3, "NK1-3 field");
        eqi(q.component, 0, "ISC-6 field without components has no component");

        // Segment-ID region.
        eqi(lx.getPathAtPosition(nk1.c_str(), (int)nk1.size(), 1).field, 0,
            "ISC-9 segment ID region");

        // getFieldIndexAtPosition still agrees with the resolver everywhere.
        bool agree = true;
        for (int i = 0; i < (int)nk1.size(); i++) {
            if (lx.getFieldIndexAtPosition(nk1.c_str(), (int)nk1.size(), i) !=
                lx.getPathAtPosition(nk1.c_str(), (int)nk1.size(), i).field) agree = false;
        }
        ok(agree, "ISC-11 getFieldIndexAtPosition == getPathAtPosition().field");
    }

    // repeats and subcomponents
    {
        const std::wstring pid =
            L"PID|1||MRN123^^^HOSP&1.2.3&ISO~SSN999^^^SSA|";

        HL7FieldPath r = pathAt(lx, pid, L"SSN999");
        eqi(r.field, 3, "PID-3 field");
        eqi(r.repeat, 2, "ISC-7 second repetition");
        eqi(r.component, 1, "PID-3[2].1 component");

        HL7FieldPath s = pathAt(lx, pid, L"1.2.3");
        eqi(s.field, 3, "PID-3 subcomponent field");
        eqi(s.repeat, 1, "PID-3[1] repeat");
        eqi(s.component, 4, "PID-3.4 component");
        ok(s.subcomponent >= 1, "ISC-8 subcomponent resolved");
        eqi(s.subcomponent, 2, "PID-3.4.2 subcomponent index");
    }

    // MSH offset survives the encoding characters (which contain the escape char)
    {
        std::wstring msh = L"MSH|^~";
        msh.push_back(L'\\');
        msh += L"&|SENDAPP|SENDFAC|RECAPP|RECFAC|20260825120000||ADT^A01|MSG0001|P|2.5";

        HL7FieldPath p = pathAt(lx, msh, L"SENDAPP");
        eqi(p.field, 3, "ISC-10 MSH-3 after encoding characters");

        HL7FieldPath q = pathAt(lx, msh, L"A01");
        eqi(q.field, 9, "MSH-9 field");
        eqi(q.component, 2, "MSH-9.2 trigger event component");
    }

    // ------------------------------------------------------- [2] segment IDs
    {
        auto seg = [&](const wchar_t* s) {
            return lx.extractSegmentID(s, (int)std::wcslen(s));
        };

        eqw(seg(L"ZQRY|1|abc"), L"ZQRY", "ISC-21 four-char Z segment");
        eqw(seg(L"ZQRY"),       L"ZQRY", "ISC-22 four-char Z segment at EOL");
        eqw(seg(L"ZPD|x"),      L"ZPD",  "ISC-23 three-char Z segment unaffected");
        eqw(seg(L"PID|1"),      L"PID",  "three-char segment unaffected");
        eqw(seg(L"MSH|^~"),     L"MSH",  "MSH self-defining");

        eqw(seg(L"ZONE OF INTEREST"), L"", "ISC-24 anti: prose starting with Z");
        eqw(seg(L"THE QUICK BROWN FOX"), L"", "ISC-25 anti: prose");
        eqw(seg(L"OBXA|1"), L"", "ISC-26 anti: four-char non-Z run");
        eqw(seg(L"ICU^101^A|x"), L"", "anti: wrapped PV1 tail");

        // tokenize must agree about the width
        std::vector<HL7Token> toks;
        std::wstring zq = L"ZQRY|1|abc";
        lx.tokenize(zq.c_str(), (int)zq.size(), toks);
        ok(!toks.empty() && toks[0].type == HL7TokenType::SEGMENT_ID,
           "ISC-27a tokenize emits a segment ID for ZQRY");
        if (!toks.empty()) eqi(toks[0].length, 4, "ISC-27b segment ID token length");

        eqi(lx.getFieldIndexAtPosition(zq.c_str(), (int)zq.size(), at(zq, L"1")), 1,
            "ISC-28 first field on a ZQRY line");

        ok(hl7val::validSegId(L"ZQRY"), "ISC-29a validSegId accepts ZQRY");
        ok(!hl7val::validSegId(L"ICU^101^A"), "ISC-29b validSegId rejects a wrapped tail");
        ok(!hl7val::validSegId(L"OBXA"), "validSegId rejects a four-char non-Z run");
    }

    // --------------------------------------------------- [3] component names
    {
        eqw(hl7dt::componentName(L"XAD", 3), L"City", "ISC-18 XAD.3 is City");
        eqw(hl7dt::componentName(L"XPN", 1), L"Family Name", "XPN.1 is Family Name");
        eqw(hl7dt::componentName(L"ZZZ", 1), L"", "ISC-20a untabled type yields nothing");
        eqw(hl7dt::componentName(L"XAD", 99), L"", "ISC-20b past end yields nothing");
        eqw(hl7dt::componentName(L"XAD", 0), L"", "component 0 yields nothing");
    }

    // ------------------------------------------------ [4] wrapped segments
    {
        std::wstring msh = L"MSH|^~";
        msh.push_back(L'\\');
        msh += L"&|A|B|C|D|20260825||ADT^A01|1|P|2.5";

        std::vector<std::wstring> wrapped = {
            msh,
            L"PID|1||MRN1^^^H||DOE^JOHN||19700101|M",
            L"PV1|1|I|WEST^101^A^HOSP||||1234^SMITH^JOHN|||SUR||||ADM|A0|||1234^SMITH",
            L"^JOHN|||||||||||||||||||||||20260825120000",
            L"OBX|1|ST|GLU||99|mg/dL"
        };

        std::vector<int> cont = hl7val::continuationLines(wrapped, L'|');
        eqi((int)cont.size(), 1, "ISC-31a exactly one continuation line");
        if (cont.size() == 1) eqi(cont[0], 3, "ISC-31b continuation is line 3");

        std::vector<std::wstring> clean = {
            msh,
            L"PID|1||MRN1^^^H||DOE^JOHN||19700101|M",
            L"ZQRY|1|abc",
            L"OBX|1|ST|GLU||99|mg/dL"
        };
        eqi((int)hl7val::continuationLines(clean, L'|').size(), 0,
            "ISC-32 anti: clean message reports nothing");

        std::vector<std::wstring> blanks = { msh, L"", L"   ", msh };
        eqi((int)hl7val::continuationLines(blanks, L'|').size(), 0,
            "ISC-33 anti: blank lines are not continuations");

        // The validator must name the wrapped segment, not invent a segment named '^JOHN'.
        std::vector<hl7val::Finding> f = hl7val::validate(wrapped, L'|', L'\\');
        bool named = false, misleading = false;
        for (const auto& x : f) {
            if (x.line == 3 && x.message.find(L"Line break inside segment PV1") != std::wstring::npos)
                named = true;
            if (x.message.find(L"Invalid segment ID") != std::wstring::npos)
                misleading = true;
        }
        ok(named, "ISC-36a validator names the wrapped segment");
        ok(!misleading, "ISC-36b validator drops the misleading 'Invalid segment ID'");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
