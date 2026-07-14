# HL7 v2 Message Grammar Specification

## Top-Level Structure

```
message    := segment (CR segment)*
segment    := segment_id field (field_sep field)*
field      := component (component_sep component)*
component  := subcomponent (subcomp_sep subcomponent)*
subcomponent := value

segment_id := 3 uppercase alpha chars
value      := any chars except delimiters | escape sequences
```

## Delimiters (from MSH-1 and MSH-2)

```
MSH-1  = field_sep      (default: '|')
MSH-2  = encoding_chars (default: '^~\&')
          └─ comp_sep   (position 0: '^')
          └─ repeat_sep (position 1: '~')
          └─ escape_sep (position 2: '\')
          └─ subcomp_sep(position 3: '&')
```

**Critical:** Every message can define its own delimiters. The lexer must read MSH-1 and MSH-2 before tokenizing the rest of the message. The existing UDL hardcodes `|^~\&` and breaks on non-standard separators.

## Segment Terminator

HL7 uses `\r` (0x0D, carriage return) as the segment terminator. Some systems also emit `\n` or `\r\n`. The lexer should handle all three.

```
segment_terminator := CR | LF | CRLF
```

## Escape Sequences

```
escape_seq := escape_sep + escape_code + escape_sep

escape_codes:
  \F\  field separator
  \S\  component separator
  \T\  subcomponent separator
  \R\  repeat separator
  \E\  escape character
  \Xdddd\  hexadecimal data (variable length)
  \Zdddd\  locally defined escape
  \.br\  line break
  \.sk\  skip (ignore this component)
  \.ce\  end highlighting
  \Cxxyy\  character set switch
  \Mxxyyzz\  multi-byte character set switch
  \H\  start highlighting
  \N\  normal text (end highlighting)
```

## MSH Segment (Message Header) — Always First

| Field | Name | Notes |
|-------|------|-------|
| MSH-1 | Field Separator | The char after 'MSH' — defines the pipe |
| MSH-2 | Encoding Characters | 4 chars: component, repeat, escape, subcomponent |
| MSH-3 | Sending Application | |
| MSH-4 | Sending Facility | |
| MSH-5 | Receiving Application | |
| MSH-6 | Receiving Facility | |
| MSH-7 | Date/Time of Message | |
| MSH-8 | Security | |
| MSH-9 | Message Type | `ADT^A01` — component structure |
| MSH-10 | Message Control ID | |
| MSH-11 | Processing ID | |
| MSH-12 | Version ID | `2.5.1` — drives segment definition lookup |

## Common Segment Definitions (v2.7 subset)

### PID — Patient Identification

| Field | Name | DataType | Req |
|-------|------|----------|-----|
| PID-1 | Set ID | SI | O |
| PID-2 | Patient ID (External) | CX | X |
| PID-3 | Patient Identifier List | CX[] | R |
| PID-4 | Alternate Patient ID | CX[] | X |
| PID-5 | Patient Name | XPN[] | R |
| PID-6 | Mother's Maiden Name | XPN[] | O |
| PID-7 | Date/Time of Birth | DTM | O |
| PID-8 | Administrative Sex | CWE | R |
| PID-9 | Patient Alias | XPN[] | X |
| PID-10 | Race | CWE[] | O |
| PID-11 | Patient Address | XAD[] | O |
| PID-12 | County Code | CWE | X |
| PID-13 | Phone Number - Home | XTN[] | O |
| PID-14 | Phone Number - Business | XTN[] | O |
| PID-15 | Primary Language | CWE | O |
| PID-16 | Marital Status | CWE | O |
| PID-17 | Religion | CWE | O |
| PID-18 | Patient Account Number | CX | O |
| PID-19 | SSN | ST | X |
| PID-20 | Driver's License | DLN | X |
| PID-21-39 | (various) | | |

### OBR — Observation Request

| Field | Name | DataType | Req |
|-------|------|----------|-----|
| OBR-1 | Set ID | SI | O |
| OBR-2 | Placer Order Number | EI | C |
| OBR-3 | Filler Order Number | EI | C |
| OBR-4 | Universal Service ID | CWE | R |
| OBR-5 | Priority | ID | X |
| OBR-6 | Requested Date/Time | DTM | X |
| OBR-7 | Observation Date/Time | DTM | C |
| OBR-8 | Observation End Date/Time | DTM | O |
| OBR-9-50 | (various) | | |

### OBX — Observation Result

| Field | Name | DataType | Req |
|-------|------|----------|-----|
| OBX-1 | Set ID | SI | O |
| OBX-2 | Value Type | ID | C |
| OBX-3 | Observation Identifier | CWE | R |
| OBX-4 | Observation Sub-ID | ST | C |
| OBX-5 | Observation Value | varies | R |
| OBX-6 | Units | CWE | O |
| OBX-7 | References Range | ST | O |
| OBX-8 | Abnormal Flags | CWE[] | X |
| OBX-9 | Probability | NM | X |
| OBX-10 | Nature of Abnormal Test | ID[] | X |
| OBX-11 | Observation Result Status | ID | R |
| OBX-12-25 | (various) | | |

## Data Types (subset)

| Code | Name | Structure |
|------|------|-----------|
| ST | String | plain text |
| NM | Numeric | number |
| SI | Sequence ID | non-negative integer |
| ID | Coded Value | from HL7 table |
| IS | Coded Value User | from user-defined table |
| DT | Date | YYYY[MM[DD]] |
| DTM | Date/Time | YYYY[MM[DD[HH[MM[SS]]]]] |
| CWE | Coded with Exceptions | `identifier^text^nameOfCodingSystem^altId^altText^nameOfAltCodingSystem^codingSystemVersionId^altCodingSystemVersionId^originalText` |
| CX | Extended Composite ID | `id^checkDigit^checkDigitScheme^assigningAuthority^identifierTypeCode^assigningFacility^effectiveDate^expirationDate` |
| XPN | Extended Person Name | `familyName^givenName^middleName^suffix^prefix^degree^nameTypeCode^nameRepresentationCode^nameContext^nameValidityRange^nameAssemblyOrder^effectiveDate^expirationDate^professionalSuffix` |
| XAD | Extended Address | `street^otherDesignation^city^state^zip^country^addressType^otherGeographicDesignation^countyCode^censusTract^addressRepresentationCode^addressValidityRange^effectiveDate^expirationDate^expirationReason^temporaryIndicator^badAddressIndicator^addressUsage^addressee^comment^preferenceIndicator^protectionIndicator^mapUrl` |
| XTN | Extended Telecom | complex |
| EI | Entity Identifier | `entityId^namespaceId^universalId^universalIdType` |

## Message Type / Event Matrix (common)

| Type | Event | Description |
|------|-------|-------------|
| ADT | A01 | Admit/Visit Notification |
| ADT | A02 | Transfer a Patient |
| ADT | A03 | Discharge/End Visit |
| ADT | A04 | Register a Patient |
| ADT | A05 | Pre-Admit a Patient |
| ADT | A08 | Update Patient Information |
| ADT | A31 | Update Person Information |
| ORM | O01 | Order Message |
| ORU | R01 | Unsolicited Observation Result |
| MDM | T02 | Original Document Notification |
| SIU | S12 | Notification of New Appointment |
| ACK | (any) | General Acknowledgment |

## Lexer Edge Cases

1. **Empty fields:** `||` is valid — two adjacent separators = empty field
2. **Truncated segments:** Last field may omit trailing separators
3. **Repeated fields:** `value1~value2~value3` — repeat separator inside a field
4. **Escaped separators:** `Dr. Smith\| MD` — the `\F\` means a literal pipe
5. **Non-standard MSH-1:** `MSH|` vs `MSH^` — some systems use non-standard field sep
6. **FHS/BHS/FSH/BSH:** Batch wrappers — segments outside the message body
7. **Continuation segments:** ADD (addendum) and various continuation patterns
8. **Z-segments:** Custom segments — lex as SEGMENT_ID but no field defs available
