#pragma once
#include <string>
#include <vector>
#include "HL7DataTypes.h"

// Structure below the field: repetitions, components, subcomponents.
//
// The tree used to stop at the field and, worse, built the field's text by
// joining the lexer's FIELD_VALUE tokens with spaces -- which threw the
// separators away, so `DOE^JANE^Q` was displayed as `DOE JANE Q`. A reader
// looking at the tree to answer "which component is the city in" got no answer
// and a misleading string.
//
// This header takes one field's RAW text and returns the nodes underneath it.
// It is deliberately pure -- no Windows, no Scintilla, no lexer -- so
// tests/FieldTreeTest.cpp builds and runs off-Windows the same way the endpoint
// profile tests do.
//
// Splitting on the raw characters is correct because an HL7 escape sequence can
// never contain an unescaped delimiter: a literal `^` in data is written `\S\`,
// a literal `~` is `\R\`. The one place a delimiter appears as data is MSH-2
// (`^~\&`), and the caller special-cases it -- see MessageTreeView::refresh.
namespace hl7tree {

struct Delims {
    wchar_t comp   = L'^';
    wchar_t repeat = L'~';
    wchar_t sub    = L'&';
};

struct Node {
    std::wstring label;              // relative to the parent: "[2]", ".3 City", ".1.2"
    std::wstring value;              // raw text of this piece
    std::vector<Node> children;
};

// A field with a great many repetitions (a batch OBX, a long allergy list)
// should not be able to make the panel unusable or freeze the UI thread while
// the TreeView inserts thousands of items. Past this many siblings a single
// summary node stands in for the rest.
constexpr int kMaxSiblings = 64;

namespace detail {

// Split on `sep`, keeping empty pieces. Empty pieces are the point: in
// `123 MAIN ST^^ROCHESTER^NY` the reader needs to see that component 2 is empty
// and that ROCHESTER is component 3, not component 2.
inline std::vector<std::wstring> split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        if (p == std::wstring::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

// Shown after a label. Long values are clipped so one OBX-5 carrying an embedded
// document cannot push every other node off the panel.
inline std::wstring preview(const std::wstring& v) {
    if (v.empty()) return std::wstring();
    const size_t kMax = 60;
    if (v.size() <= kMax) return L" = " + v;
    return L" = " + v.substr(0, kMax) + L"\x2026";   // ellipsis
}

inline std::wstring truncatedNode(size_t remaining) {
    return L"\x2026 " + std::to_wstring(remaining) + L" more";
}

// Subcomponents of one component. Returns empty when there is nothing to show.
inline std::vector<Node> subcomponents(const std::wstring& comp, const Delims& d,
                                       int componentNo) {
    std::vector<Node> out;
    std::vector<std::wstring> parts = split(comp, d.sub);
    if (parts.size() <= 1) return out;          // no subcomponent separator present

    const size_t shown = parts.size() > (size_t)kMaxSiblings ? (size_t)kMaxSiblings : parts.size();
    for (size_t i = 0; i < shown; ++i) {
        Node n;
        n.label = L"." + std::to_wstring(componentNo) + L"." + std::to_wstring((int)i + 1) +
                  preview(parts[i]);
        n.value = parts[i];
        out.push_back(n);
    }
    if (shown < parts.size()) {
        Node more; more.label = truncatedNode(parts.size() - shown);
        out.push_back(more);
    }
    return out;
}

// Components of one repetition. dataType names them where the table knows it.
inline std::vector<Node> components(const std::wstring& rep, const Delims& d,
                                    const std::wstring& dataType) {
    std::vector<Node> out;
    std::vector<std::wstring> parts = split(rep, d.comp);
    if (parts.size() <= 1) {
        // A single component with subcomponents still has structure worth
        // showing: `A&B` under a field is two subcomponents of component 1.
        return subcomponents(parts.empty() ? std::wstring() : parts[0], d, 1);
    }

    const size_t shown = parts.size() > (size_t)kMaxSiblings ? (size_t)kMaxSiblings : parts.size();
    for (size_t i = 0; i < shown; ++i) {
        const int no = (int)i + 1;
        Node n;
        n.label = L"." + std::to_wstring(no);
        const std::wstring name = hl7dt::componentName(dataType, no);
        if (!name.empty()) n.label += L" " + name;
        n.label += preview(parts[i]);
        n.value = parts[i];
        n.children = subcomponents(parts[i], d, no);
        out.push_back(n);
    }
    if (shown < parts.size()) {
        Node more; more.label = truncatedNode(parts.size() - shown);
        out.push_back(more);
    }
    return out;
}

} // namespace detail

// The nodes to hang under one field. Empty means the field is a leaf.
//
// The repetition level appears ONLY when the field actually repeats. Adding a
// `[1]` node to every single-valued field would double the depth of the whole
// tree to say nothing, and PID-5 with one name would read worse than it does
// today. When there is exactly one repetition its components hang directly off
// the field, which is what a reader expects and what hl7inspector shows.
inline std::vector<Node> buildFieldChildren(const std::wstring& fieldText,
                                            const Delims& d,
                                            const std::wstring& dataType) {
    std::vector<Node> out;
    if (fieldText.empty()) return out;

    std::vector<std::wstring> reps = detail::split(fieldText, d.repeat);
    if (reps.size() <= 1)
        return detail::components(fieldText, d, dataType);

    const size_t shown = reps.size() > (size_t)kMaxSiblings ? (size_t)kMaxSiblings : reps.size();
    for (size_t i = 0; i < shown; ++i) {
        Node n;
        n.label = L"[" + std::to_wstring((int)i + 1) + L"]" + detail::preview(reps[i]);
        n.value = reps[i];
        n.children = detail::components(reps[i], d, dataType);
        out.push_back(n);
    }
    if (shown < reps.size()) {
        Node more; more.label = detail::truncatedNode(reps.size() - shown);
        out.push_back(more);
    }
    return out;
}

} // namespace hl7tree
