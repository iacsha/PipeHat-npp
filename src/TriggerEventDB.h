#pragma once
#include <string>
#include <unordered_map>

// Human-readable decoding of HL7 v2.x message types (MSH-9.1) and trigger event
// codes (MSH-9.2 / EVN-1). Header-only so it needs no CMake wiring — the tables are
// function-local statics, built once on first use. Not version-aware; the common
// v2.3–v2.7 events are covered. Unknown codes return an empty string (caller shows
// the raw value only).
namespace hl7trig {

inline std::wstring messageTypeName(const std::wstring& code) {
    static const std::unordered_map<std::wstring, std::wstring> m = {
        {L"ADT", L"Admit/Discharge/Transfer"},
        {L"ORM", L"Order Message"},
        {L"ORU", L"Observation Result (Unsolicited)"},
        {L"ORR", L"Order Response"},
        {L"OML", L"Laboratory Order"},
        {L"ORL", L"Laboratory Order Response"},
        {L"OUL", L"Unsolicited Laboratory Observation"},
        {L"SIU", L"Scheduling Information (Unsolicited)"},
        {L"SRM", L"Schedule Request Message"},
        {L"SRR", L"Schedule Request Response"},
        {L"MDM", L"Medical Document Management"},
        {L"DFT", L"Detailed Financial Transaction"},
        {L"BAR", L"Add/Change Billing Account"},
        {L"MFN", L"Master File Notification"},
        {L"MFR", L"Master File Response"},
        {L"ACK", L"General Acknowledgment"},
        {L"QRY", L"Query"},
        {L"QBP", L"Query by Parameter"},
        {L"RSP", L"Segment Pattern Response"},
        {L"RAS", L"Pharmacy Administration"},
        {L"RDE", L"Pharmacy Encoded Order"},
        {L"RDS", L"Pharmacy Dispense"},
        {L"RGV", L"Pharmacy Give"},
        {L"VXU", L"Vaccination Record Update"},
        {L"PPR", L"Patient Problem"},
        {L"REF", L"Patient Referral"},
        {L"RRI", L"Return Referral Information"},
    };
    auto it = m.find(code);
    return it != m.end() ? it->second : std::wstring();
}

inline std::wstring triggerEventName(const std::wstring& code) {
    static const std::unordered_map<std::wstring, std::wstring> m = {
        // ADT — A##
        {L"A01", L"Admit / Visit Notification"},
        {L"A02", L"Transfer a Patient"},
        {L"A03", L"Discharge / End Visit"},
        {L"A04", L"Register a Patient"},
        {L"A05", L"Pre-admit a Patient"},
        {L"A06", L"Change an Outpatient to an Inpatient"},
        {L"A07", L"Change an Inpatient to an Outpatient"},
        {L"A08", L"Update Patient Information"},
        {L"A09", L"Patient Departing (Tracking)"},
        {L"A10", L"Patient Arriving (Tracking)"},
        {L"A11", L"Cancel Admit / Visit Notification"},
        {L"A12", L"Cancel Transfer"},
        {L"A13", L"Cancel Discharge / End Visit"},
        {L"A14", L"Pending Admit"},
        {L"A15", L"Pending Transfer"},
        {L"A16", L"Pending Discharge"},
        {L"A17", L"Swap Patients"},
        {L"A18", L"Merge Patient Information"},
        {L"A19", L"Patient Query (QRY/ADR)"},
        {L"A20", L"Bed Status Update"},
        {L"A21", L"Patient Goes on Leave of Absence"},
        {L"A22", L"Patient Returns from Leave of Absence"},
        {L"A23", L"Delete a Patient Record"},
        {L"A24", L"Link Patient Information"},
        {L"A25", L"Cancel Pending Discharge"},
        {L"A26", L"Cancel Pending Transfer"},
        {L"A27", L"Cancel Pending Admit"},
        {L"A28", L"Add Person Information"},
        {L"A29", L"Delete Person Information"},
        {L"A30", L"Merge Person Information"},
        {L"A31", L"Update Person Information"},
        {L"A40", L"Merge Patient (Patient Identifier List)"},
        {L"A44", L"Move Account Information"},
        {L"A45", L"Move Visit Information"},
        {L"A47", L"Change Patient Identifier List"},
        // SIU — S##
        {L"S12", L"New Appointment Booking"},
        {L"S13", L"Appointment Rescheduling"},
        {L"S14", L"Appointment Modification"},
        {L"S15", L"Appointment Cancellation"},
        {L"S16", L"Appointment Discontinuation"},
        {L"S17", L"Appointment Deletion"},
        {L"S18", L"Add Service/Resource on Appointment"},
        {L"S19", L"Modify Service/Resource on Appointment"},
        {L"S20", L"Cancel Service/Resource on Appointment"},
        {L"S21", L"Discontinue Service/Resource on Appointment"},
        {L"S22", L"Delete Service/Resource on Appointment"},
        {L"S23", L"Block Schedule Time Slot(s)"},
        {L"S24", L"Open ('Unblock') Schedule Time Slot(s)"},
        {L"S26", L"Patient Did Not Show for Appointment"},
        // ORU / ORM / lab
        {L"R01", L"Unsolicited Observation Message"},
        {L"O01", L"Order Message"},
        {L"O02", L"Order Response"},
        {L"O21", L"Laboratory Order"},
        {L"O22", L"Laboratory Order Response"},
        // MDM — T##
        {L"T01", L"Original Document Notification"},
        {L"T02", L"Original Document Notification & Content"},
        {L"T04", L"Document Status Change Notification"},
        {L"T06", L"Document Addendum Notification"},
        {L"T08", L"Document Edit Notification"},
        {L"T11", L"Document Cancel Notification"},
        // DFT / financial — P##
        {L"P01", L"Add Patient Account"},
        {L"P03", L"Post Detail Financial Transaction"},
        // Vaccination
        {L"V04", L"Unsolicited Vaccination Record Update"},
    };
    auto it = m.find(code);
    return it != m.end() ? it->second : std::wstring();
}

// Extract the value of a 1-based HL7 field from a raw line by splitting on the field
// separator. isMSH accounts for MSH-1 being the field separator itself (so MSH-N is
// the value after the (N-1)-th separator). No component parsing.
inline std::wstring fieldValueAt(const std::wstring& line, wchar_t fieldSep, bool isMSH, int targetIdx) {
    int wantToken = isMSH ? (targetIdx - 1) : targetIdx;
    if (wantToken < 0) return std::wstring();
    int token = 0;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == fieldSep) {
            if (token == wantToken) return line.substr(start, i - start);
            token++;
            start = i + 1;
        }
    }
    return std::wstring();
}

// Decode MSH-9 "ADT^A01^ADT_A01" into "ADT (Admit/Discharge/Transfer) · A01 Admit / Visit Notification".
inline std::wstring decodeMSH9(const std::wstring& field, wchar_t compSep) {
    std::wstring msgType, event;
    size_t p = field.find(compSep);
    if (p == std::wstring::npos) {
        msgType = field;
    } else {
        msgType = field.substr(0, p);
        size_t q = field.find(compSep, p + 1);
        event = field.substr(p + 1, (q == std::wstring::npos ? field.size() : q) - (p + 1));
    }

    std::wstring out;
    std::wstring mt = messageTypeName(msgType);
    if (!mt.empty())         out += msgType + L" (" + mt + L")";
    else if (!msgType.empty()) out += msgType;

    if (!event.empty()) {
        if (!out.empty()) out += L" \x00B7 "; // ·
        out += event;
        std::wstring ev = triggerEventName(event);
        if (!ev.empty()) out += L" " + ev;
    }
    return out;
}

// Field-level decode hook used by both the tree panel and hover tooltips. Returns an
// empty string for fields that carry no coded meaning.
inline std::wstring decodeField(const std::wstring& segId, int fieldIdx,
                                const std::wstring& rawLine, wchar_t fieldSep, wchar_t compSep) {
    if (segId == L"MSH" && fieldIdx == 9) {
        return decodeMSH9(fieldValueAt(rawLine, fieldSep, true, 9), compSep);
    }
    if (segId == L"EVN" && fieldIdx == 1) {
        std::wstring v = fieldValueAt(rawLine, fieldSep, false, 1);
        size_t p = v.find(compSep);
        if (p != std::wstring::npos) v = v.substr(0, p);
        std::wstring ev = triggerEventName(v);
        if (!ev.empty()) return v + L" " + ev;
    }
    return std::wstring();
}

} // namespace hl7trig
