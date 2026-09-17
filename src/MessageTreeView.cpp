#include "MessageTreeView.h"
#include "HL7Lexer.h"
#include "SegmentDB.h"
#include "ScintillaStyler.h"
#include "SciUtils.h"
#include "PluginDefs.h"
#include "TriggerEventDB.h"
#include "MessageIndex.h"
#include <string>
#include <vector>
#include <cwctype>

MessageTreeView* MessageTreeView::s_instance = nullptr;

MessageTreeView::MessageTreeView() {
    s_instance = this;
}

bool MessageTreeView::create(HINSTANCE hInst, HWND hParent, NppData* nppData) {
    m_hInst = hInst;
    m_nppData = nppData;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Create the dialog
    m_hDlg = CreateDialogParamW(hInst,
        MAKEINTRESOURCEW(1),
        hParent,
        dlgProc,
        (LPARAM)this);

    if (!m_hDlg) return false;

    // Register as dockable
    DockedWidgetData dwd;
    dwd.hClient = m_hDlg;
    dwd.pszName = L"PipeHat \x2014 Message Tree";
    dwd.dlgID = 1;
    dwd.uMask = DWS_DF_CONT_RIGHT | DWS_ICONTAB;
    dwd.pszModuleName = HL7_PLUGIN_DLL;

    SendMessage(nppData->_nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&dwd);
    m_registered = true;

    return true;
}

void MessageTreeView::show() {
    if (m_hDlg && m_registered) {
        SendMessage(m_nppData->_nppHandle, NPPM_DMMSHOW, 0, (LPARAM)m_hDlg);
        m_visible = true;
    }
}

void MessageTreeView::hide() {
    if (m_hDlg && m_registered) {
        SendMessage(m_nppData->_nppHandle, NPPM_DMMHIDE, 0, (LPARAM)m_hDlg);
        m_visible = false;
    }
}

// lParam 0 means "no target", which is how a node with nothing to select is
// distinguished from one pointing at the first entry.
LPARAM MessageTreeView::makeTarget(int line, int startCol, int length) {
    m_targets.push_back(NodeTarget{ line, startCol, length });
    return (LPARAM)m_targets.size();
}

void MessageTreeView::clearTree() {
    m_targets.clear();
    if (m_hTree) {
        SendMessageW(m_hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    }
}

// `parent` is TVI_ROOT for a single-message buffer (the tree stays flat, exactly as
// before) and the message node when a buffer holds several.
HTREEITEM MessageTreeView::addSegmentNode(const std::wstring& text, int lineNum, LPARAM lparam,
                                          HTREEITEM parent) {
    TVINSERTSTRUCTW tvis;
    memset(&tvis, 0, sizeof(tvis));
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = (LPWSTR)text.c_str();
    tvis.item.lParam = lparam;
    return (HTREEITEM)SendMessageW(m_hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

// Top-level grouping node for one message in a multi-message buffer. lParam carries
// line+1 like every other node, so clicking it navigates to that message's MSH.
HTREEITEM MessageTreeView::addMessageNode(const std::wstring& text, LPARAM lparam) {
    TVINSERTSTRUCTW tvis;
    memset(&tvis, 0, sizeof(tvis));
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = (LPWSTR)text.c_str();
    tvis.item.lParam = lparam;
    return (HTREEITEM)SendMessageW(m_hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

HTREEITEM MessageTreeView::addFieldNode(HTREEITEM parent, const std::wstring& text, int lineNum, int fieldIdx, LPARAM lparam) {
    TVINSERTSTRUCTW tvis;
    memset(&tvis, 0, sizeof(tvis));
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = (LPWSTR)text.c_str();
    tvis.item.lParam = lparam;
    return (HTREEITEM)SendMessageW(m_hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

// Hang a field's repetition / component / subcomponent nodes. Each node gets its
// own entry in m_targets, so clicking one selects exactly that piece rather than
// the whole line it sits on.
void MessageTreeView::addValueNodes(HTREEITEM parent, const std::vector<hl7tree::Node>& nodes,
                                    int line, int fieldStart, int& budget) {
    for (const auto& n : nodes) {
        if (budget <= 0) return;
        --budget;
        // offset is relative to the field's text, so the field's own start is
        // added here. offset -1 is the truncation summary, which describes no
        // single piece and falls back to selecting nothing.
        const LPARAM lp = (n.offset >= 0)
            ? makeTarget(line, fieldStart + n.offset, n.length)
            : makeTarget(line, 0, 0);
        HTREEITEM h = addFieldNode(parent, n.label, 0, 0, lp);
        if (!n.children.empty()) addValueNodes(h, n.children, line, fieldStart, budget);
    }
}

void MessageTreeView::refresh(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect,
                               HL7Lexer& sharedLexer, SegmentDB& segDB) {
    if (!m_hTree || !fnDirect) return;

    // Inserting and deleting items raises selection changes; acting on those
    // would scroll the editor around while the panel is only being rebuilt.
    m_refreshing = true;
    struct ClearGuard {
        bool* flag;
        ~ClearGuard() { *flag = false; }
    } guard{ &m_refreshing };

    clearTree();

    ScintillaStyler styler;
    styler.init(hScintilla, fnDirect, ptrDirect);

    int lineCount = styler.sciGetLineCount();
    if (lineCount <= 0) return;

    // The buffer may hold many messages (a log or batch file), each declaring its own
    // delimiters. Build the boundary map once and let it -- not "the first MSH we
    // happen to find" -- decide which delimiters apply to each line.
    hl7::MessageIndex index;
    index.build(lineCount, [fnDirect, ptrDirect](int i) { return getLineW(fnDirect, ptrDirect, i); });
    if (index.empty()) return;

    // One message keeps the flat segment tree users already know. Several messages get
    // a grouping node each, so a 480-message log is navigable instead of being a flat
    // list of thousands of segments.
    const bool group = index.count() > 1;
    HTREEITEM msgParent = TVI_ROOT;
    int curMsg = -1;

    // Every node is a synchronous TVM_INSERTITEMW on the UI thread. The segment
    // and field levels were already bounded by the message itself; the value
    // levels below them are not -- a 480-message batch multiplies by however many
    // components each field happens to carry. Past this budget the fields still
    // list, they just stop expanding, so a huge log degrades to the tree users
    // had before instead of freezing Notepad++ while it inserts.
    int valueNodeBudget = 20000;

    for (int li = 0; li < lineCount; li++) {
        std::wstring wlStr = getLineW(fnDirect, ptrDirect, li);
        if (wlStr.size() < 3) continue;
        const wchar_t* wl = wlStr.c_str();
        int wlLen = (int)wlStr.size();

        // Load this line's own message's delimiters before reading it.
        sharedLexer.setDelimiters(index.delimitersFor(li));

        std::wstring segId = sharedLexer.extractSegmentID(wl, wlLen);
        if (segId.empty()) continue;

        if (group) {
            int mi = index.indexAt(li);
            if (mi < 0) {
                // Envelope/preamble (FHS/BHS/BTS/FTS) belongs to no message: hang it
                // at the root rather than inside whichever message happens to be open.
                msgParent = TVI_ROOT;
                curMsg = -1;
            } else if (mi != curMsg) {
                const hl7::MessageSpan* s = index.at((size_t)mi);
                std::wstring decoded = hl7trig::decodeMSH9(s->type, s->delims.compSep);
                std::wstring label = std::to_wstring(mi + 1) + L"/" + std::to_wstring(index.count())
                                   + L"  " + (s->type.empty() ? L"(no MSH-9)" : s->type);
                if (!s->controlId.empty()) label += L"  [" + s->controlId + L"]";
                if (!decoded.empty()) label += L"  \x21D2 " + decoded; // ⇒
                msgParent = addMessageNode(label, makeTarget(s->startLine, 0, 0));
                curMsg = mi;
            }
        }

        const HL7SegmentDef* segDef = segDB.lookup(segId);
        std::wstring segLabel;
        if (segDef) {
            segLabel = segId + L" \x25B6 " + segDef->name; // ▶
        } else {
            segLabel = segId + L" \x25B6 (unknown)";
        }

        // A segment node selects its whole line: the node IS the segment.
        HTREEITEM segNode = addSegmentNode(segLabel, li,
                                           makeTarget(li, 0, (int)wlStr.size()), msgParent);

        // Tokenize to find fields
        std::vector<HL7Token> tokens;
        sharedLexer.tokenize(wl, wlLen, tokens);

        const HL7Delimiters& dl = sharedLexer.delimiters();
        hl7tree::Delims treeDelims;
        treeDelims.comp   = dl.compSep;
        treeDelims.repeat = dl.repeatSep;
        treeDelims.sub    = dl.subcompSep;

        // MSH-1 is the field separator itself, so the first value is MSH-2 — start
        // the counter one higher for MSH to keep field labels aligned.
        int fieldIdx = (segId == L"MSH") ? 1 : 0;

        // The field's RAW text, sliced straight out of the line between field
        // separators. This replaced joining the lexer's FIELD_VALUE tokens with
        // spaces, which threw the component and repetition separators away and
        // rendered `DOE^JANE^Q` as `DOE JANE Q` — unreadable for the one question
        // the tree exists to answer, which is what sits at which position.
        int valStart = -1;

        // Emit one field node plus its repetition / component / subcomponent
        // subtree. Shared by the in-loop and end-of-line emits so the two cannot
        // drift apart; they already had, and the end-of-line copy was passing the
        // field number where every other node passes line+1, so clicking the last
        // field of a segment jumped to whatever line happened to share that number.
        auto emitField = [&](std::wstring raw, int fieldStart) {
            // getLineW returns the line's own terminator, so the LAST field on a
            // line carries a trailing CR/LF. Left in, it becomes a stray glyph in
            // the label and a phantom character in the final subcomponent.
            while (!raw.empty() && (raw.back() == L'\r' || raw.back() == L'\n'))
                raw.pop_back();
            if (raw.empty()) return;

            const HL7FieldDef* fd = segDB.lookupField(segId, fieldIdx);
            std::wstring flabel;
            if (fd) {
                flabel = std::to_wstring(fieldIdx) + L": " + fd->name + L"  [" + fd->dataType + L"]";
            } else {
                flabel = std::to_wstring(fieldIdx) + L": (unnamed)";
            }
            if (!raw.empty()) flabel += L" = " + raw;

            std::wstring decoded = hl7trig::decodeField(segId, fieldIdx, wlStr,
                dl.fieldSep, dl.compSep);
            if (!decoded.empty()) flabel += L"  \x21D2 " + decoded; // ⇒

            HTREEITEM fieldNode = addFieldNode(segNode, flabel, li, fieldIdx,
                                               makeTarget(li, fieldStart, (int)raw.size()));

            // MSH-2 IS the encoding characters (`^~\&`), the one place in a message
            // where delimiter characters are data. Splitting it would render the
            // component separator as a component separator.
            if (segId == L"MSH" && fieldIdx == 2) return;

            const std::wstring dataType = fd ? fd->dataType : std::wstring();
            addValueNodes(fieldNode, hl7tree::buildFieldChildren(raw, treeDelims, dataType),
                          li, fieldStart, valueNodeBudget);
        };

        for (const auto& tok : tokens) {
            if (tok.type != HL7TokenType::FIELD_SEP) continue;
            if (fieldIdx >= 1 && valStart >= 0 && tok.startPos > valStart) {
                std::wstring raw = wlStr.substr((size_t)valStart, (size_t)(tok.startPos - valStart));
                if (!raw.empty()) emitField(raw, valStart);
            }
            valStart = tok.startPos + tok.length;
            fieldIdx++;
        }
        // Emit the last field, which has no separator after it.
        if (fieldIdx >= 1 && valStart >= 0 && valStart < wlLen) {
            std::wstring raw = wlStr.substr((size_t)valStart);
            if (!raw.empty()) emitField(raw, valStart);
        }
    }
}

void MessageTreeView::onTreeClick(bool moveFocus) {
    if (!m_hTree || !m_nppData) return;

    HTREEITEM hItem = (HTREEITEM)SendMessageW(m_hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    if (!hItem) return;

    TVITEMW item;
    memset(&item, 0, sizeof(item));
    item.hItem = hItem;
    item.mask = TVIF_PARAM;
    SendMessageW(m_hTree, TVM_GETITEMW, 0, (LPARAM)&item);

    const size_t idx = (size_t)item.lParam;
    if (idx == 0 || idx > m_targets.size()) return;
    const NodeTarget t = m_targets[idx - 1];

    // Navigate to the line in the editor
    HWND hSci = GetFocus(); // Fallback
    HWND hMain = m_nppData->_scintillaMainHandle;
    HWND hSub = m_nppData->_scintillaSecondHandle;

    // Get current scintilla
    int which = 0;
    SendMessage(m_nppData->_nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    hSci = (which == 0) ? hMain : hSub;

    if (hSci) {
        SciFnDirect fn = (SciFnDirect)SendMessage(hSci, SCI_GETDIRECTFUNCTION, 0, 0);
        sptr_t ptr = (sptr_t)SendMessage(hSci, SCI_GETDIRECTPOINTER, 0, 0);
        if (fn) {
            fn(ptr, SCI_ENSUREVISIBLE, t.line, 0);   // unfold the segment if folded
            fn(ptr, SCI_GOTOLINE, t.line, 0);

            if (t.length > 0 || t.startCol > 0) {
                // Scintilla addresses BYTES and the tree measured wchar_t, so the
                // column and length are re-measured as UTF-8 against this line's
                // own text. Counting characters instead works until a message
                // carries an accented name, and then every position past it is
                // silently wrong.
                const std::wstring line = getLineW(fn, ptr, t.line);
                const int startCol = (t.startCol <= (int)line.size())
                                     ? t.startCol : (int)line.size();
                const int endCol = (startCol + t.length <= (int)line.size())
                                   ? startCol + t.length : (int)line.size();

                const sptr_t lineStart = fn(ptr, SCI_POSITIONFROMLINE, t.line, 0);
                const sptr_t from = lineStart + utf8Len(line.substr(0, (size_t)startCol));
                const sptr_t to   = lineStart + utf8Len(line.substr(0, (size_t)endCol));
                fn(ptr, SCI_SETSEL, from, to);
                fn(ptr, SCI_SCROLLCARET, 0, 0);
            }
            if (moveFocus) SetFocus(hSci);
        }
    }
}

INT_PTR CALLBACK MessageTreeView::dlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    MessageTreeView* self = s_instance;

    switch (message) {
        case WM_INITDIALOG: {
            // Create TreeView filling the dialog
            RECT rc;
            GetClientRect(hDlg, &rc);
            self->m_hTree = CreateWindowExW(0, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                0, 0, rc.right, rc.bottom,
                hDlg, nullptr, self->m_hInst, nullptr);

            // Set font to match system GUI font
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SendMessageW(self->m_hTree, WM_SETFONT, (WPARAM)hFont, TRUE);
            return TRUE;
        }

        case WM_SIZE: {
            if (self->m_hTree) {
                RECT rc;
                GetClientRect(hDlg, &rc);
                SetWindowPos(self->m_hTree, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);
            }
            return TRUE;
        }

        case WM_NOTIFY: {
            NMHDR* nmhdr = (NMHDR*)lParam;
            if (nmhdr->idFrom != 0) return FALSE;

            // Both paths run the SAME navigation. They used to be two separate
            // implementations, and when the meaning of a node's lParam changed
            // only one of them was updated -- so single click went on calling
            // SCI_GOTOLINE with what had become an index into the target table,
            // and quietly jumped to unrelated lines.
            if (nmhdr->code == TVN_SELCHANGEDW) {
                // Selection changes cover a single click AND arrow-key walking,
                // which is the case that matters when reading down a message.
                // Focus stays in the tree so the next arrow key still lands here.
                if (!self->m_refreshing) self->onTreeClick(false);
                return TRUE;
            }
            if (nmhdr->code == NM_DBLCLK) {
                self->onTreeClick(true);   // "take me there"
                return TRUE;
            }
            return FALSE;
        }

        case WM_DESTROY: {
            self->m_hTree = nullptr;
            return TRUE;
        }
    }
    return FALSE;
}
