#pragma once
#include <stdint.h>
#include "Sci_Position.h"

typedef uintptr_t uptr_t;
typedef intptr_t sptr_t;

typedef sptr_t (*SciFnDirect)(sptr_t ptr, unsigned int iMessage, uptr_t wParam, sptr_t lParam);

#define INVALID_POSITION -1

// Lexer
#define SCI_SETLEXER 4001
#define SCI_GETLEXER 4002
#define SCI_COLOURISE 4003
#define SCLEX_CONTAINER 0

// Styling
#define SCI_STARTSTYLING 2032
#define SCI_SETSTYLING 2033
#define SCI_SETSTYLINGEX 2073
#define SCI_GETENDSTYLED 2028
#define SCI_CLEARDOCUMENTSTYLE 2005

// Style definitions
#define STYLE_DEFAULT 32
#define STYLE_LASTPREDEFINED 39
#define STYLE_MAX 255

#define SCI_STYLECLEARALL 2050
#define SCI_STYLESETFORE 2051
#define SCI_STYLESETBACK 2052
#define SCI_STYLESETBOLD 2053
#define SCI_STYLESETITALIC 2054
#define SCI_STYLESETSIZE 2055
#define SCI_STYLESETFONT 2056
#define SCI_STYLERESETDEFAULT 2058

// Text access
#define SCI_GETLINE 2153
#define SCI_GETLINECOUNT 2154
#define SCI_GETLENGTH 2006
#define SCI_GETTEXT 2182
#define SCI_SETTEXT 2181
#define SCI_GETTEXTLENGTH 2183
#define SCI_GETCHARAT 2007
#define SCI_GETCURRENTPOS 2008
#define SCI_GETSTYLEAT 2010
#define SCI_GETENDSTYLED 2028
#define SCI_STYLEGETFORE 2481
#define SCN_PAINTED 2013
#define SCI_LINEFROMPOSITION 2166
#define SCI_POSITIONFROMLINE 2167
#define SCI_GETLINEENDPOSITION 2136
#define SCI_LINELENGTH 2350

// Selection
#define SCI_SETCURRENTPOS 2141
#define SCI_GOTOPOS 2025
#define SCI_GOTOLINE 2024

// Read-only
#define SCI_SETREADONLY 2171

// Undo control
#define SCI_BEGINUNDOACTION 2078
#define SCI_ENDUNDOACTION 2079
#define SCI_EMPTYUNDOBUFFER 2175

// Target-based replacement (doesn't modify cursor position)
#define SCI_SETTARGETSTART 2190
#define SCI_SETTARGETEND 2192
#define SCI_SETTARGETRANGE 2686
#define SCI_REPLACETARGET 2194
#define SCI_GETTARGETTEXT 2687
#define SCI_REPLACETARGETRE 2195

// Indicators
#define SCI_SETINDICATORCURRENT 2500
#define SCI_INDICSETSTYLE 2080
#define SCI_INDICSETFORE 2082
#define SCI_SETINDICATORVALUE 2502
#define SCI_INDICATORFILLRANGE 2504
#define SCI_INDICATORCLEARRANGE 2505
#define INDIC_PLAIN 0
#define INDIC_SQUIGGLE 1
#define INDIC_ROUNDBOX 7
#define INDIC_STRAIGHTBOX 8
#define SCI_INDICSETALPHA 2523
#define SCI_INDICSETOUTLINEALPHA 2558

// Mouse / dwell
#define SCI_SETMOUSEDWELLTIME 2264
#define SCI_GETMOUSEDWELLTIME 2265
#define SCI_WORDSTARTPOSITION 2266
#define SCI_WORDENDPOSITION 2267
#define SCI_POSITIONFROMPOINT 2022

// Calltips (used for field tooltips)
#define SCI_CALLTIPSHOW 2200
#define SCI_CALLTIPCANCEL 2201

// Code page
#define SCI_SETCODEPAGE 2037
#define SC_CP_UTF8 65001

// Margins (for folding)
#define SCI_SETMARGINTYPEN 2240
#define SCI_SETMARGINWIDTHN 2242
#define SC_MARGIN_SYMBOL 0
#define SC_MARGIN_NUMBER 1

// Folding
#define SCI_SETFOLDLEVEL 2222
#define SCI_GETFOLDLEVEL 2223
#define SCI_TOGGLEFOLD 2231
#define SC_FOLDLEVELBASE 0x400
#define SC_FOLDLEVELHEADERFLAG 0x2000
#define SC_FOLDLEVELNUMBERMASK 0x0FFF

// Notification codes
#define SCN_STYLENEEDED 2000
#define SCN_MODIFIED 2008
#define SCN_DWELLSTART 2016
#define SCN_DWELLEND 2017
#define SCN_UPDATEUI 2007
#define SC_UPDATE_CONTENT 0x1
#define SC_UPDATE_SELECTION 0x2
#define SCN_CHARADDED 2001
#define SCN_MARGINCLICK 2010

// SCNotification flags
#define SC_MOD_INSERTTEXT 0x1
#define SC_MOD_DELETETEXT 0x2
#define SC_MOD_CHANGESTYLE 0x4
#define SC_MOD_BEFOREINSERT 0x400
#define SC_MOD_BEFOREDELETE 0x800
#define SC_MODEVENTMASKALL 0x7FFFFF
#define SCI_SETMODEVENTMASK 2359

// Find text struct (needed by app)
#define SCI_FINDTEXTFULL 2196
#define SCFIND_WHOLEWORD 0x2
#define SCFIND_MATCHCASE 0x4

// Buffered draw
#define SCI_SETBUFFEREDDRAW 2035

// EOL
#define SCI_SETEOLMODE 2031
#define SC_EOL_CRLF 0
#define SC_EOL_CR 1
#define SC_EOL_LF 2

// Direct function pointer
#define SCI_GETDIRECTFUNCTION 2184
#define SCI_GETDIRECTPOINTER 2185

// Document pointer
#define SCI_GETDOCPOINTER 2357
#define SCI_SETDOCPOINTER 2358

// Allocation
#define SCI_ALLOCATE 2446

struct Sci_NotifyHeader {
    void *hwndFrom;
    uptr_t idFrom;
    unsigned int code;
};

struct SCNotification {
    Sci_NotifyHeader nmhdr;
    Sci_Position position;
    int ch;
    int modifiers;
    int modificationType;
    const char *text;
    Sci_Position length;
    Sci_Position linesAdded;
    int message;
    uptr_t wParam;
    sptr_t lParam;
    Sci_Position line;
    int foldLevelNow;
    int foldLevelPrev;
    int margin;
    int listType;
    int x;
    int y;
    int token;
    int annotationLinesAdded;
    int updated;
    int listCompletionMethod;
    int characterSource;
};
