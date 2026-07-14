#pragma once
#include <string>
#include <vector>

struct HL7FieldDef {
    int position;       // 1-based field position
    std::wstring name;
    std::wstring dataType;
    bool required;      // R = required, C = conditional
};

struct HL7SegmentDef {
    std::wstring id;            // e.g. "PID", "MSH", "OBR"
    std::wstring name;          // e.g. "Patient Identification"
    std::wstring category;      // "header", "patient", "order", "financial", "pharmacy"
    std::vector<HL7FieldDef> fields;
};

class SegmentDB {
public:
    SegmentDB();

    // Lookup segment definition by ID
    const HL7SegmentDef* lookup(const std::wstring& segmentId) const;

    // Lookup a specific field within a segment
    const HL7FieldDef* lookupField(const std::wstring& segmentId, int fieldPos) const;

    // Get all segment definitions
    const std::vector<HL7SegmentDef>& allSegments() const { return m_segments; }

    // Map category to display color
    static int categoryColor(const std::wstring& category);

private:
    std::vector<HL7SegmentDef> m_segments;
    void initSegments();
    void addSegment(const std::wstring& id, const std::wstring& name, const std::wstring& category,
                    std::initializer_list<HL7FieldDef> fields);
};
