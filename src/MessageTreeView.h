#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "FieldTree.h"
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

    // What a tree node points at in the document. Clicking a node used to do
    // nothing but SCI_GOTOLINE, because a node carried only its line number --
    // so clicking OBX-5.3.3 landed you on the OBX line with no indication of
    // which part you had asked about. Each node now owns a range instead.
    //
    // startCol and length are in wchar_t units within the line; Scintilla
    // addresses bytes, so onTreeClick converts before selecting. A length of 0
    // means "no range, just go to the line".
    struct NodeTarget {
        int line = 0;
        int startCol = 0;
        int length = 0;
    };
    std::vector<NodeTarget> m_targets;   // lParam is an index into this, plus 1

    // Record a target and return the lParam that addresses it.
    LPARAM makeTarget(int line, int startCol, int length);
    // Repetition / component / subcomponent nodes under a field. Recursive, so
    // one call hangs the whole subtree.
    void addValueNodes(HTREEITEM parent, const std::vector<hl7tree::Node>& nodes,
                       int line, int fieldStart, int& budget);

    static INT_PTR CALLBACK dlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static MessageTreeView* s_instance; // singleton for static callback

    // Navigate to the node currently selected in the tree.
    //
    // moveFocus is false for a selection change -- a single click or an arrow
    // key -- so the editor scrolls and highlights while the keyboard stays in
    // the tree and the reader can keep walking it. A double click means "take me
    // there", so that one moves focus to the editor.
    void onTreeClick(bool moveFocus);

    // True while refresh() is rebuilding. The TreeView raises selection changes
    // as items are inserted and removed, and acting on those would scroll the
    // editor around while the panel is merely being repopulated.
    bool m_refreshing = false;
};
