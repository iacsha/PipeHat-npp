#pragma once
#include <string>
#include <vector>

// Component names for the HL7 v2.x composite data types, so a tooltip can say
// "NK1-4.3  City" instead of leaving the reader to count carets.
//
// Deliberately partial. This covers the composites that carry the fields people
// actually hover -- names, addresses, identifiers, phone numbers, coded elements,
// locations. A full v2.x data-type dictionary is a different (and much larger)
// artifact; a miss here degrades to "no component name", never to a wrong one.
//
// Header-only so CMakeLists.txt needs no edit, same as TriggerEventDB.h / Validator.h.
namespace hl7dt {

struct ComponentTable {
    const wchar_t* type;
    std::vector<const wchar_t*> components;   // index 0 == component 1
};

inline const std::vector<ComponentTable>& tables() {
    static const std::vector<ComponentTable> t = {
        // Person name
        {L"XPN", {L"Family Name", L"Given Name", L"Middle Name", L"Suffix", L"Prefix",
                  L"Degree", L"Name Type Code", L"Name Representation Code"}},
        {L"PN",  {L"Family Name", L"Given Name", L"Middle Name", L"Suffix", L"Prefix",
                  L"Degree"}},
        // Address
        {L"XAD", {L"Street Address", L"Other Designation", L"City", L"State or Province",
                  L"Zip or Postal Code", L"Country", L"Address Type",
                  L"Other Geographic Designation", L"County/Parish Code",
                  L"Census Tract"}},
        {L"AD",  {L"Street Address", L"Other Designation", L"City", L"State or Province",
                  L"Zip or Postal Code", L"Country", L"Address Type",
                  L"Other Geographic Designation"}},
        {L"SAD", {L"Street or Mailing Address", L"Street Name", L"Dwelling Number"}},
        // Composite name and ID for persons (providers)
        {L"XCN", {L"ID Number", L"Family Name", L"Given Name", L"Middle Name", L"Suffix",
                  L"Prefix", L"Degree", L"Source Table", L"Assigning Authority",
                  L"Name Type Code", L"Identifier Check Digit", L"Check Digit Scheme",
                  L"Identifier Type Code", L"Assigning Facility"}},
        {L"CN",  {L"ID Number", L"Family Name", L"Given Name", L"Middle Name", L"Suffix",
                  L"Prefix", L"Degree"}},
        // Composite name and ID for organizations
        {L"XON", {L"Organization Name", L"Organization Name Type Code", L"ID Number",
                  L"Check Digit", L"Check Digit Scheme", L"Assigning Authority",
                  L"Identifier Type Code", L"Assigning Facility", L"Name Representation Code"}},
        // Identifiers
        {L"CX",  {L"ID Number", L"Check Digit", L"Check Digit Scheme", L"Assigning Authority",
                  L"Identifier Type Code", L"Assigning Facility", L"Effective Date",
                  L"Expiration Date"}},
        {L"EI",  {L"Entity Identifier", L"Namespace ID", L"Universal ID", L"Universal ID Type"}},
        {L"HD",  {L"Namespace ID", L"Universal ID", L"Universal ID Type"}},
        {L"DLN", {L"License Number", L"Issuing State/Province/Country", L"Expiration Date"}},
        // Telecom
        {L"XTN", {L"Telephone Number", L"Telecommunication Use Code",
                  L"Telecommunication Equipment Type", L"Email Address", L"Country Code",
                  L"Area/City Code", L"Local Number", L"Extension", L"Any Text"}},
        {L"TN",  {L"Telephone Number"}},
        // Coded elements
        {L"CE",  {L"Identifier", L"Text", L"Name of Coding System", L"Alternate Identifier",
                  L"Alternate Text", L"Name of Alternate Coding System"}},
        {L"CWE", {L"Identifier", L"Text", L"Name of Coding System", L"Alternate Identifier",
                  L"Alternate Text", L"Name of Alternate Coding System", L"Coding System Version ID",
                  L"Alternate Coding System Version ID", L"Original Text"}},
        {L"CNE", {L"Identifier", L"Text", L"Name of Coding System", L"Alternate Identifier",
                  L"Alternate Text", L"Name of Alternate Coding System", L"Coding System Version ID",
                  L"Alternate Coding System Version ID", L"Original Text"}},
        {L"CF",  {L"Identifier", L"Formatted Text", L"Name of Coding System",
                  L"Alternate Identifier", L"Alternate Formatted Text",
                  L"Name of Alternate Coding System"}},
        // Location
        {L"PL",  {L"Point of Care", L"Room", L"Bed", L"Facility", L"Location Status",
                  L"Person Location Type", L"Building", L"Floor", L"Location Description"}},
        // MSH composites
        {L"MSG", {L"Message Code", L"Trigger Event", L"Message Structure"}},
        {L"VID", {L"Version ID", L"Internationalization Code", L"International Version ID"}},
        {L"PT",  {L"Processing ID", L"Processing Mode"}},
        // Money / quantity / misc composites that show up in financial segments
        {L"CP",  {L"Price", L"Price Type", L"From Value", L"To Value", L"Range Units",
                  L"Range Type"}},
        {L"MO",  {L"Quantity", L"Denomination"}},
        {L"CQ",  {L"Quantity", L"Units"}},
        {L"FN",  {L"Surname", L"Own Surname Prefix", L"Own Surname",
                  L"Surname Prefix From Partner/Spouse", L"Surname From Partner/Spouse"}},
    };
    return t;
}

// Name of a 1-based component of a data type, or empty when the type is not tabled
// or the component is past the end of the table. Empty is a normal answer: the caller
// renders the numeric path alone rather than guessing.
inline std::wstring componentName(const std::wstring& dataType, int component) {
    if (component < 1 || dataType.empty()) return std::wstring();
    for (const auto& tb : tables()) {
        if (dataType == tb.type) {
            if (component > (int)tb.components.size()) return std::wstring();
            return tb.components[component - 1];
        }
    }
    return std::wstring();
}

} // namespace hl7dt
