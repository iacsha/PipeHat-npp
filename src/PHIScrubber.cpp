#include "PHIScrubber.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cwctype>

PHIScrubber::PHIScrubber() {
    initFieldMap();
}

bool PHIScrubber::isPHI(const std::wstring& segmentId, int fieldIdx) const {
    for (const auto& f : m_fields) {
        if (f.segmentId == segmentId && f.fieldIndex == fieldIdx) return true;
    }
    return false;
}

const wchar_t* PHIScrubber::getLabel(const std::wstring& segmentId, int fieldIdx) const {
    for (const auto& f : m_fields) {
        if (f.segmentId == segmentId && f.fieldIndex == fieldIdx) return f.label;
    }
    return L"[REDACTED]";
}

static const wchar_t* kFakeFirstNames[] = {
    L"James", L"Mary", L"Robert", L"Patricia", L"John", L"Jennifer", L"Michael", L"Linda",
    L"David", L"Barbara", L"William", L"Elizabeth", L"Richard", L"Susan", L"Joseph", L"Jessica",
    L"Thomas", L"Sarah", L"Charles", L"Karen", L"Christopher", L"Lisa", L"Daniel", L"Nancy",
    L"Matthew", L"Betty", L"Anthony", L"Margaret", L"Mark", L"Sandra"
};
static const int kFakeFirstNameCount = 30;

static const wchar_t* kFakeLastNames[] = {
    L"Smith", L"Johnson", L"Williams", L"Brown", L"Jones", L"Garcia", L"Miller", L"Davis",
    L"Rodriguez", L"Martinez", L"Hernandez", L"Lopez", L"Gonzalez", L"Wilson", L"Anderson",
    L"Thomas", L"Taylor", L"Moore", L"Jackson", L"Martin"
};
static const int kFakeLastNameCount = 20;

static const wchar_t* kFakeStreets[] = {
    L"123 Oak St", L"456 Maple Ave", L"789 Pine Rd", L"321 Elm Dr", L"654 Birch Ln",
    L"987 Cedar Ct", L"147 Walnut Way", L"258 Spruce Pl", L"369 Ash Blvd", L"741 Cherry Cir"
};

static const wchar_t* kFakeCities[] = {
    L"Springfield", L"Riverside", L"Franklin", L"Greenville", L"Fairview",
    L"Madison", L"Georgetown", L"Clinton", L"Centerville", L"Lakewood"
};

static const wchar_t* kFakeStates[] = {
    L"CA", L"NY", L"TX", L"FL", L"IL", L"PA", L"OH", L"GA", L"NC", L"MI"
};

static const wchar_t* kFakeOrgs[] = {
    L"General Hospital", L"Community Medical Center", L"Regional Health System",
    L"Memorial Hospital", L"University Medical Center", L"Valley Health",
    L"Blue Cross", L"Aetna Health", L"United Healthcare", L"Cigna"
};

static unsigned int g_fakeSeed = 0;

static unsigned int fakeRandom() {
    // Simple LCG
    g_fakeSeed = g_fakeSeed * 1103515245 + 12345;
    return (g_fakeSeed / 65536) % 32768;
}

static void fakeInit(const std::wstring& seed) {
    g_fakeSeed = 0;
    for (wchar_t c : seed) g_fakeSeed = g_fakeSeed * 31 + (unsigned int)c;
    if (g_fakeSeed == 0) g_fakeSeed = 1;
}

static const wchar_t* pickFake(const wchar_t** list, int count) {
    return list[fakeRandom() % count];
}

static std::wstring fakeName() {
    return std::wstring(pickFake(kFakeFirstNames, kFakeFirstNameCount))
         + L"^" + pickFake(kFakeLastNames, kFakeLastNameCount);
}

static std::wstring fakeID(int digits) {
    std::wstring result;
    for (int i = 0; i < digits; i++) {
        result += L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[fakeRandom() % 36];
    }
    return result;
}

static std::wstring fakeSSN() {
    wchar_t buf[12];
    swprintf_s(buf, L"%03d-%02d-%04d", fakeRandom() % 900 + 100, fakeRandom() % 99 + 1, fakeRandom() % 9000 + 1000);
    return buf;
}

static std::wstring fakePhone() {
    wchar_t buf[15];
    swprintf_s(buf, L"(555) %03d-%04d", fakeRandom() % 900 + 100, fakeRandom() % 9000 + 1000);
    return buf;
}

static std::wstring fakeDOB() {
    int year = 1940 + (fakeRandom() % 60);
    int month = 1 + (fakeRandom() % 12);
    int day = 1 + (fakeRandom() % 28);
    wchar_t buf[9];
    swprintf_s(buf, L"%04d%02d%02d", year, month, day);
    return buf;
}

static std::wstring fakeDate() {
    int year = 2020 + (fakeRandom() % 6);
    int month = 1 + (fakeRandom() % 12);
    int day = 1 + (fakeRandom() % 28);
    wchar_t buf[9];
    swprintf_s(buf, L"%04d%02d%02d", year, month, day);
    return buf;
}

static std::wstring fakeAddress() {
    std::wstring addr = pickFake(kFakeStreets, 10);
    addr += L"^^";
    addr += pickFake(kFakeCities, 10);
    addr += L"^";
    addr += pickFake(kFakeStates, 10);
    addr += L"^";
    wchar_t zip[6];
    swprintf_s(zip, L"%05d", fakeRandom() % 90000 + 10000);
    addr += zip;
    return addr;
}

static std::wstring fakeOrg() {
    return pickFake(kFakeOrgs, 10);
}

static std::wstring fakeApp() {
    const wchar_t* apps[] = { L"EPIC", L"CERNER", L"MEDITECH", L"ATHENA", L"ECLINICAL" };
    return apps[fakeRandom() % 5];
}

static std::wstring fakeFacility() {
    return pickFake(kFakeOrgs, 10);
}

static std::wstring fakeMsgId() {
    return fakeID(20);
}

std::wstring PHIScrubber::generateFake(const std::wstring& segmentId, int fieldIdx, const std::wstring& original) const {
    const wchar_t* label = getLabel(segmentId, fieldIdx);

    if (wcscmp(label, L"[NAME]") == 0)      return fakeName();
    if (wcscmp(label, L"[ID]") == 0)        return fakeID(8);
    if (wcscmp(label, L"[SSN]") == 0)       return fakeSSN();
    if (wcscmp(label, L"[PHONE]") == 0)     return fakePhone();
    if (wcscmp(label, L"[DATE]") == 0)      return fakeDOB();
    if (wcscmp(label, L"[ADDRESS]") == 0)   return fakeAddress();
    if (wcscmp(label, L"[ORG]") == 0)       return fakeOrg();
    if (wcscmp(label, L"[APP]") == 0)       return fakeApp();
    if (wcscmp(label, L"[FACILITY]") == 0)  return fakeFacility();
    if (wcscmp(label, L"[MSGID]") == 0)     return fakeMsgId();
    if (wcscmp(label, L"[ACCT]") == 0)      return fakeID(12);
    if (wcscmp(label, L"[LICENSE]") == 0)   return fakeID(10);
    if (wcscmp(label, L"[LOCATION]") == 0)  return pickFake(kFakeCities, 10);
    if (wcscmp(label, L"[TITLE]") == 0)     return L"Staff";
    if (wcscmp(label, L"[RELATION]") == 0) {
        const wchar_t* rels[] = { L"SPOUSE", L"CHILD", L"PARENT", L"SIBLING", L"SELF" };
        return rels[fakeRandom() % 5];
    }
    if (wcscmp(label, L"[NOTE]") == 0)     return L"Note redacted for privacy.";
    if (wcscmp(label, L"[VALUE]") == 0)    return L"Value redacted.";

    return std::wstring(label);
}

void PHIScrubber::initFieldMap() {
    // ── PID — Patient Identification ──
    m_fields.push_back({L"PID", 2,  L"[ID]",          L"Patient ID (External)"});
    m_fields.push_back({L"PID", 3,  L"[ID]",          L"Patient Identifier List"});
    m_fields.push_back({L"PID", 4,  L"[ID]",          L"Alternate Patient ID"});
    m_fields.push_back({L"PID", 5,  L"[NAME]",        L"Patient Name"});
    m_fields.push_back({L"PID", 6,  L"[NAME]",        L"Mother's Maiden Name"});
    m_fields.push_back({L"PID", 7,  L"[DATE]",        L"Date/Time of Birth"});
    m_fields.push_back({L"PID", 9,  L"[NAME]",        L"Patient Alias"});
    m_fields.push_back({L"PID", 11, L"[ADDRESS]",     L"Patient Address"});
    m_fields.push_back({L"PID", 13, L"[PHONE]",       L"Phone Number - Home"});
    m_fields.push_back({L"PID", 14, L"[PHONE]",       L"Phone Number - Business"});
    m_fields.push_back({L"PID", 18, L"[ACCT]",        L"Patient Account Number"});
    m_fields.push_back({L"PID", 19, L"[SSN]",         L"SSN"});
    m_fields.push_back({L"PID", 20, L"[LICENSE]",     L"Driver's License"});
    m_fields.push_back({L"PID", 21, L"[ID]",          L"Mother's Identifier"});
    m_fields.push_back({L"PID", 23, L"[LOCATION]",    L"Birth Place"});
    m_fields.push_back({L"PID", 29, L"[DATE]",        L"Patient Death Date and Time"});

    // ── PD1 — Additional Demographics ──
    m_fields.push_back({L"PD1", 4,  L"[NAME]",        L"Patient Primary Care Provider"});
    m_fields.push_back({L"PD1", 10, L"[ID]",          L"Duplicate Patient"});

    // ── NK1 — Next of Kin ──
    m_fields.push_back({L"NK1", 2,  L"[NAME]",        L"Next of Kin Name"});
    m_fields.push_back({L"NK1", 4,  L"[ADDRESS]",     L"Next of Kin Address"});
    m_fields.push_back({L"NK1", 5,  L"[PHONE]",       L"Next of Kin Phone"});
    m_fields.push_back({L"NK1", 6,  L"[PHONE]",       L"Next of Kin Business Phone"});
    m_fields.push_back({L"NK1", 10, L"[TITLE]",       L"Next of Kin Job Title"});
    m_fields.push_back({L"NK1", 12, L"[ID]",          L"Next of Kin Employee Number"});
    m_fields.push_back({L"NK1", 13, L"[ORG]",         L"Next of Kin Organization"});

    // ── PV1 — Patient Visit ──
    m_fields.push_back({L"PV1", 7,  L"[NAME]",        L"Attending Doctor"});
    m_fields.push_back({L"PV1", 8,  L"[NAME]",        L"Referring Doctor"});
    m_fields.push_back({L"PV1", 9,  L"[NAME]",        L"Consulting Doctor"});
    m_fields.push_back({L"PV1", 17, L"[NAME]",        L"Admitting Doctor"});
    m_fields.push_back({L"PV1", 19, L"[ID]",          L"Visit Number"});

    // ── GT1 — Guarantor ──
    m_fields.push_back({L"GT1", 2,  L"[ID]",          L"Guarantor Number"});
    m_fields.push_back({L"GT1", 3,  L"[NAME]",        L"Guarantor Name"});
    m_fields.push_back({L"GT1", 4,  L"[NAME]",        L"Guarantor Spouse Name"});
    m_fields.push_back({L"GT1", 5,  L"[ADDRESS]",     L"Guarantor Address"});
    m_fields.push_back({L"GT1", 6,  L"[PHONE]",       L"Guarantor Phone Home"});
    m_fields.push_back({L"GT1", 7,  L"[PHONE]",       L"Guarantor Phone Business"});
    m_fields.push_back({L"GT1", 8,  L"[DATE]",        L"Guarantor DOB"});
    m_fields.push_back({L"GT1", 12, L"[SSN]",         L"Guarantor SSN"});

    // ── IN1 — Insurance ──
    m_fields.push_back({L"IN1", 3,  L"[ID]",          L"Insurance Company ID"});
    m_fields.push_back({L"IN1", 4,  L"[ORG]",         L"Insurance Company Name"});
    m_fields.push_back({L"IN1", 5,  L"[ADDRESS]",     L"Insurance Company Address"});
    m_fields.push_back({L"IN1", 6,  L"[NAME]",        L"Insurance Contact Person"});
    m_fields.push_back({L"IN1", 7,  L"[PHONE]",       L"Insurance Phone"});
    m_fields.push_back({L"IN1", 8,  L"[ID]",          L"Group Number"});
    m_fields.push_back({L"IN1", 9,  L"[ORG]",         L"Group Name"});
    m_fields.push_back({L"IN1", 10, L"[ID]",          L"Insured Group Emp ID"});
    m_fields.push_back({L"IN1", 11, L"[ORG]",         L"Insured Group Emp Name"});
    m_fields.push_back({L"IN1", 16, L"[NAME]",        L"Name of Insured"});
    m_fields.push_back({L"IN1", 17, L"[RELATION]",    L"Insured's Relationship to Patient"});
    m_fields.push_back({L"IN1", 18, L"[DATE]",        L"Insured's DOB"});
    m_fields.push_back({L"IN1", 19, L"[ADDRESS]",     L"Insured's Address"});

    // ── IN2 — Insurance Additional ──
    m_fields.push_back({L"IN2", 4,  L"[ID]",          L"Medicaid Case Number"});
    m_fields.push_back({L"IN2", 7,  L"[ID]",          L"Medicare Health Ins Card Number"});
    m_fields.push_back({L"IN2", 16, L"[NAME]",        L"Employer Contact Person"});
    m_fields.push_back({L"IN2", 23, L"[NAME]",        L"Roommate Name"});
    m_fields.push_back({L"IN2", 24, L"[ADDRESS]",     L"Roommate Address"});
    m_fields.push_back({L"IN2", 50, L"[NAME]",        L"Insured's Spouse Name"});

    // ── Z-segments / custom ──
    m_fields.push_back({L"ZAL", 1, L"[ID]",           L"ZAL Custom ID"});
    m_fields.push_back({L"ZAL", 2, L"[NAME]",         L"ZAL Custom Name"});

    // ── NTE comments ──
    m_fields.push_back({L"NTE", 3,  L"[NOTE]",        L"Comment (may contain PHI)"});

    // ── OBX observation values ──
    m_fields.push_back({L"OBX", 5,  L"[VALUE]",       L"Observation Value (scrubbed if text)"});

    // ── MSH sender/receiver ──
    m_fields.push_back({L"MSH", 3,  L"[APP]",         L"Sending Application"});
    m_fields.push_back({L"MSH", 4,  L"[FACILITY]",    L"Sending Facility"});
    m_fields.push_back({L"MSH", 5,  L"[APP]",         L"Receiving Application"});
    m_fields.push_back({L"MSH", 6,  L"[FACILITY]",    L"Receiving Facility"});
    m_fields.push_back({L"MSH", 10, L"[MSGID]",       L"Message Control ID"});

    // ── OBR — Ordering provider ──
    m_fields.push_back({L"OBR", 10, L"[NAME]",        L"Collector Identifier"});
    m_fields.push_back({L"OBR", 16, L"[NAME]",        L"Ordering Provider"});
    m_fields.push_back({L"OBR", 17, L"[PHONE]",       L"Order Callback Phone"});
    m_fields.push_back({L"OBR", 28, L"[NAME]",        L"Result Copies To"});

    // ── ORC ordering ──
    m_fields.push_back({L"ORC", 10, L"[NAME]",        L"Entered By"});
    m_fields.push_back({L"ORC", 11, L"[NAME]",        L"Verified By"});
    m_fields.push_back({L"ORC", 12, L"[NAME]",        L"Ordering Provider"});
    m_fields.push_back({L"ORC", 14, L"[PHONE]",       L"Call Back Phone Number"});
    m_fields.push_back({L"ORC", 19, L"[NAME]",        L"Action By"});

    // ── EVN operator ──
    m_fields.push_back({L"EVN", 5,  L"[NAME]",        L"Operator ID"});
}
