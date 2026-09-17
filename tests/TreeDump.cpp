// Prints the subtree PipeHat builds for a given field, using the same
// hl7tree::buildFieldChildren the panel calls. A way to see what the message
// tree will render without opening Notepad++ -- handy when checking a test
// message, or when a field renders differently than expected and you want to
// know whether the splitting or the Win32 rendering is at fault.
//
//   MSVC:  cl /std:c++17 /EHsc /I..\src TreeDump.cpp
//   g++:   g++ -std=c++17 -I../src TreeDump.cpp -o TreeDump

#include <cstdio>
#include <string>
#include <vector>
#include "FieldTree.h"

// Hand-rolled UTF-8 conversion rather than printf("%ls").
//
// %ls converts through the C locale, and in the default "C" locale a character
// outside ASCII -- the U+2026 ellipsis this code uses for clipped values and
// truncated sibling lists -- makes printf STOP MID-LINE and return an error that
// nothing checks. The first run of this tool appeared to show a missing
// component and a misplaced one; the splitting was right and the printer was
// swallowing the rest of the line.
static std::string u8(const std::wstring& w) {
    std::string o;
    for (wchar_t wc : w) {
        unsigned long c = (unsigned long)wc;
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) {
            o += (char)(0xC0 | (c >> 6));
            o += (char)(0x80 | (c & 0x3F));
        } else {
            o += (char)(0xE0 | (c >> 12));
            o += (char)(0x80 | ((c >> 6) & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
    }
    return o;
}

static void dump(const std::vector<hl7tree::Node>& nodes, int depth) {
    for (const auto& n : nodes) {
        std::printf("%*s%s\n", depth * 4, "", u8(n.label).c_str());
        dump(n.children, depth + 1);
    }
}

static void field(const std::wstring& path, const std::wstring& dataType, const std::wstring& raw) {
    const hl7tree::Delims d;
    std::printf("\n%s  [%s]\n  %s\n", u8(path).c_str(), u8(dataType).c_str(), u8(raw).c_str());
    std::vector<hl7tree::Node> kids = hl7tree::buildFieldChildren(raw, d, dataType);
    if (kids.empty()) { std::printf("    (leaf - no subtree)\n"); return; }
    dump(kids, 1);
}

int main() {
    field(L"PID-3", L"CX",
          L"1000123^^^MRN&ISO&L~999887777^^^SSN~A12345^^^MEDITECH^^VN");
    field(L"PID-5", L"XPN",
          L"TESTPATIENT^ALICE^Q^JR^DR^^L~TESTPATIENT^ALLIE^^^^^N");
    field(L"PID-11", L"XAD",
          L"123 MAIN ST^^ROCHESTER^NY^14624^USA^H~99 LAKE RD^APT 4B^BUFFALO^NY^14201^USA^B");
    field(L"PID-13", L"XTN",
          L"(585)555-0142^PRN^PH^^^585^5550142~alice.test\\S\\home@example.org^NET^X.400");
    field(L"PID-7", L"TS", L"19850412");
    field(L"PID-18", L"CX", L"ACCT99887^^^");
    field(L"PV1-7", L"XCN",
          L"1234^PROVIDER^PAT^^^DR^^^NPI&2.16.840.1.113883.4.6&ISO");
    field(L"IN1-3", L"CX", L"INS9^^^&&");
    field(L"OBX-5", L"ST",
          L"Short first part^This second component is deliberately far longer than sixty "
          L"characters so that label clipping is visible on a child node while the node "
          L"still carries the whole string^Short third part");
    field(L"ZQRY-3", L"", L"SITEVAL~SECOND~THIRD");
    field(L"ZQRY-4", L"", L"LOCAL&SUB&PART");
    // MSH-2 is what the caller skips. Shown so the guard's purpose is visible:
    // this is what the tree WOULD do if MessageTreeView ever stopped skipping it.
    field(L"MSH-2 (skipped by MessageTreeView)", L"ST", L"^~\\&");
    return 0;
}
