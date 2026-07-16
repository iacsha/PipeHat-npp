#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "Docking.h"
#include "npp/PluginInterface.h"
#include "npp/Notepad_plus_msgs.h"

class SegmentDB;
class HL7Lexer;
class ScintillaStyler;

class MessageTreeView {
public:
    MessageTreeView();

    // Create and register the dockable panel
    bool create(HINSTANCE hInst, HWND hParent, NppData* nppData);

    // Refresh tree from the current HL7 buffer
    void refresh(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect,
                 HL7Lexer& sharedLexer, SegmentDB& segDB);

    // Show/hide the panel
    void show();
    void hide();

    HWND hwnd() const { return m_hDlg; }
    bool isVisible() const { return m_visible; }

private:
    HWND m_hDlg = nullptr;
    HWND m_hTree = nullptr;
    HINSTANCE m_hInst = nullptr;
    NppData* m_nppData = nullptr;
    bool m_visible = false;
    bool m_registered = false;

    // Tree node data
    struct TreeNodeData {
        std::wstring segmentId;
        int lineNumber;
        int fieldIndex;  // 0 = segment header, 1+ = field index
        std::wstring fieldName;
    };

    void clearTree();
    HTREEITEM addSegmentNode(const std::wstring& text, int lineNum, LPARAM lparam,
                             HTREEITEM parent = TVI_ROOT);
    HTREEITEM addMessageNode(const std::wstring& text, LPARAM lparam);
    HTREEITEM addFieldNode(HTREEITEM parent, const std::wstring& text, int lineNum, int fieldIdx, LPARAM lparam);

    static INT_PTR CALLBACK dlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static MessageTreeView* s_instance; // singleton for static callback

    void onTreeClick(LPARAM lParam);
};
