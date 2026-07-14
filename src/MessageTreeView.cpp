#include "MessageTreeView.h"
#include "HL7Lexer.h"
#include "SegmentDB.h"
#include "ScintillaStyler.h"
#include "SciUtils.h"
#include "PluginDefs.h"
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

void MessageTreeView::clearTree() {
    if (m_hTree) {
        SendMessageW(m_hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    }
}

HTREEITEM MessageTreeView::addSegmentNode(const std::wstring& text, int lineNum, LPARAM lparam) {
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

void MessageTreeView::refresh(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect,
                               HL7Lexer& sharedLexer, SegmentDB& segDB) {
    if (!m_hTree || !fnDirect) return;

    clearTree();

    ScintillaStyler styler;
    styler.init(hScintilla, fnDirect, ptrDirect);

    int lineCount = styler.sciGetLineCount();
    if (lineCount <= 0) return;

    // First find MSH to set delimiters
    bool mshFound = false;
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fnDirect, ptrDirect, li);
        if (wl.empty()) continue;
        if (sharedLexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            sharedLexer.parseMSH(wl.c_str(), (int)wl.size());
            mshFound = true;
            break;
        }
    }
    if (!mshFound) return;

    // Parse all segments
    for (int li = 0; li < lineCount; li++) {
        std::wstring wlStr = getLineW(fnDirect, ptrDirect, li);
        if (wlStr.size() < 3) continue;
        const wchar_t* wl = wlStr.c_str();
        int wlLen = (int)wlStr.size();

        std::wstring segId = sharedLexer.extractSegmentID(wl, wlLen);
        if (segId.empty()) continue;

        const HL7SegmentDef* segDef = segDB.lookup(segId);
        std::wstring segLabel;
        if (segDef) {
            segLabel = segId + L" \x25B6 " + segDef->name; // ▶
        } else {
            segLabel = segId + L" \x25B6 (unknown)";
        }

        HTREEITEM segNode = addSegmentNode(segLabel, li, (LPARAM)(li + 1));

        // Tokenize to find fields
        std::vector<HL7Token> tokens;
        sharedLexer.tokenize(wl, wlLen, tokens);

        // MSH-1 is the field separator itself, so the first value is MSH-2 — start
        // the counter one higher for MSH to keep field labels aligned.
        int fieldIdx = (segId == L"MSH") ? 1 : 0;
        std::wstring fieldValue;
        bool inField = false;

        for (const auto& tok : tokens) {
            if (tok.type == HL7TokenType::FIELD_SEP) {
                if (inField) {
                    // Emit previous field
                    const HL7FieldDef* fd = segDB.lookupField(segId, fieldIdx);
                    std::wstring flabel;
                    if (fd) {
                        flabel = std::to_wstring(fieldIdx) + L": " + fd->name + L"  [" + fd->dataType + L"]";
                    } else {
                        flabel = std::to_wstring(fieldIdx) + L": (unnamed)";
                    }
                    if (!fieldValue.empty()) {
                        flabel += L" = " + fieldValue;
                    }
                    addFieldNode(segNode, flabel, li, fieldIdx, (LPARAM)(fieldIdx));
                    fieldValue.clear();
                }
                inField = false;
                fieldIdx++;
            } else if (tok.type == HL7TokenType::SEGMENT_ID) {
                // Skip — already handled as node label
            } else if (tok.type == HL7TokenType::FIELD_VALUE) {
                inField = true;
                if (!fieldValue.empty()) fieldValue += L" ";
                std::wstring val(wl + tok.startPos, tok.length);
                if (!val.empty()) {
                    fieldValue += val;
                }
            }
        }
        // Emit last field
        if (inField) {
            const HL7FieldDef* fd = segDB.lookupField(segId, fieldIdx);
            std::wstring flabel;
            if (fd) {
                flabel = std::to_wstring(fieldIdx) + L": " + fd->name + L"  [" + fd->dataType + L"]";
            } else {
                flabel = std::to_wstring(fieldIdx) + L": (unnamed)";
            }
            if (!fieldValue.empty()) {
                flabel += L" = " + fieldValue;
            }
            addFieldNode(segNode, flabel, li, fieldIdx, (LPARAM)(fieldIdx));
        }
    }
}

void MessageTreeView::onTreeClick(LPARAM lParam) {
    if (!m_hTree || !m_nppData) return;

    HTREEITEM hItem = (HTREEITEM)SendMessageW(m_hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    if (!hItem) return;

    TVITEMW item;
    memset(&item, 0, sizeof(item));
    item.hItem = hItem;
    item.mask = TVIF_PARAM;
    SendMessageW(m_hTree, TVM_GETITEMW, 0, (LPARAM)&item);

    int lineNumber = (int)item.lParam;

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
            fn(ptr, SCI_GOTOLINE, lineNumber, 0);
            SetFocus(hSci);
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
            if (nmhdr->idFrom == 0 && nmhdr->code == NM_DBLCLK) {
                self->onTreeClick(lParam);
            }
            // Also handle single click for navigation
            if (nmhdr->idFrom == 0 && nmhdr->code == TVN_SELCHANGEDW) {
                NMTREEVIEWW* pnmtv = (NMTREEVIEWW*)lParam;
                if (pnmtv->itemNew.hItem) {
                    TVITEMW item;
                    memset(&item, 0, sizeof(item));
                    item.hItem = pnmtv->itemNew.hItem;
                    item.mask = TVIF_PARAM;
                    SendMessageW(self->m_hTree, TVM_GETITEMW, 0, (LPARAM)&item);
                    int lineNumber = (int)item.lParam;

                    if (lineNumber > 0 && self->m_nppData) {
                        int which = 0;
                        SendMessage(self->m_nppData->_nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
                        HWND hSci = (which == 0) ? self->m_nppData->_scintillaMainHandle
                                                  : self->m_nppData->_scintillaSecondHandle;
                        if (hSci) {
                            SciFnDirect fn = (SciFnDirect)SendMessage(hSci, SCI_GETDIRECTFUNCTION, 0, 0);
                            sptr_t ptr = (sptr_t)SendMessage(hSci, SCI_GETDIRECTPOINTER, 0, 0);
                            if (fn) {
                                fn(ptr, SCI_GOTOLINE, lineNumber, 0);
                                SetFocus(hSci);
                            }
                        }
                    }
                }
            }
            return TRUE;
        }

        case WM_DESTROY: {
            self->m_hTree = nullptr;
            return TRUE;
        }
    }
    return FALSE;
}
