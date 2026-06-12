#pragma once

#include <string>
#include <string_view>
#include <vector>

struct XmlAttr {
    std::string_view name;
    std::string_view value; // raw, entities NOT decoded
};

// Minimal zero-copy pull parser, tuned for OOXML: no DTD, no validation,
// names returned with their prefix as written (e.g. "w:p"). All
// string_views point into the input buffer, which must outlive the parser.
class XmlParser {
public:
    enum class Event { Start, End, Text, Eof };

    explicit XmlParser(std::string_view xml) : m_xml(xml) {}

    Event next();

    // Valid after Start/End events. For Start, also until the next call
    // to next() for attrs().
    std::string_view name() const { return m_name; }
    // Valid after a Text event. Raw text; XML entities are not decoded.
    std::string_view text() const { return m_text; }
    // Valid only immediately after a Start event.
    std::string_view attr(std::string_view name) const;
    const std::vector<XmlAttr>& attrs() const { return m_attrs; }

    // Call immediately after a Start event: consumes everything through the
    // matching End event (handles self-closing elements too).
    void skipElement();

    // Appends `raw` to `out`, decoding the five XML entities and numeric
    // character references (as UTF-8).
    static void appendDecoded(std::string_view raw, std::string& out);

private:
    std::string_view m_xml;
    size_t m_pos = 0;
    std::string_view m_name;
    std::string_view m_text;
    std::vector<XmlAttr> m_attrs;
    bool m_pendingEnd = false; // self-closing tag: deliver End next
};
