#pragma once
#include <string>
#include <vector>
#include <utility>

struct PHIField {
    std::wstring segmentId;
    int fieldIndex;
    const wchar_t* label;
    const wchar_t* description;
};

class PHIScrubber {
public:
    PHIScrubber();

    // Returns the list of all known PHI fields
    const std::vector<PHIField>& phiFields() const { return m_fields; }

    // Check if a given segment+field is PHI
    bool isPHI(const std::wstring& segmentId, int fieldIdx) const;

    // Get the redaction label for a PHI field
    const wchar_t* getLabel(const std::wstring& segmentId, int fieldIdx) const;

    // Generate fake replacement value for a PHI field type
    std::wstring generateFake(const std::wstring& segmentId, int fieldIdx, const std::wstring& original) const;

private:
    std::vector<PHIField> m_fields;
    void initFieldMap();
};
