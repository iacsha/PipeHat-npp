#include "SegmentDB.h"
#include <algorithm>

SegmentDB::SegmentDB() {
    initSegments();
}

void SegmentDB::addSegment(const std::wstring& id, const std::wstring& name, const std::wstring& category,
                           std::initializer_list<HL7FieldDef> fields) {
    HL7SegmentDef seg;
    seg.id = id;
    seg.name = name;
    seg.category = category;
    seg.fields = fields;
    m_segments.push_back(seg);
}

const HL7SegmentDef* SegmentDB::lookup(const std::wstring& segmentId) const {
    for (const auto& seg : m_segments) {
        if (seg.id == segmentId) return &seg;
    }
    return nullptr;
}

const HL7FieldDef* SegmentDB::lookupField(const std::wstring& segmentId, int fieldPos) const {
    const HL7SegmentDef* seg = lookup(segmentId);
    if (!seg) return nullptr;
    for (const auto& f : seg->fields) {
        if (f.position == fieldPos) return &f;
    }
    return nullptr;
}

int SegmentDB::categoryColor(const std::wstring& category) {
    if (category == L"header") return 0x0000CC;    // dark blue
    if (category == L"patient") return 0x0066CC;   // blue
    if (category == L"order") return 0x008800;     // green
    if (category == L"observation") return 0x008800;
    if (category == L"financial") return 0xCC6600; // orange
    if (category == L"pharmacy") return 0x8800CC;  // purple
    return 0x000000; // default
}

void SegmentDB::initSegments() {
    // ── Header ──
    addSegment(L"MSH", L"Message Header", L"header", {
        {1, L"Field Separator", L"ST", true},
        {2, L"Encoding Characters", L"ST", true},
        {3, L"Sending Application", L"HD", false},
        {4, L"Sending Facility", L"HD", false},
        {5, L"Receiving Application", L"HD", false},
        {6, L"Receiving Facility", L"HD", false},
        {7, L"Date/Time of Message", L"DTM", true},
        {8, L"Security", L"ST", false},
        {9, L"Message Type", L"MSG", true},
        {10, L"Message Control ID", L"ST", true},
        {11, L"Processing ID", L"PT", true},
        {12, L"Version ID", L"VID", true},
        {13, L"Sequence Number", L"NM", false},
        {14, L"Continuation Pointer", L"ST", false},
        {15, L"Accept Acknowledgment Type", L"ID", false},
        {16, L"Application Acknowledgment Type", L"ID", false},
        {17, L"Country Code", L"ID", false},
        {18, L"Character Set", L"ID", false},
        {19, L"Principal Language of Message", L"CWE", false},
        {20, L"Alternate Character Set Handling", L"ID", false},
        {21, L"Message Profile Identifier", L"EI", false},
    });

    // ── Patient ──
    addSegment(L"PID", L"Patient Identification", L"patient", {
        {1, L"Set ID", L"SI", false},
        {2, L"Patient ID (External)", L"CX", false},
        {3, L"Patient Identifier List", L"CX", true},
        {4, L"Alternate Patient ID", L"CX", false},
        {5, L"Patient Name", L"XPN", true},
        {6, L"Mother's Maiden Name", L"XPN", false},
        {7, L"Date/Time of Birth", L"DTM", false},
        {8, L"Administrative Sex", L"CWE", true},
        {9, L"Patient Alias", L"XPN", false},
        {10, L"Race", L"CWE", false},
        {11, L"Patient Address", L"XAD", false},
        {12, L"County Code", L"CWE", false},
        {13, L"Phone Number - Home", L"XTN", false},
        {14, L"Phone Number - Business", L"XTN", false},
        {15, L"Primary Language", L"CWE", false},
        {16, L"Marital Status", L"CWE", false},
        {17, L"Religion", L"CWE", false},
        {18, L"Patient Account Number", L"CX", false},
        {19, L"SSN", L"ST", false},
        {20, L"Driver's License", L"DLN", false},
        {21, L"Mother's Identifier", L"CX", false},
        {22, L"Ethnic Group", L"CWE", false},
        {23, L"Birth Place", L"ST", false},
        {24, L"Multiple Birth Indicator", L"ID", false},
        {25, L"Birth Order", L"NM", false},
        {26, L"Citizenship", L"CWE", false},
        {27, L"Veterans Military Status", L"CWE", false},
        {29, L"Patient Death Date and Time", L"DTM", false},
        {30, L"Patient Death Indicator", L"ID", false},
        {31, L"Identity Unknown Indicator", L"ID", false},
        {32, L"Identity Reliability Code", L"CWE", false},
        {33, L"Last Update Date/Time", L"DTM", false},
        {34, L"Last Update Facility", L"HD", false},
        {35, L"Taxonomic Species Code", L"CWE", false},
        {36, L"Breed Code", L"CWE", false},
        {37, L"Strain", L"ST", false},
        {38, L"Production Class Code", L"CWE", false},
        {39, L"Tribal Citizenship", L"CWE", false},
    });

    addSegment(L"PD1", L"Patient Additional Demographic", L"patient", {
        {1, L"Living Dependency", L"CWE", false},
        {2, L"Living Arrangement", L"CWE", false},
        {3, L"Patient Primary Facility", L"XON", false},
        {4, L"Patient Primary Care Provider", L"XCN", false},
        {5, L"Student Indicator", L"CWE", false},
        {6, L"Handicap", L"CWE", false},
        {7, L"Living Will Code", L"CWE", false},
        {8, L"Organ Donor Code", L"CWE", false},
        {9, L"Separate Bill", L"ID", false},
        {10, L"Duplicate Patient", L"CX", false},
        {11, L"Publicity Code", L"CWE", false},
        {12, L"Protection Indicator", L"ID", false},
    });

    addSegment(L"NK1", L"Next of Kin / Associated Parties", L"patient", {
        {1, L"Set ID", L"SI", true},
        {2, L"Name", L"XPN", false},
        {3, L"Relationship", L"CWE", false},
        {4, L"Address", L"XAD", false},
        {5, L"Phone Number", L"XTN", false},
        {6, L"Business Phone Number", L"XTN", false},
        {7, L"Contact Role", L"CWE", false},
        {8, L"Start Date", L"DT", false},
        {9, L"End Date", L"DT", false},
        {10, L"Next of Kin / Associated Parties Job Title", L"ST", false},
        {11, L"Next of Kin / Associated Parties Job Code/Class", L"JCC", false},
        {12, L"Next of Kin / Associated Parties Employee Number", L"CX", false},
        {13, L"Organization Name", L"XON", false},
    });

    addSegment(L"PV1", L"Patient Visit", L"patient", {
        {1, L"Set ID", L"SI", false},
        {2, L"Patient Class", L"CWE", true},
        {3, L"Assigned Patient Location", L"PL", false},
        {4, L"Admission Type", L"CWE", false},
        {5, L"Preadmit Number", L"CX", false},
        {6, L"Prior Patient Location", L"PL", false},
        {7, L"Attending Doctor", L"XCN", false},
        {8, L"Referring Doctor", L"XCN", false},
        {9, L"Consulting Doctor", L"XCN", false},
        {10, L"Hospital Service", L"CWE", false},
        {11, L"Temporary Location", L"PL", false},
        {12, L"Preadmit Test Indicator", L"CWE", false},
        {13, L"Re-admission Indicator", L"CWE", false},
        {14, L"Admit Source", L"CWE", false},
        {15, L"Ambulatory Status", L"CWE", false},
        {16, L"VIP Indicator", L"CWE", false},
        {17, L"Admitting Doctor", L"XCN", false},
        {18, L"Patient Type", L"CWE", false},
        {19, L"Visit Number", L"CX", false},
        {20, L"Financial Class", L"FC", false},
    });

    // ── Order / Observation ──
    addSegment(L"OBR", L"Observation Request", L"order", {
        {1, L"Set ID", L"SI", false},
        {2, L"Placer Order Number", L"EI", false},
        {3, L"Filler Order Number", L"EI", false},
        {4, L"Universal Service Identifier", L"CWE", true},
        {5, L"Priority", L"ID", false},
        {6, L"Requested Date/Time", L"DTM", false},
        {7, L"Observation Date/Time", L"DTM", false},
        {8, L"Observation End Date/Time", L"DTM", false},
        {9, L"Collection Volume", L"CQ", false},
        {10, L"Collector Identifier", L"XCN", false},
        {11, L"Specimen Action Code", L"ID", false},
        {12, L"Danger Code", L"CWE", false},
        {13, L"Relevant Clinical Information", L"ST", false},
        {14, L"Specimen Received Date/Time", L"DTM", false},
        {15, L"Specimen Source", L"SPS", false},
        {16, L"Ordering Provider", L"XCN", false},
        {17, L"Order Callback Phone Number", L"XTN", false},
        {18, L"Placer Field 1", L"ST", false},
        {19, L"Placer Field 2", L"ST", false},
        {20, L"Filler Field 1", L"ST", false},
        {21, L"Filler Field 2", L"ST", false},
        {22, L"Results Rpt/Status Change - Date/Time", L"DTM", false},
        {23, L"Charge to Practice", L"MOC", false},
        {24, L"Diagnostic Serv Sect ID", L"ID", false},
        {25, L"Result Status", L"ID", false},
        {26, L"Parent Result", L"PRL", false},
        {27, L"Quantity/Timing", L"TQ", false},
        {28, L"Result Copies To", L"XCN", false},
        {29, L"Parent Number", L"EIP", false},
        {30, L"Transportation Mode", L"ID", false},
        {31, L"Reason for Study", L"CWE", false},
    });

    addSegment(L"OBX", L"Observation Result", L"observation", {
        {1, L"Set ID", L"SI", false},
        {2, L"Value Type", L"ID", false},
        {3, L"Observation Identifier", L"CWE", true},
        {4, L"Observation Sub-ID", L"ST", false},
        {5, L"Observation Value", L"varies", true},
        {6, L"Units", L"CWE", false},
        {7, L"References Range", L"ST", false},
        {8, L"Abnormal Flags", L"CWE", false},
        {9, L"Probability", L"NM", false},
        {10, L"Nature of Abnormal Test", L"ID", false},
        {11, L"Observation Result Status", L"ID", true},
        {12, L"Effective Date of Reference Range", L"DTM", false},
        {13, L"User Defined Access Checks", L"ST", false},
        {14, L"Date/Time of the Observation", L"DTM", false},
        {15, L"Producer's ID", L"CWE", false},
        {16, L"Responsible Observer", L"XCN", false},
        {17, L"Observation Method", L"CWE", false},
    });

    addSegment(L"ORC", L"Common Order", L"order", {
        {1, L"Order Control", L"ID", true},
        {2, L"Placer Order Number", L"EI", false},
        {3, L"Filler Order Number", L"EI", false},
        {4, L"Placer Group Number", L"EI", false},
        {5, L"Order Status", L"ID", false},
        {6, L"Response Flag", L"ID", false},
        {7, L"Quantity/Timing", L"TQ", false},
        {8, L"Parent", L"EIP", false},
        {9, L"Date/Time of Transaction", L"DTM", false},
        {10, L"Entered By", L"XCN", false},
        {11, L"Verified By", L"XCN", false},
        {12, L"Ordering Provider", L"XCN", false},
        {13, L"Enterer's Location", L"PL", false},
        {14, L"Call Back Phone Number", L"XTN", false},
        {15, L"Order Effective Date/Time", L"DTM", false},
        {16, L"Order Control Code Reason", L"CWE", false},
        {17, L"Entering Organization", L"CWE", false},
        {18, L"Entering Device", L"CWE", false},
        {19, L"Action By", L"XCN", false},
    });

    // ── Notes ──
    addSegment(L"NTE", L"Notes and Comments", L"observation", {
        {1, L"Set ID", L"SI", false},
        {2, L"Source of Comment", L"ID", false},
        {3, L"Comment", L"FT", false},
        {4, L"Comment Type", L"CWE", false},
    });

    // ── Financial ──
    addSegment(L"IN1", L"Insurance", L"financial", {
        {1, L"Set ID", L"SI", true},
        {2, L"Insurance Plan ID", L"CWE", false},
        {3, L"Insurance Company ID", L"CX", true},
        {4, L"Insurance Company Name", L"XON", false},
        {5, L"Insurance Company Address", L"XAD", false},
        {6, L"Insurance Co Contact Person", L"XPN", false},
        {7, L"Insurance Co Phone Number", L"XTN", false},
        {8, L"Group Number", L"ST", false},
        {9, L"Group Name", L"XON", false},
        {10, L"Insured's Group Emp ID", L"CX", false},
        {11, L"Insured's Group Emp Name", L"XON", false},
        {12, L"Plan Effective Date", L"DT", false},
        {13, L"Plan Expiration Date", L"DT", false},
        {14, L"Authorization Information", L"AUI", false},
        {15, L"Plan Type", L"CWE", false},
        {16, L"Name of Insured", L"XPN", false},
        {17, L"Insured's Relationship to Patient", L"CWE", false},
        {18, L"Insured's Date of Birth", L"DTM", false},
        {19, L"Insured's Address", L"XAD", false},
    });

    addSegment(L"GT1", L"Guarantor", L"financial", {
        {1, L"Set ID", L"SI", true},
        {2, L"Guarantor Number", L"CX", false},
        {3, L"Guarantor Name", L"XPN", true},
        {4, L"Guarantor Spouse Name", L"XPN", false},
        {5, L"Guarantor Address", L"XAD", false},
        {6, L"Guarantor Ph Num - Home", L"XTN", false},
        {7, L"Guarantor Ph Num - Business", L"XTN", false},
        {8, L"Guarantor Date/Time Of Birth", L"DTM", false},
        {9, L"Guarantor Administrative Sex", L"CWE", false},
        {10, L"Guarantor Type", L"CWE", false},
        {11, L"Guarantor Relationship", L"CWE", false},
        {12, L"Guarantor SSN", L"ST", false},
    });

    // ── Diagnosis / Allergy ──
    addSegment(L"DG1", L"Diagnosis", L"patient", {
        {1, L"Set ID", L"SI", true},
        {2, L"Diagnosis Coding Method", L"ID", false},
        {3, L"Diagnosis Code", L"CWE", false},
        {4, L"Diagnosis Description", L"ST", false},
        {5, L"Diagnosis Date/Time", L"DTM", false},
        {6, L"Diagnosis Type", L"CWE", false},
        {7, L"Major Diagnostic Category", L"CWE", false},
        {8, L"Diagnostic Related Group", L"CWE", false},
        {9, L"DRG Approval Indicator", L"ID", false},
    });

    addSegment(L"AL1", L"Patient Allergy Information", L"patient", {
        {1, L"Set ID", L"SI", true},
        {2, L"Allergen Type Code", L"CWE", false},
        {3, L"Allergen Code/Mnemonic/Description", L"CWE", true},
        {4, L"Allergy Severity Code", L"CWE", false},
        {5, L"Allergy Reaction Code", L"ST", false},
        {6, L"Identification Date", L"DT", false},
    });

    // ── Pharmacy ──
    addSegment(L"RXA", L"Pharmacy/Treatment Administration", L"pharmacy", {
        {1, L"Give Sub-ID Counter", L"NM", true},
        {2, L"Administration Sub-ID Counter", L"NM", false},
        {3, L"Date/Time Start of Administration", L"DTM", true},
        {4, L"Date/Time End of Administration", L"DTM", false},
        {5, L"Administered Code", L"CWE", true},
        {6, L"Administered Amount", L"NM", true},
        {7, L"Administered Units", L"CWE", false},
    });

    addSegment(L"RXR", L"Pharmacy/Treatment Route", L"pharmacy", {
        {1, L"Route", L"CWE", true},
        {2, L"Administration Site", L"CWE", false},
        {3, L"Administration Device", L"CWE", false},
        {4, L"Administration Method", L"CWE", false},
        {5, L"Routing Instruction", L"CWE", false},
    });

    // ── Acknowledgment ──
    addSegment(L"MSA", L"Message Acknowledgment", L"header", {
        {1, L"Acknowledgment Code", L"ID", true},
        {2, L"Message Control ID", L"ST", true},
        {3, L"Text Message", L"ST", false},
        {4, L"Expected Sequence Number", L"NM", false},
        {5, L"Delayed Acknowledgment Type", L"ID", false},
        {6, L"Error Condition", L"CWE", false},
    });

    addSegment(L"ERR", L"Error", L"header", {
        {1, L"Error Code and Location", L"ELD", false},
        {2, L"Error Location", L"ERL", false},
        {3, L"HL7 Error Code", L"CWE", true},
        {4, L"Severity", L"ID", true},
        {5, L"Application Error Code", L"CWE", false},
        {6, L"Application Error Parameter", L"ST", false},
        {7, L"Diagnostic Information", L"TX", false},
        {8, L"User Message", L"TX", false},
    });

    // ── Merge ──
    addSegment(L"MRG", L"Merge Patient Information", L"patient", {
        {1, L"Prior Patient Identifier List", L"CX", true},
        {2, L"Prior Alternate Patient ID", L"CX", false},
        {3, L"Prior Patient Account Number", L"CX", false},
        {4, L"Prior Patient ID", L"CX", false},
        {5, L"Prior Visit Number", L"CX", false},
        {6, L"Prior Alternate Visit ID", L"CX", false},
        {7, L"Prior Patient Name", L"XPN", false},
    });

    // ── Event ──
    addSegment(L"EVN", L"Event Type", L"header", {
        {1, L"Event Type Code", L"ID", false},
        {2, L"Recorded Date/Time", L"DTM", true},
        {3, L"Date/Time Planned Event", L"DTM", false},
        {4, L"Event Reason Code", L"CWE", false},
        {5, L"Operator ID", L"XCN", false},
        {6, L"Event Occurred", L"DTM", false},
        {7, L"Event Facility", L"HD", false},
    });
}
