// Standalone test for src/FieldTree.h -- the repetition / component /
// subcomponent structure shown under a field in the message tree.
// Exits non-zero on failure.
//
// FieldTree.h and HL7DataTypes.h are both free of Windows headers, so this
// builds with either toolchain:
//
//   MSVC:  cl /std:c++17 /EHsc /I..\src FieldTreeTest.cpp
//   g++:   g++ -std=c++17 -I../src FieldTreeTest.cpp -o FieldTreeTest
//
// Run it after touching FieldTree.h, HL7DataTypes.h, or the tree-building loop
// in MessageTreeView::refresh.

#include <cstdio>
#include <string>
#include "FieldTree.h"

static int g_failures = 0;
static int g_checks = 0;

static void ok(bool cond, const char* what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

static void eqs(const std::wstring& got, const std::wstring& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  FAIL: %s\n        got  '%ls'\n        want '%ls'\n",
                    what, got.c_str(), want.c_str());
    }
}

static void eqi(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  FAIL: %s (got %d, want %d)\n", what, got, want);
    }
}

static bool labelStarts(const hl7tree::Node& n, const wchar_t* prefix) {
    return n.label.rfind(prefix, 0) == 0;
}

int main() {
    using namespace hl7tree;
    const Delims D;   // ^ ~ &

    // ---------------------------------------------------------------- [1]
    // The reason the comment was filed: a repeating field showed as one flat
    // node with the separators gone.
    std::printf("[1] repetition level\n");
    {
        // PID-3 with three identifiers.
        std::vector<Node> n = buildFieldChildren(L"1234^^^MRN~5678^^^MR~9999^^^SSN", D, L"CX");
        eqi((int)n.size(), 3, "three repetitions produce three nodes");
        ok(labelStarts(n[0], L"[1]"), "first repetition is labelled [1]");
        ok(labelStarts(n[1], L"[2]"), "second repetition is labelled [2]");
        ok(labelStarts(n[2], L"[3]"), "third repetition is labelled [3]");
        eqs(n[1].value, L"5678^^^MR", "a repetition node carries its own raw text");
        eqi((int)n[1].children.size(), 4, "a repetition splits into its components");
    }

    // ---------------------------------------------------------------- [2]
    // A [1] node on every single-valued field would double the depth of the
    // whole tree to say nothing.
    std::printf("[2] no repetition level when the field does not repeat\n");
    {
        std::vector<Node> n = buildFieldChildren(L"DOE^JANE^Q", D, L"XPN");
        eqi((int)n.size(), 3, "a single repetition hangs its components off the field");
        ok(labelStarts(n[0], L".1"), "first child is component 1, not [1]");
        ok(!labelStarts(n[0], L"[1]"), "Anti: no repetition node is emitted for one value");
    }

    // ---------------------------------------------------------------- [3]
    std::printf("[3] component names from the data type\n");
    {
        std::vector<Node> n = buildFieldChildren(L"DOE^JANE^Q", D, L"XPN");
        eqs(n[0].label, L".1 Family Name = DOE", "component 1 is named from XPN");
        eqs(n[1].label, L".2 Given Name = JANE", "component 2 is named from XPN");

        // The address case from the original hover bug: the reader wants to know
        // ROCHESTER is component 3, and that it is the City.
        std::vector<Node> a = buildFieldChildren(L"123 MAIN ST^APT 4^ROCHESTER^NY^14624", D, L"XAD");
        eqs(a[2].label, L".3 City = ROCHESTER", "XAD component 3 is named City");

        // An untabled type still gets the numeric path, never a guess.
        std::vector<Node> u = buildFieldChildren(L"A^B", D, L"ZZZ");
        eqs(u[0].label, L".1 = A", "an unknown data type renders the number alone");
        std::vector<Node> e = buildFieldChildren(L"A^B", D, L"");
        eqs(e[0].label, L".1 = A", "an empty data type renders the number alone");
    }

    // ---------------------------------------------------------------- [4]
    // Position is the whole point for an interface engineer. An empty component
    // that silently vanished would renumber everything after it.
    std::printf("[4] empty pieces are preserved\n");
    {
        std::vector<Node> n = buildFieldChildren(L"123 MAIN ST^^ROCHESTER^NY", D, L"XAD");
        eqi((int)n.size(), 4, "an empty middle component still occupies its position");
        eqs(n[1].value, L"", "the empty component has an empty value");
        eqs(n[1].label, L".2 Other Designation", "an empty component shows no ' = ' preview");
        eqs(n[2].value, L"ROCHESTER", "the component after the gap keeps position 3");

        std::vector<Node> t = buildFieldChildren(L"A^B^", D, L"ZZZ");
        eqi((int)t.size(), 3, "a trailing empty component is not dropped");

        std::vector<Node> r = buildFieldChildren(L"A~~C", D, L"ZZZ");
        eqi((int)r.size(), 3, "an empty middle repetition keeps its position");
        ok(labelStarts(r[2], L"[3]"), "the repetition after the gap is still [3]");
    }

    // ---------------------------------------------------------------- [5]
    std::printf("[5] subcomponents\n");
    {
        std::vector<Node> n = buildFieldChildren(L"1234^^^MRN&ISO&L", D, L"CX");
        eqi((int)n.size(), 4, "four components");
        eqi((int)n[3].children.size(), 3, "component 4 splits into three subcomponents");
        eqs(n[3].children[0].label, L".4.1 = MRN", "subcomponent path is .4.1");
        eqs(n[3].children[2].label, L".4.3 = L", "subcomponent path is .4.3");
        ok(n[0].children.empty(), "a component with no subcomponent separator is a leaf");
    }

    // ---------------------------------------------------------------- [6]
    // `A&B` with no component separator is still structure worth showing.
    std::printf("[6] subcomponents without a component separator\n");
    {
        std::vector<Node> n = buildFieldChildren(L"A&B", D, L"ZZZ");
        eqi((int)n.size(), 2, "a lone component with subcomponents still expands");
        eqs(n[0].label, L".1.1 = A", "it is numbered as component 1");
        eqs(n[1].label, L".1.2 = B", "second subcomponent of component 1");
    }

    // ---------------------------------------------------------------- [7]
    std::printf("[7] leaves and empties\n");
    {
        ok(buildFieldChildren(L"", D, L"ST").empty(), "an empty field has no children");
        ok(buildFieldChildren(L"12345", D, L"ST").empty(), "a plain value is a leaf");
        ok(buildFieldChildren(L"12345", D, L"XPN").empty(),
           "a plain value is a leaf even when the type is tabled");
    }

    // ---------------------------------------------------------------- [8]
    // A literal delimiter in data is escaped, so splitting on raw characters is
    // safe. \S\ is an escaped ^, \R\ an escaped ~.
    std::printf("[8] escaped delimiters do not split\n");
    {
        std::vector<Node> n = buildFieldChildren(L"SMITH \\S\\ JONES^ANN", D, L"XPN");
        eqi((int)n.size(), 2, "an escaped component separator does not create a component");
        eqs(n[0].value, L"SMITH \\S\\ JONES", "the escape sequence stays in the value");

        std::vector<Node> r = buildFieldChildren(L"A\\R\\B", D, L"ZZZ");
        ok(r.empty(), "an escaped repetition separator does not create a repetition");
    }

    // ---------------------------------------------------------------- [9]
    // Custom delimiters are read from MSH-2 and must be honoured; a message
    // using ! and * must not be split on ^ and ~.
    std::printf("[9] non-default delimiters\n");
    {
        Delims alt; alt.comp = L'*'; alt.repeat = L'!'; alt.sub = L'@';
        std::vector<Node> n = buildFieldChildren(L"A*B!C*D", alt, L"ZZZ");
        eqi((int)n.size(), 2, "splits on the message's own repetition separator");
        eqi((int)n[0].children.size(), 2, "and on its own component separator");
        eqs(n[0].children[1].value, L"B", "component 2 of repetition 1");

        std::vector<Node> d = buildFieldChildren(L"A^B", alt, L"ZZZ");
        ok(d.empty(), "Anti: a default ^ is NOT a separator when MSH-2 says otherwise");
    }

    // ---------------------------------------------------------------- [10]
    // One OBX-5 carrying an embedded document must not be able to make the panel
    // unusable or stall the UI thread inserting items.
    std::printf("[10] runaway fields are capped\n");
    {
        std::wstring many = L"r1";
        for (int i = 2; i <= 200; ++i) many += L"~r" + std::to_wstring(i);
        std::vector<Node> n = buildFieldChildren(many, D, L"ZZZ");
        eqi((int)n.size(), kMaxSiblings + 1, "capped at kMaxSiblings plus one summary node");
        ok(labelStarts(n[kMaxSiblings], L"\x2026"), "the last node summarises the remainder");
        ok(n.back().label.find(L"136 more") != std::wstring::npos,
           "the summary names how many were not shown");

        std::wstring longVal(500, L'X');
        std::vector<Node> p = buildFieldChildren(longVal + L"^B", D, L"ZZZ");
        ok(p[0].label.size() < 100, "a very long value is clipped in the label");
        eqi((int)p[0].value.size(), 500, "but the node keeps the full value");
    }

    // ---------------------------------------------------------------- [11]
    // Regression guards for the two defects the tree wiring had to fix. They
    // live here rather than in the Win32 code because this is the half that can
    // actually be run.
    std::printf("[11] separators survive, positions are honest\n");
    {
        // The old tree joined the lexer's FIELD_VALUE tokens with spaces, so
        // `DOE^JANE^Q` reached the panel as `DOE JANE Q`. Nothing this header
        // produces may contain an invented space where a separator was.
        std::vector<Node> n = buildFieldChildren(L"DOE^JANE^Q", D, L"XPN");
        for (const auto& c : n)
            ok(c.value.find(L' ') == std::wstring::npos,
               "a component value carries no separator-turned-space");
        eqs(n[2].value, L"Q", "the third component is exactly Q");

        // MSH-2 is the one field whose content IS delimiter characters. The
        // caller must not pass it here; if it ever does, this is what it looks
        // like, and the assertion records why the guard exists.
        std::vector<Node> enc = buildFieldChildren(L"^~\\&", D, L"ST");
        ok(!enc.empty(),
           "MSH-2 would split into pieces, which is why MessageTreeView skips it");
    }

    // ---------------------------------------------------------------- [12]
    // Offsets are what let a click in the tree select the exact piece in the
    // editor instead of just jumping to the line. Every one is relative to the
    // start of the FIELD text; the caller adds the field's own position.
    std::printf("[12] offsets into the field text\n");
    {
        const std::wstring f = L"1234^^^MRN&ISO&L~5678^^^MR";
        std::vector<Node> n = buildFieldChildren(f, D, L"CX");

        eqi(n[0].offset, 0, "repetition 1 starts at 0");
        eqi(n[0].length, 16, "repetition 1 spans its own text only");
        eqi(n[1].offset, 17, "repetition 2 starts after the ~");
        eqs(f.substr((size_t)n[1].offset, (size_t)n[1].length), L"5678^^^MR",
            "slicing the field by repetition 2's range returns repetition 2");

        const Node& auth = n[0].children[3];
        eqs(f.substr((size_t)auth.offset, (size_t)auth.length), L"MRN&ISO&L",
            "component 4's range returns component 4");
        eqs(f.substr((size_t)auth.children[1].offset, (size_t)auth.children[1].length), L"ISO",
            "subcomponent .4.2's range returns ISO");

        // An empty piece still has an honest position: it is where a value would
        // go, which is what makes selecting it useful at all.
        const Node& empty = n[0].children[1];
        eqi(empty.length, 0, "an empty component has zero length");
        eqi(empty.offset, 5, "and still points at where it would start");

        // No repetition level means components are measured from the field start.
        std::vector<Node> c = buildFieldChildren(L"DOE^JANE^Q", D, L"XPN");
        eqi(c[0].offset, 0, "component 1 of a non-repeating field starts at 0");
        // DOE(0-2) ^(3) JANE(4-7) ^(8) Q(9)
        eqi(c[2].offset, 9, "component 3 starts after both separators");
        eqs(std::wstring(L"DOE^JANE^Q").substr((size_t)c[2].offset, (size_t)c[2].length), L"Q",
            "and slicing by that range returns Q");

        // The summary node stands in for siblings that were not shown, so it
        // describes no single range and must not claim one.
        std::wstring many = L"r1";
        for (int i = 2; i <= 200; ++i) many += L"~r" + std::to_wstring(i);
        std::vector<Node> m = buildFieldChildren(many, D, L"ZZZ");
        eqi(m.back().offset, -1, "Anti: the truncation summary claims no range");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
