// Resource IDs shared between resource.rc and the dialog code.
// Dialog 1 (the dockable Message Tree panel) is created dynamically in
// MessageTreeView.cpp and intentionally has no controls declared here.
#pragma once

// Dialog templates
#define IDD_SETTINGS      2   // Settings — conformance rules list
#define IDD_RULE          3   // Add/Edit a single conformance rule

// Settings dialog (IDD_SETTINGS)
#define IDC_RULE_LIST     1001
#define IDC_RULE_ADD      1002
#define IDC_RULE_EDIT     1003
#define IDC_RULE_REMOVE   1004

// Rule editor dialog (IDD_RULE)
#define IDC_RULE_SEG      1010
#define IDC_RULE_FIELD    1011
#define IDC_RULE_MAXCHK   1012
#define IDC_RULE_MAX      1013
#define IDC_RULE_VALCHK   1014
#define IDC_RULE_VALUES   1015
#define IDC_RULE_REQ      1016
