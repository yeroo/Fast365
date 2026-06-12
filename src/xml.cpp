#include "xml.h"

namespace {

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool isNameEnd(char c) { return isWs(c) || c == '>' || c == '/' || c == '='; }

void appendUtf8(unsigned cp, std::string& out) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

XmlParser::Event XmlParser::next() {
    if (m_pendingEnd) {
        m_pendingEnd = false;
        return Event::End; // m_name still holds the element name
    }
    const size_t size = m_xml.size();
    for (;;) {
        if (m_pos >= size) return Event::Eof;

        if (m_xml[m_pos] != '<') {
            size_t start = m_pos;
            size_t lt = m_xml.find('<', m_pos);
            if (lt == std::string_view::npos) lt = size;
            m_pos = lt;
            m_text = m_xml.substr(start, lt - start);
            return Event::Text;
        }

        m_pos++; // consume '<'
        if (m_pos >= size) return Event::Eof;
        char c = m_xml[m_pos];

        if (c == '/') { // end tag
            m_pos++;
            size_t start = m_pos;
            size_t gt = m_xml.find('>', m_pos);
            if (gt == std::string_view::npos) return Event::Eof;
            size_t end = start;
            while (end < gt && !isWs(m_xml[end])) end++;
            m_name = m_xml.substr(start, end - start);
            m_pos = gt + 1;
            return Event::End;
        }

        if (c == '?') { // processing instruction / xml decl
            size_t e = m_xml.find("?>", m_pos);
            m_pos = (e == std::string_view::npos) ? size : e + 2;
            continue;
        }

        if (c == '!') {
            if (m_xml.compare(m_pos, 3, "!--") == 0) { // comment
                size_t e = m_xml.find("-->", m_pos + 3);
                m_pos = (e == std::string_view::npos) ? size : e + 3;
                continue;
            }
            if (m_xml.compare(m_pos, 8, "![CDATA[") == 0) {
                size_t start = m_pos + 8;
                size_t e = m_xml.find("]]>", start);
                if (e == std::string_view::npos) e = size;
                m_text = m_xml.substr(start, e - start);
                m_pos = (e == size) ? size : e + 3;
                return Event::Text;
            }
            // DOCTYPE etc. — skip to '>' (good enough: OOXML has none)
            size_t e = m_xml.find('>', m_pos);
            m_pos = (e == std::string_view::npos) ? size : e + 1;
            continue;
        }

        // start tag
        size_t start = m_pos;
        while (m_pos < size && !isNameEnd(m_xml[m_pos])) m_pos++;
        m_name = m_xml.substr(start, m_pos - start);
        m_attrs.clear();

        for (;;) {
            while (m_pos < size && isWs(m_xml[m_pos])) m_pos++;
            if (m_pos >= size) return Event::Eof;
            char d = m_xml[m_pos];
            if (d == '>') {
                m_pos++;
                return Event::Start;
            }
            if (d == '/') {
                m_pos++;
                if (m_pos < size && m_xml[m_pos] == '>') m_pos++;
                m_pendingEnd = true;
                return Event::Start;
            }
            // attribute
            size_t as = m_pos;
            while (m_pos < size && !isNameEnd(m_xml[m_pos])) m_pos++;
            std::string_view an = m_xml.substr(as, m_pos - as);
            while (m_pos < size && isWs(m_xml[m_pos])) m_pos++;
            if (m_pos < size && m_xml[m_pos] == '=') {
                m_pos++;
                while (m_pos < size && isWs(m_xml[m_pos])) m_pos++;
                if (m_pos < size &&
                    (m_xml[m_pos] == '"' || m_xml[m_pos] == '\'')) {
                    char q = m_xml[m_pos++];
                    size_t vs = m_pos;
                    size_t ve = m_xml.find(q, m_pos);
                    if (ve == std::string_view::npos) return Event::Eof;
                    m_attrs.push_back({an, m_xml.substr(vs, ve - vs)});
                    m_pos = ve + 1;
                    continue;
                }
            }
            m_attrs.push_back({an, {}}); // malformed/valueless — keep going
        }
    }
}

std::string_view XmlParser::attr(std::string_view name) const {
    for (const auto& a : m_attrs)
        if (a.name == name) return a.value;
    return {};
}

void XmlParser::skipElement() {
    int depth = 1;
    while (depth > 0) {
        Event ev = next();
        if (ev == Event::Eof) return;
        if (ev == Event::Start) depth++;
        else if (ev == Event::End) depth--;
    }
}

void XmlParser::appendDecoded(std::string_view raw, std::string& out) {
    size_t i = 0;
    while (i < raw.size()) {
        char c = raw[i];
        if (c != '&') {
            out += c;
            i++;
            continue;
        }
        size_t semi = raw.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 12) {
            out += c; // stray '&'
            i++;
            continue;
        }
        std::string_view ent = raw.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned cp = 0;
            bool ok = ent.size() > 1;
            if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                for (size_t k = 2; k < ent.size() && ok; k++) {
                    char h = ent[k];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                    else ok = false;
                }
            } else {
                for (size_t k = 1; k < ent.size() && ok; k++) {
                    char d = ent[k];
                    if (d < '0' || d > '9') { ok = false; break; }
                    cp = cp * 10 + static_cast<unsigned>(d - '0');
                }
            }
            if (ok && cp != 0 && cp <= 0x10FFFF) appendUtf8(cp, out);
        } else {
            out.append(raw.substr(i, semi - i + 1)); // unknown entity, keep
        }
        i = semi + 1;
    }
}
