#pragma once

// HL7 lexer style indices (0-7)
#define SCE_HL7_DEFAULT 0
#define SCE_HL7_SEGMENT_ID 1
#define SCE_HL7_FIELD_SEP 2
#define SCE_HL7_COMPONENT_SEP 3
#define SCE_HL7_REPEAT_SEP 4
#define SCE_HL7_ESCAPE_SEP 5
#define SCE_HL7_SUBCOMP_SEP 6
#define SCE_HL7_ESCAPE_SEQ 7
#define SCE_HL7_FIELD_VALUE 8
#define SCE_HL7_COMMENT 9
#define SCE_HL7_FIELD_VALUE_ALT 10  // alternate field shade (every other field)

#define HL7_PLUGIN_NAME L"PipeHat"
#define HL7_PLUGIN_VERSION L"1.3.0"
#define HL7_PLUGIN_TAGLINE L"HL7 v2.x for Notepad++"
#define HL7_PLUGIN_DLL L"PipeHat.dll"

// Built-in NPP command IDs assigned to our menu items
// These get allocated by NPP at startup; 0 is "no command"
constexpr int CMD_HL7_ABOUT = 0;
constexpr int CMD_HL7_TOGGLE_FOLD = 1;
