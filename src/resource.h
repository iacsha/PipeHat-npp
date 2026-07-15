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

// Conformance profile selector (IDD_SETTINGS)
#define IDC_PROFILE       1030
#define IDC_PROFILE_NEW   1031

// MLLP (network) section of the settings dialog (IDD_SETTINGS)
#define IDC_MLLP_ENABLE       1020
#define IDC_MLLP_HOST         1021
#define IDC_MLLP_SENDPORT     1022
#define IDC_MLLP_LISTENPORT   1023
#define IDC_MLLP_ALLOWNONLOOP 1024
#define IDC_MLLP_BINDADDR     1025
#define IDC_MLLP_SAVERECV     1026
