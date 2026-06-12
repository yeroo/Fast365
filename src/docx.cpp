// WordprocessingML -> HTML conversion. Single streaming pass over
// word/document.xml with small lookup tables built from styles.xml,
// numbering.xml and the relationship parts. Tables are buffered into a
// grid model so vertical merges (vMerge) become rowspans.

#include "docx.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "html_util.h"
#include "xml.h"
#include "zip.h"

namespace {

using Event = XmlParser::Event;

int svToInt(std::string_view s) {
    int v = 0;
    bool neg = false;
    size_t i = 0;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        i = 1;
    }
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10 + (s[i] - '0');
    }
    return neg ? -v : v;
}

unsigned svToHex(std::string_view s) {
    unsigned v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return 0;
    }
    return v;
}

std::string toLower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::string decodeAttr(std::string_view raw) {
    std::string s;
    XmlParser::appendDecoded(raw, s);
    return s;
}

// "w:val" in {"0","false","none","off"} means an explicitly disabled toggle.
bool toggleOn(std::string_view val) {
    return !(val == "0" || val == "false" || val == "none" || val == "off");
}

std::string_view mapAlign(std::string_view jc) {
    if (jc == "center") return "center";
    if (jc == "right" || jc == "end") return "right";
    if (jc == "both" || jc == "distribute") return "justify";
    if (jc == "left" || jc == "start") return "left";
    return {};
}

// w:highlight uses a fixed name set; all are valid CSS except darkYellow.
std::string highlightCss(std::string_view v) {
    if (v == "darkYellow") return "#808000";
    return toLower(v);
}

void appendPt(std::string& css, const char* prop, int twips) {
    if (twips == 0) return;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s:%gpt;", prop, twips / 20.0);
    css += buf;
}

std::string_view mimeForPath(std::string_view path) {
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string_view::npos
                          ? std::string()
                          : toLower(path.substr(dot + 1));
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "bmp") return "image/bmp";
    if (ext == "webp") return "image/webp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "tif" || ext == "tiff") return "image/tiff";
    if (ext == "emf") return "image/emf";
    if (ext == "wmf") return "image/wmf";
    return "application/octet-stream";
}

// Resolves `target` relative to `baseDir` ("word/"), collapsing "..".
std::string resolveZipPath(std::string_view baseDir, std::string_view target) {
    std::string full;
    if (!target.empty() && target[0] == '/') {
        full = std::string(target.substr(1));
    } else {
        full = std::string(baseDir);
        full += target;
    }
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= full.size()) {
        size_t slash = full.find('/', pos);
        if (slash == std::string::npos) slash = full.size();
        std::string_view part(full.data() + pos, slash - pos);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.emplace_back(part);
        }
        pos = slash + 1;
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

// Parses a HYPERLINK field instruction:
//   HYPERLINK "url" [\l "bookmark"] [\o "tooltip"] [\t "target"] ...
// Returns the href, or empty if the instruction is not a hyperlink.
std::string hyperlinkFromInstr(std::string_view instr) {
    size_t i = 0;
    const size_t n = instr.size();
    auto skipWs = [&] {
        while (i < n && (instr[i] == ' ' || instr[i] == '\t' ||
                         instr[i] == '\r' || instr[i] == '\n'))
            i++;
    };
    skipWs();
    size_t ks = i;
    while (i < n && instr[i] != ' ' && instr[i] != '\t') i++;
    if (toLower(instr.substr(ks, i - ks)) != "hyperlink") return {};

    std::string url, anchor;
    char pending = 0; // flag whose argument the next token is
    while (i < n) {
        skipWs();
        if (i >= n) break;
        if (instr[i] == '\\') {
            char f = (i + 1 < n) ? instr[i + 1] : 0;
            i += 2;
            pending = (f == 'l' || f == 'o' || f == 't') ? f : 0;
            continue;
        }
        std::string tok;
        if (instr[i] == '"') {
            size_t e = instr.find('"', i + 1);
            if (e == std::string_view::npos) e = n;
            tok = std::string(instr.substr(i + 1, e - i - 1));
            i = (e == n) ? n : e + 1;
        } else {
            size_t s = i;
            while (i < n && instr[i] != ' ' && instr[i] != '\t') i++;
            tok = std::string(instr.substr(s, i - s));
        }
        if (pending == 'l') anchor = tok;
        else if (pending == 'o' || pending == 't') { /* tooltip/frame: drop */ }
        else if (url.empty()) url = tok;
        pending = 0;
    }
    if (url.empty() && anchor.empty()) return {};
    if (url.empty()) return "#" + anchor;
    if (!anchor.empty()) return url + "#" + anchor;
    return url;
}

struct Relationship {
    std::string target;
    bool external = false;
};

struct RunProps {
    bool b = false, i = false, u = false, strike = false;
    bool caps = false, smallCaps = false, vanish = false;
    int vert = 0; // 1 = superscript, -1 = subscript
    std::string color;     // hex without '#', from w:color
    std::string highlight; // named color, from w:highlight
};

// Applies one run-property element (shared between direct formatting in
// w:rPr and character-style definitions in styles.xml).
void applyRunProp(RunProps& rp, std::string_view n, std::string_view val) {
    bool on = toggleOn(val);
    if (n == "w:b") rp.b = on;
    else if (n == "w:i") rp.i = on;
    else if (n == "w:u") rp.u = on;
    else if (n == "w:strike" || n == "w:dstrike") rp.strike = on;
    else if (n == "w:caps") rp.caps = on;
    else if (n == "w:smallCaps") rp.smallCaps = on;
    else if (n == "w:vanish" || n == "w:webHidden") rp.vanish = on;
    else if (n == "w:vertAlign")
        rp.vert = (val == "superscript") ? 1 : (val == "subscript") ? -1 : 0;
    else if (n == "w:color") {
        if (!val.empty() && val != "auto") rp.color = std::string(val);
    } else if (n == "w:highlight") {
        if (!val.empty() && val != "none") rp.highlight = std::string(val);
    }
}

struct ParaProps {
    int heading = 0; // 1..6, 0 = body text
    std::string_view align;
    int numId = -1; // -1 = unset, 0 = explicitly none
    int ilvl = 0;
    bool rtl = false;
    std::string styleId;
    std::string css; // indentation, shading
};

struct CellData {
    std::string html;
    std::string css;
    int colspan = 1;
    int vmerge = 0; // 0 = none, 1 = restart, 2 = continue
};

struct RowData {
    std::vector<CellData> cells;
    bool header = false;
};

// Complex field (w:fldChar begin/separate/end) state.
struct Field {
    std::string instr;
    bool collecting = true; // between begin and separate
    bool open = false;      // we emitted an <a> that needs closing
};

class Converter {
public:
    explicit Converter(const ConvertOptions& opts) : m_opts(opts) {}

    bool run(const uint8_t* data, size_t size, std::string& htmlOut,
             std::string& error);

private:
    // --- part loading -------------------------------------------------
    bool loadPart(const std::string& path, std::vector<uint8_t>& buf) {
        const ZipEntry* e = m_zip.find(path);
        if (!e) return false;
        return m_zip.extract(*e, buf);
    }
    static std::string_view asView(const std::vector<uint8_t>& buf) {
        return {reinterpret_cast<const char*>(buf.data()), buf.size()};
    }

    std::string findDocumentPath();
    void parseRels(std::string_view xml);
    void parseStyles(std::string_view xml);
    void parseNumbering(std::string_view xml);
    void parseCoreProps(std::string_view xml);

    // --- document traversal -------------------------------------------
    void convertBody(std::string_view xml);
    void parseBlocks(XmlParser& xp, std::string_view endName);
    void parseBlockChild(XmlParser& xp, std::string_view name);
    void parseParagraph(XmlParser& xp);
    void parseParaProps(XmlParser& xp, ParaProps& pp);
    void parseInline(XmlParser& xp, std::string_view name);
    void parseInlineChildren(XmlParser& xp, std::string_view endName);
    void parseRun(XmlParser& xp);
    void parseRunChildren(XmlParser& xp, std::string_view endName, RunProps& rp,
                          bool& opened, std::string& close);
    void parseRunProps(XmlParser& xp, RunProps& rp);
    void parseText(XmlParser& xp);
    void parseHyperlink(XmlParser& xp);
    void parseDrawing(XmlParser& xp);

    // --- tables ----------------------------------------------------------
    void parseTable(XmlParser& xp);
    void collectRows(XmlParser& xp, std::string_view endName,
                     std::vector<RowData>& rows);
    void collectRowChildren(XmlParser& xp, std::string_view endName,
                            RowData& row);
    void collectCell(XmlParser& xp, CellData& cell);
    void parseCellProps(XmlParser& xp, CellData& cell);
    void emitTable(std::vector<RowData>& rows);

    // --- fields, notes, symbols ------------------------------------------
    void handleFldChar(XmlParser& xp);
    void handleInstrText(XmlParser& xp);
    void emitSym(XmlParser& xp);
    void emitNoteRef(XmlParser& xp, bool footnote, bool alreadySuper);
    void appendNotesSection(const char* partName, bool footnote);

    // --- emission helpers ----------------------------------------------
    std::string openParagraph(const ParaProps& pp);
    static void buildRunTags(const RunProps& rp, std::string& open,
                             std::string& close);
    void emitImage(const std::string& relId, long cx, long cy);

    // --- list state ------------------------------------------------------
    struct OpenList {
        const char* tag; // "ul" or "ol"
        bool liOpen;
    };
    std::string numFmtFor(int numId, int ilvl);
    int startFor(int numId, int ilvl);
    static uint64_t listKey(int numId, int ilvl) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(numId)) << 8) |
               static_cast<uint32_t>(ilvl & 0xFF);
    }
    void setListState(int numId, int ilvl);
    void closeOneList();
    void closeLists(size_t target);

    struct StyleNum {
        int numId = -1;
        int ilvl = 0;
    };

    // Pathological nesting (fuzzers, hostile files) must not overflow the
    // stack: content deeper than this is skipped.
    static constexpr int kMaxDepth = 128;
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& depth) : d(depth) { ++d; }
        ~DepthGuard() { --d; }
    };
    int m_depth = 0;

    const ConvertOptions& m_opts;
    ZipArchive m_zip;
    std::string m_baseDir; // directory of the main part, e.g. "word/"
    std::unordered_map<std::string, Relationship> m_rels;
    std::unordered_map<std::string, int> m_headingByStyle;
    std::unordered_map<std::string, StyleNum> m_styleNum;
    std::unordered_map<std::string, RunProps> m_charStyle;
    std::unordered_map<int, int> m_numToAbstract;
    std::unordered_map<int, std::unordered_map<int, std::string>> m_abstractFmt;
    std::unordered_map<int, std::unordered_map<int, int>> m_abstractStart;
    std::unordered_map<int, std::unordered_map<int, std::string>> m_numFmtOverride;
    std::unordered_map<int, std::unordered_map<int, int>> m_numStartOverride;
    // items already emitted per (numId, ilvl) — drives <ol start="...">
    std::unordered_map<uint64_t, int> m_listEmitted;
    std::string m_title;
    std::vector<OpenList> m_listStack;
    std::vector<Field> m_fields;
    std::vector<std::string> m_fnOrder, m_enOrder; // note ids in ref order
    std::string m_out;
};

// ---------------------------------------------------------------------------
// Part loading and lookup tables

std::string Converter::findDocumentPath() {
    std::vector<uint8_t> buf;
    if (loadPart("_rels/.rels", buf)) {
        XmlParser xp(asView(buf));
        for (;;) {
            Event ev = xp.next();
            if (ev == Event::Eof) break;
            if (ev != Event::Start || xp.name() != "Relationship") continue;
            std::string_view type = xp.attr("Type");
            if (type.size() >= 15 &&
                type.substr(type.size() - 15) == "/officeDocument") {
                std::string target = decodeAttr(xp.attr("Target"));
                return resolveZipPath("", target);
            }
        }
    }
    return "word/document.xml";
}

void Converter::parseRels(std::string_view xml) {
    XmlParser xp(xml);
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev != Event::Start || xp.name() != "Relationship") continue;
        Relationship rel;
        rel.target = decodeAttr(xp.attr("Target"));
        rel.external = xp.attr("TargetMode") == "External";
        m_rels.emplace(std::string(xp.attr("Id")), std::move(rel));
    }
}

void Converter::parseStyles(std::string_view xml) {
    XmlParser xp(xml);
    std::string curId, curType;
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::End) {
            if (xp.name() == "w:style") curId.clear();
            continue;
        }
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();
        if (n == "w:style") {
            curId = std::string(xp.attr("w:styleId"));
            curType = std::string(xp.attr("w:type"));
            // Fallback for localized style names: ids "Heading1".."Heading6"
            if (curType == "paragraph" && curId.size() == 8 &&
                curId.compare(0, 7, "Heading") == 0 && curId[7] >= '1' &&
                curId[7] <= '6') {
                m_headingByStyle[curId] = curId[7] - '0';
            }
            continue;
        }
        if (curId.empty()) continue;
        if (curType == "paragraph") {
            if (n == "w:name") {
                std::string nm = toLower(xp.attr("w:val"));
                int lvl = 0;
                if (nm == "title") lvl = 1;
                else if (nm.size() == 9 && nm.compare(0, 8, "heading ") == 0 &&
                         nm[8] >= '1' && nm[8] <= '6')
                    lvl = nm[8] - '0';
                if (lvl) m_headingByStyle[curId] = lvl;
            } else if (n == "w:outlineLvl") {
                int lvl = svToInt(xp.attr("w:val"));
                if (lvl >= 0 && lvl <= 5 && !m_headingByStyle.count(curId))
                    m_headingByStyle[curId] = lvl + 1;
            } else if (n == "w:numId") { // style-bound numbering (List Bullet)
                m_styleNum[curId].numId = svToInt(xp.attr("w:val"));
            } else if (n == "w:ilvl") {
                m_styleNum[curId].ilvl = svToInt(xp.attr("w:val"));
            }
        } else if (curType == "character") {
            applyRunProp(m_charStyle[curId], n, xp.attr("w:val"));
        }
    }
}

void Converter::parseNumbering(std::string_view xml) {
    XmlParser xp(xml);
    int curAbstract = -1, curLvl = -1, curNum = -1;
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::Start) {
            std::string_view n = xp.name();
            if (n == "w:abstractNum") {
                curAbstract = svToInt(xp.attr("w:abstractNumId"));
            } else if (n == "w:lvl" || n == "w:lvlOverride") {
                curLvl = svToInt(xp.attr("w:ilvl"));
            } else if (n == "w:numFmt" && curLvl >= 0) {
                if (curAbstract >= 0)
                    m_abstractFmt[curAbstract][curLvl] =
                        std::string(xp.attr("w:val"));
                else if (curNum >= 0) // redefined inside w:lvlOverride
                    m_numFmtOverride[curNum][curLvl] =
                        std::string(xp.attr("w:val"));
            } else if ((n == "w:start" || n == "w:startOverride") &&
                       curLvl >= 0) {
                if (curAbstract >= 0)
                    m_abstractStart[curAbstract][curLvl] =
                        svToInt(xp.attr("w:val"));
                else if (curNum >= 0)
                    m_numStartOverride[curNum][curLvl] =
                        svToInt(xp.attr("w:val"));
            } else if (n == "w:num") {
                curNum = svToInt(xp.attr("w:numId"));
            } else if (n == "w:abstractNumId" && curNum >= 0) {
                m_numToAbstract[curNum] = svToInt(xp.attr("w:val"));
            }
        } else if (ev == Event::End) {
            std::string_view n = xp.name();
            if (n == "w:abstractNum") { curAbstract = -1; curLvl = -1; }
            else if (n == "w:lvl" || n == "w:lvlOverride") curLvl = -1;
            else if (n == "w:num") curNum = -1;
        }
    }
}

void Converter::parseCoreProps(std::string_view xml) {
    XmlParser xp(xml);
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev != Event::Start || xp.name() != "dc:title") continue;
        std::string raw;
        for (;;) {
            Event e2 = xp.next();
            if (e2 == Event::Eof || e2 == Event::End) break;
            if (e2 == Event::Text) raw.append(xp.text());
        }
        XmlParser::appendDecoded(raw, m_title);
        return;
    }
}

// ---------------------------------------------------------------------------
// List state

std::string Converter::numFmtFor(int numId, int ilvl) {
    auto ovIt = m_numFmtOverride.find(numId);
    if (ovIt != m_numFmtOverride.end()) {
        auto lvlIt = ovIt->second.find(ilvl);
        if (lvlIt != ovIt->second.end()) return lvlIt->second;
    }
    auto numIt = m_numToAbstract.find(numId);
    if (numIt != m_numToAbstract.end()) {
        auto absIt = m_abstractFmt.find(numIt->second);
        if (absIt != m_abstractFmt.end()) {
            auto lvlIt = absIt->second.find(ilvl);
            if (lvlIt != absIt->second.end()) return lvlIt->second;
        }
    }
    return "decimal";
}

int Converter::startFor(int numId, int ilvl) {
    auto ovIt = m_numStartOverride.find(numId);
    if (ovIt != m_numStartOverride.end()) {
        auto lvlIt = ovIt->second.find(ilvl);
        if (lvlIt != ovIt->second.end()) return lvlIt->second;
    }
    auto numIt = m_numToAbstract.find(numId);
    if (numIt != m_numToAbstract.end()) {
        auto absIt = m_abstractStart.find(numIt->second);
        if (absIt != m_abstractStart.end()) {
            auto lvlIt = absIt->second.find(ilvl);
            if (lvlIt != absIt->second.end()) return lvlIt->second;
        }
    }
    return 1;
}

void Converter::closeOneList() {
    OpenList& top = m_listStack.back();
    if (top.liOpen) m_out += "</li>";
    m_out += "</";
    m_out += top.tag;
    m_out += ">";
    m_listStack.pop_back();
}

void Converter::closeLists(size_t target) {
    while (m_listStack.size() > target) closeOneList();
}

void Converter::setListState(int numId, int ilvl) {
    size_t target = static_cast<size_t>(ilvl) + 1;
    std::string fmt = numFmtFor(numId, ilvl);
    const char* tag = (fmt == "bullet" || fmt == "none") ? "ul" : "ol";

    closeLists(target);
    if (m_listStack.size() == target &&
        std::string_view(m_listStack.back().tag) != tag)
        closeOneList();

    while (m_listStack.size() < target) {
        int lvl = static_cast<int>(m_listStack.size());
        std::string f = numFmtFor(numId, lvl);
        const char* t = (f == "bullet" || f == "none") ? "ul" : "ol";
        m_out += '<';
        m_out += t;
        if (f == "lowerLetter") m_out += " type=\"a\"";
        else if (f == "upperLetter") m_out += " type=\"A\"";
        else if (f == "lowerRoman") m_out += " type=\"i\"";
        else if (f == "upperRoman") m_out += " type=\"I\"";
        else if (f == "none") m_out += " style=\"list-style:none\"";
        if (t[0] == 'o') { // resume numbering / honor w:start
            int startAt = startFor(numId, lvl) + m_listEmitted[listKey(numId, lvl)];
            if (startAt != 1)
                m_out += " start=\"" + std::to_string(startAt) + "\"";
        }
        m_out += '>';
        m_listStack.push_back({t, false});
    }
}

// ---------------------------------------------------------------------------
// Document traversal

void Converter::convertBody(std::string_view xml) {
    XmlParser xp(xml);
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::Start && xp.name() == "w:body") {
            parseBlocks(xp, "w:body");
            break;
        }
    }
    closeLists(0);
}

void Converter::parseBlocks(XmlParser& xp, std::string_view endName) {
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::End && xp.name() == endName) return;
        if (ev == Event::Start) parseBlockChild(xp, xp.name());
    }
}

void Converter::parseBlockChild(XmlParser& xp, std::string_view name) {
    if (m_depth > kMaxDepth) {
        xp.skipElement();
        return;
    }
    DepthGuard guard(m_depth);
    if (name == "w:p") {
        parseParagraph(xp);
    } else if (name == "w:tbl") {
        closeLists(0);
        parseTable(xp);
    } else if (name == "w:sectPr" || name == "w:sdtPr" ||
               name == "w:sdtEndPr" || name == "w:del" ||
               name == "w:moveFrom") {
        xp.skipElement();
    } else {
        // transparent container (w:sdt, w:sdtContent, bookmarks, ...)
        parseBlocks(xp, name);
    }
}

void Converter::parseParagraph(XmlParser& xp) {
    ParaProps pp;
    std::string closeTag;
    bool opened = false;
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::End && xp.name() == "w:p") break;
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();
        if (n == "w:pPr") { // always precedes content
            parseParaProps(xp, pp);
            // numbering attached to the paragraph style (List Bullet etc.)
            if (pp.numId < 0 && !pp.styleId.empty()) {
                auto it = m_styleNum.find(pp.styleId);
                if (it != m_styleNum.end()) {
                    pp.numId = it->second.numId;
                    pp.ilvl = it->second.ilvl;
                }
            }
            continue;
        }
        if (!opened) {
            closeTag = openParagraph(pp);
            opened = true;
        }
        parseInline(xp, n);
    }
    // anchors from fields must not cross the paragraph boundary
    for (Field& f : m_fields) {
        if (f.open) {
            m_out += "</a>";
            f.open = false;
        }
    }
    if (!opened) closeTag = openParagraph(pp);
    m_out += closeTag;
}

void Converter::parseParaProps(XmlParser& xp, ParaProps& pp) {
    int depth = 1;
    while (depth > 0) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::Start) {
            std::string_view n = xp.name();
            if (n == "w:pStyle") {
                pp.styleId = std::string(xp.attr("w:val"));
                auto it = m_headingByStyle.find(pp.styleId);
                if (it != m_headingByStyle.end()) pp.heading = it->second;
            } else if (n == "w:jc") {
                pp.align = mapAlign(xp.attr("w:val"));
            } else if (n == "w:ilvl") {
                pp.ilvl = svToInt(xp.attr("w:val"));
            } else if (n == "w:numId") {
                pp.numId = svToInt(xp.attr("w:val"));
            } else if (n == "w:ind") {
                std::string_view left = xp.attr("w:left");
                if (left.empty()) left = xp.attr("w:start");
                int hanging = svToInt(xp.attr("w:hanging"));
                int firstLine = svToInt(xp.attr("w:firstLine"));
                appendPt(pp.css, "margin-left", svToInt(left));
                if (hanging) appendPt(pp.css, "text-indent", -hanging);
                else if (firstLine) appendPt(pp.css, "text-indent", firstLine);
            } else if (n == "w:shd") {
                std::string_view fill = xp.attr("w:fill");
                if (!fill.empty() && fill != "auto") {
                    pp.css += "background-color:#";
                    pp.css += fill;
                    pp.css += ";";
                }
            } else if (n == "w:bidi") {
                pp.rtl = toggleOn(xp.attr("w:val"));
            }
            depth++;
        } else if (ev == Event::End) {
            depth--;
        }
    }
}

std::string Converter::openParagraph(const ParaProps& pp) {
    // numId 0 means "numbering removed" in OOXML
    if (pp.numId > 0 && pp.heading == 0) {
        setListState(pp.numId, pp.ilvl);
        OpenList& top = m_listStack.back();
        if (top.liOpen) m_out += "</li>";
        m_out += "<li>";
        top.liOpen = true;
        m_listEmitted[listKey(pp.numId, pp.ilvl)]++;
        for (int l = pp.ilvl + 1; l <= 8; l++) // sublevels restart per item
            m_listEmitted.erase(listKey(pp.numId, l));
        return std::string(); // </li> emitted lazily by the list machinery
    }

    closeLists(0);
    std::string tag =
        pp.heading > 0 ? "h" + std::to_string(pp.heading) : std::string("p");
    std::string style;
    if (!pp.align.empty()) {
        style += "text-align:";
        style += pp.align;
        style += ";";
    }
    style += pp.css;

    m_out += "<";
    m_out += tag;
    if (pp.rtl) m_out += " dir=\"rtl\"";
    if (!style.empty()) {
        m_out += " style=\"";
        m_out += style;
        m_out += "\"";
    }
    m_out += ">";
    return "</" + tag + ">";
}

void Converter::parseInline(XmlParser& xp, std::string_view name) {
    if (m_depth > kMaxDepth) {
        xp.skipElement();
        return;
    }
    DepthGuard guard(m_depth);
    if (name == "w:r") {
        parseRun(xp);
    } else if (name == "w:hyperlink") {
        parseHyperlink(xp);
    } else if (name == "w:fldSimple") {
        std::string href = hyperlinkFromInstr(decodeAttr(xp.attr("w:instr")));
        if (!href.empty()) {
            m_out += "<a href=\"";
            appendEscapedAttr(href, m_out);
            m_out += "\">";
        }
        parseInlineChildren(xp, "w:fldSimple");
        if (!href.empty()) m_out += "</a>";
    } else if (name == "m:t") {
        parseText(xp); // math fallback: linearized text
    } else if (name == "w:bookmarkStart") {
        std::string_view nm = xp.attr("w:name");
        if (!nm.empty() && nm != "_GoBack") {
            m_out += "<a id=\"";
            appendEscapedAttr(decodeAttr(nm), m_out);
            m_out += "\"></a>";
        }
        xp.skipElement();
    } else if (name == "mc:AlternateContent") {
        // mc:Choice and mc:Fallback duplicate content — emit only the
        // fallback to avoid doubling.
        for (;;) {
            Event ev = xp.next();
            if (ev == Event::Eof) return;
            if (ev == Event::End && xp.name() == "mc:AlternateContent") return;
            if (ev != Event::Start) continue;
            if (xp.name() == "mc:Fallback")
                parseInlineChildren(xp, "mc:Fallback");
            else
                xp.skipElement();
        }
    } else if (name == "w:del" || name == "w:moveFrom" || name == "w:pPr") {
        xp.skipElement();
    } else {
        // transparent: w:ins, w:smartTag, w:sdt, m:oMath, ...
        parseInlineChildren(xp, name);
    }
}

void Converter::parseInlineChildren(XmlParser& xp, std::string_view endName) {
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::End && xp.name() == endName) return;
        if (ev == Event::Start) parseInline(xp, xp.name());
    }
}

void Converter::parseRun(XmlParser& xp) {
    RunProps rp;
    std::string close;
    bool opened = false;
    parseRunChildren(xp, "w:r", rp, opened, close);
    if (opened) m_out += close;
}

void Converter::parseRunChildren(XmlParser& xp, std::string_view endName,
                                 RunProps& rp, bool& opened,
                                 std::string& close) {
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::End && xp.name() == endName) return;
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();

        if (n == "w:rPr") {
            parseRunProps(xp, rp);
            continue;
        }
        if (n == "w:fldChar") {
            handleFldChar(xp);
            continue;
        }
        if (n == "w:instrText") {
            handleInstrText(xp);
            continue;
        }
        if (n == "w:delText" || n == "w:lastRenderedPageBreak" ||
            n == "w:footnoteRef" || n == "w:endnoteRef" ||
            n == "w:separator" || n == "w:continuationSeparator" ||
            n == "w:commentReference" || n == "w:annotationRef") {
            xp.skipElement();
            continue;
        }
        if (n == "mc:AlternateContent") {
            for (;;) {
                Event e2 = xp.next();
                if (e2 == Event::Eof) return;
                if (e2 == Event::End && xp.name() == "mc:AlternateContent")
                    break;
                if (e2 != Event::Start) continue;
                if (xp.name() == "mc:Fallback")
                    parseRunChildren(xp, "mc:Fallback", rp, opened, close);
                else
                    xp.skipElement();
            }
            continue;
        }

        bool emits = n == "w:t" || n == "m:t" || n == "w:br" || n == "w:cr" ||
                     n == "w:tab" || n == "w:drawing" || n == "w:pict" ||
                     n == "w:object" || n == "w:noBreakHyphen" ||
                     n == "w:softHyphen" || n == "w:sym" ||
                     n == "w:footnoteReference" || n == "w:endnoteReference";
        if (emits && !opened) {
            std::string open;
            buildRunTags(rp, open, close);
            m_out += open;
            opened = true;
        }

        if (n == "w:t" || n == "m:t") parseText(xp);
        else if (n == "w:br" || n == "w:cr") { m_out += "<br>"; xp.skipElement(); }
        else if (n == "w:tab") { m_out += "<span class=\"tab\">\t</span>"; xp.skipElement(); }
        else if (n == "w:drawing" || n == "w:pict" || n == "w:object") parseDrawing(xp);
        else if (n == "w:noBreakHyphen") { m_out += "&#8209;"; xp.skipElement(); }
        else if (n == "w:softHyphen") { m_out += "&shy;"; xp.skipElement(); }
        else if (n == "w:sym") emitSym(xp);
        else if (n == "w:footnoteReference") emitNoteRef(xp, true, rp.vert == 1);
        else if (n == "w:endnoteReference") emitNoteRef(xp, false, rp.vert == 1);
        else if (m_depth > kMaxDepth) xp.skipElement();
        else { // transparent
            DepthGuard guard(m_depth);
            parseRunChildren(xp, n, rp, opened, close);
        }
    }
}

void Converter::parseRunProps(XmlParser& xp, RunProps& rp) {
    int depth = 1;
    while (depth > 0) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::Start) {
            std::string_view n = xp.name();
            if (n == "w:rStyle") {
                // character style is the base; direct formatting (which
                // follows it in w:rPr) overrides
                auto it = m_charStyle.find(std::string(xp.attr("w:val")));
                if (it != m_charStyle.end()) rp = it->second;
            } else {
                applyRunProp(rp, n, xp.attr("w:val"));
            }
            depth++;
        } else if (ev == Event::End) {
            depth--;
        }
    }
}

void Converter::buildRunTags(const RunProps& rp, std::string& open,
                             std::string& close) {
    auto wrap = [&](const char* o, const char* c) {
        open += o;
        close.insert(0, c);
    };
    if (rp.b) wrap("<strong>", "</strong>");
    if (rp.i) wrap("<em>", "</em>");
    if (rp.u) wrap("<u>", "</u>");
    if (rp.strike) wrap("<s>", "</s>");
    if (rp.vert == 1) wrap("<sup>", "</sup>");
    if (rp.vert == -1) wrap("<sub>", "</sub>");

    std::string css;
    if (!rp.color.empty()) css += "color:#" + rp.color + ";";
    if (!rp.highlight.empty())
        css += "background-color:" + highlightCss(rp.highlight) + ";";
    if (rp.caps) css += "text-transform:uppercase;";
    if (rp.smallCaps) css += "font-variant:small-caps;";
    if (rp.vanish) css += "display:none;";
    if (!css.empty()) {
        open += "<span style=\"" + css + "\">";
        close.insert(0, "</span>");
    }
}

void Converter::parseText(XmlParser& xp) {
    // XML-escaped text is valid HTML-escaped text: pass it straight through.
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof || ev == Event::End) return;
        if (ev == Event::Text) m_out.append(xp.text());
    }
}

void Converter::parseHyperlink(XmlParser& xp) {
    std::string href;
    std::string_view rid = xp.attr("r:id");
    std::string_view anchor = xp.attr("w:anchor");
    std::string title = decodeAttr(xp.attr("w:tooltip"));
    if (!rid.empty()) {
        auto it = m_rels.find(std::string(rid));
        if (it != m_rels.end()) href = it->second.target;
    } else if (!anchor.empty()) {
        href = "#" + decodeAttr(anchor);
    }
    if (!href.empty()) {
        m_out += "<a href=\"";
        appendEscapedAttr(href, m_out);
        if (!title.empty()) {
            m_out += "\" title=\"";
            appendEscapedAttr(title, m_out);
        }
        m_out += "\">";
    }
    parseInlineChildren(xp, "w:hyperlink");
    if (!href.empty()) m_out += "</a>";
}

// ---------------------------------------------------------------------------
// Fields, notes, symbols

void Converter::handleFldChar(XmlParser& xp) {
    std::string_view type = xp.attr("w:fldCharType");
    if (type == "begin") {
        m_fields.push_back({});
    } else if (type == "separate") {
        if (!m_fields.empty() && m_fields.back().collecting) {
            Field& f = m_fields.back();
            f.collecting = false;
            std::string href = hyperlinkFromInstr(f.instr);
            if (!href.empty()) {
                m_out += "<a href=\"";
                appendEscapedAttr(href, m_out);
                m_out += "\">";
                f.open = true;
            }
        }
    } else if (type == "end") {
        if (!m_fields.empty()) {
            if (m_fields.back().open) m_out += "</a>";
            m_fields.pop_back();
        }
    }
    xp.skipElement();
}

void Converter::handleInstrText(XmlParser& xp) {
    if (m_fields.empty() || !m_fields.back().collecting) {
        xp.skipElement();
        return;
    }
    std::string& instr = m_fields.back().instr;
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof || ev == Event::End) return;
        if (ev == Event::Text) XmlParser::appendDecoded(xp.text(), instr);
    }
}

void Converter::emitSym(XmlParser& xp) {
    std::string font = decodeAttr(xp.attr("w:font"));
    unsigned cp = svToHex(xp.attr("w:char"));
    xp.skipElement();
    if (cp == 0) return;
    char buf[16];
    std::snprintf(buf, sizeof buf, "&#x%X;", cp);
    if (!font.empty()) {
        m_out += "<span style=\"font-family:'";
        appendEscapedAttr(font, m_out);
        m_out += "'\">";
        m_out += buf;
        m_out += "</span>";
    } else {
        m_out += buf;
    }
}

void Converter::emitNoteRef(XmlParser& xp, bool footnote, bool alreadySuper) {
    std::string id(xp.attr("w:id"));
    xp.skipElement();
    if (id.empty()) return;
    auto& order = footnote ? m_fnOrder : m_enOrder;
    size_t idx = 0;
    for (; idx < order.size(); idx++)
        if (order[idx] == id) break;
    if (idx == order.size()) order.push_back(id);
    const char* prefix = footnote ? "fn" : "en";
    std::string num = std::to_string(idx + 1);
    if (!alreadySuper) m_out += "<sup>";
    m_out += "<a href=\"#";
    m_out += prefix;
    m_out += "-";
    m_out += id;
    m_out += "\" id=\"";
    m_out += prefix;
    m_out += "ref-";
    m_out += id;
    m_out += "\">";
    m_out += num;
    m_out += "</a>";
    if (!alreadySuper) m_out += "</sup>";
}

void Converter::appendNotesSection(const char* partName, bool footnote) {
    auto& order = footnote ? m_fnOrder : m_enOrder;
    if (order.empty()) return;
    std::vector<uint8_t> xmlBuf;
    if (!loadPart(m_baseDir + partName + ".xml", xmlBuf)) return;

    // Notes resolve hyperlinks/images through their own .rels part.
    std::unordered_map<std::string, Relationship> docRels;
    std::swap(m_rels, docRels);
    std::vector<uint8_t> rbuf;
    if (loadPart(m_baseDir + "_rels/" + partName + ".xml.rels", rbuf))
        parseRels(asView(rbuf));

    std::unordered_map<std::string, std::string> bodyById;
    XmlParser xp(asView(xmlBuf));
    std::string_view noteElem = footnote ? "w:footnote" : "w:endnote";
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev != Event::Start || xp.name() != noteElem) continue;
        std::string id(xp.attr("w:id"));
        std::string_view type = xp.attr("w:type");
        if (type == "separator" || type == "continuationSeparator" ||
            type == "continuationNotice") {
            xp.skipElement();
            continue;
        }
        std::string saved;
        saved.swap(m_out);
        parseBlocks(xp, noteElem);
        closeLists(0);
        bodyById[id].swap(m_out);
        m_out.swap(saved);
    }
    std::swap(m_rels, docRels);

    const char* prefix = footnote ? "fn" : "en";
    m_out += "<hr><section class=\"footnotes\"><ol>";
    for (const std::string& id : order) {
        m_out += "<li id=\"";
        m_out += prefix;
        m_out += "-";
        m_out += id;
        m_out += "\">";
        auto it = bodyById.find(id);
        if (it != bodyById.end()) m_out += it->second;
        m_out += "<a href=\"#";
        m_out += prefix;
        m_out += "ref-";
        m_out += id;
        m_out += "\">&#8617;</a></li>";
    }
    m_out += "</ol></section>";
}

// ---------------------------------------------------------------------------
// Drawings and images

void Converter::parseDrawing(XmlParser& xp) {
    long cx = 0, cy = 0;
    std::string relId;
    int depth = 1;
    while (depth > 0) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::Start) {
            std::string_view n = xp.name();
            if (n == "wp:extent") {
                cx = svToInt(xp.attr("cx"));
                cy = svToInt(xp.attr("cy"));
            } else if (n == "a:blip") {
                std::string_view r = xp.attr("r:embed");
                if (r.empty()) r = xp.attr("r:link");
                if (!r.empty()) relId = std::string(r);
            } else if (n == "v:imagedata") {
                std::string_view r = xp.attr("r:id");
                if (!r.empty()) relId = std::string(r);
            } else if (n == "w:txbxContent") {
                parseBlocks(xp, n); // text boxes: emit their paragraphs inline
                continue;           // parseBlocks consumed the matching End
            }
            depth++;
        } else if (ev == Event::End) {
            depth--;
        }
    }
    if (!relId.empty()) emitImage(relId, cx, cy);
}

void Converter::emitImage(const std::string& relId, long cx, long cy) {
    if (!m_opts.embedImages) return;
    auto it = m_rels.find(relId);
    if (it == m_rels.end()) return;

    std::string src;
    if (it->second.external) {
        src = it->second.target;
    } else {
        std::string path = resolveZipPath(m_baseDir, it->second.target);
        const ZipEntry* e = m_zip.find(path);
        std::vector<uint8_t> bytes;
        if (!e || !m_zip.extract(*e, bytes)) return;
        src = "data:";
        src += mimeForPath(path);
        src += ";base64,";
        src += base64Encode(bytes.data(), bytes.size());
    }

    m_out += "<img src=\"";
    appendEscapedAttr(src, m_out);
    m_out += "\" alt=\"\"";
    if (cx > 0 && cy > 0) { // EMU -> CSS px (96 dpi): 9525 EMU per px
        m_out += " width=\"" + std::to_string(cx / 9525) + "\"";
        m_out += " height=\"" + std::to_string(cy / 9525) + "\"";
    }
    m_out += ">";
}

// ---------------------------------------------------------------------------
// Tables

void Converter::parseTable(XmlParser& xp) {
    std::vector<RowData> rows;
    collectRows(xp, "w:tbl", rows);
    emitTable(rows);
}

void Converter::collectRows(XmlParser& xp, std::string_view endName,
                            std::vector<RowData>& rows) {
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::End && xp.name() == endName) return;
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();
        if (n == "w:tr") {
            rows.emplace_back();
            collectRowChildren(xp, "w:tr", rows.back());
        } else if (n == "w:tblPr" || n == "w:tblGrid" ||
                   m_depth > kMaxDepth) {
            xp.skipElement();
        } else {
            DepthGuard guard(m_depth);
            collectRows(xp, n, rows); // transparent (w:sdt around rows)
        }
    }
}

void Converter::collectRowChildren(XmlParser& xp, std::string_view endName,
                                   RowData& row) {
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::End && xp.name() == endName) return;
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();
        if (n == "w:tc") {
            row.cells.emplace_back();
            collectCell(xp, row.cells.back());
        } else if (n == "w:trPr") {
            int depth = 1;
            while (depth > 0) {
                Event e2 = xp.next();
                if (e2 == Event::Eof) return;
                if (e2 == Event::Start) {
                    if (xp.name() == "w:tblHeader" &&
                        toggleOn(xp.attr("w:val")))
                        row.header = true;
                    depth++;
                } else if (e2 == Event::End) {
                    depth--;
                }
            }
        } else if (n == "w:tblPrEx" || m_depth > kMaxDepth) {
            xp.skipElement();
        } else {
            DepthGuard guard(m_depth);
            collectRowChildren(xp, n, row); // transparent
        }
    }
}

void Converter::collectCell(XmlParser& xp, CellData& cell) {
    std::string saved;
    saved.swap(m_out); // redirect emission into the cell buffer
    for (;;) {
        Event ev = xp.next();
        if (ev == Event::Eof) break;
        if (ev == Event::End && xp.name() == "w:tc") break;
        if (ev != Event::Start) continue;
        std::string_view n = xp.name();
        if (n == "w:tcPr") {
            parseCellProps(xp, cell);
            continue;
        }
        parseBlockChild(xp, n);
    }
    closeLists(0);
    cell.html.swap(m_out);
    m_out.swap(saved);
}

void Converter::parseCellProps(XmlParser& xp, CellData& cell) {
    int depth = 1;
    while (depth > 0) {
        Event ev = xp.next();
        if (ev == Event::Eof) return;
        if (ev == Event::Start) {
            std::string_view n = xp.name();
            if (n == "w:gridSpan") {
                cell.colspan = std::max(1, svToInt(xp.attr("w:val")));
            } else if (n == "w:vMerge") {
                cell.vmerge = (xp.attr("w:val") == "restart") ? 1 : 2;
            } else if (n == "w:shd") {
                std::string_view fill = xp.attr("w:fill");
                if (!fill.empty() && fill != "auto") {
                    cell.css += "background-color:#";
                    cell.css += fill;
                    cell.css += ";";
                }
            } else if (n == "w:vAlign") {
                std::string_view v = xp.attr("w:val");
                if (v == "center") cell.css += "vertical-align:middle;";
                else if (v == "bottom") cell.css += "vertical-align:bottom;";
            }
            depth++;
        } else if (ev == Event::End) {
            depth--;
        }
    }
}

void Converter::emitTable(std::vector<RowData>& rows) {
    const size_t nr = rows.size();
    // Grid columns each cell starts at, accounting for colspans.
    std::vector<std::vector<int>> start(nr);
    std::vector<std::vector<bool>> covered(nr);
    std::vector<std::vector<int>> rowspan(nr);
    for (size_t r = 0; r < nr; r++) {
        int c = 0;
        for (const CellData& cell : rows[r].cells) {
            start[r].push_back(c);
            c += cell.colspan;
        }
        covered[r].assign(rows[r].cells.size(), false);
        rowspan[r].assign(rows[r].cells.size(), 1);
    }
    // Resolve vMerge restart/continue chains into rowspans.
    for (size_t r = 0; r < nr; r++) {
        for (size_t ci = 0; ci < rows[r].cells.size(); ci++) {
            if (rows[r].cells[ci].vmerge != 1 || covered[r][ci]) continue;
            int col = start[r][ci];
            for (size_t rr = r + 1; rr < nr; rr++) {
                bool found = false;
                for (size_t cj = 0; cj < rows[rr].cells.size(); cj++) {
                    if (start[rr][cj] == col &&
                        rows[rr].cells[cj].vmerge == 2 && !covered[rr][cj]) {
                        covered[rr][cj] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
                rowspan[r][ci]++;
            }
        }
    }

    m_out += "<table>";
    for (size_t r = 0; r < nr; r++) {
        m_out += "<tr>";
        const char* tag = rows[r].header ? "th" : "td";
        for (size_t ci = 0; ci < rows[r].cells.size(); ci++) {
            if (covered[r][ci]) continue; // swallowed by a rowspan above
            const CellData& cell = rows[r].cells[ci];
            m_out += '<';
            m_out += tag;
            if (cell.colspan > 1)
                m_out += " colspan=\"" + std::to_string(cell.colspan) + "\"";
            if (rowspan[r][ci] > 1)
                m_out += " rowspan=\"" + std::to_string(rowspan[r][ci]) + "\"";
            if (!cell.css.empty()) {
                m_out += " style=\"";
                m_out += cell.css;
                m_out += "\"";
            }
            m_out += '>';
            m_out += cell.html;
            m_out += "</";
            m_out += tag;
            m_out += '>';
        }
        m_out += "</tr>";
    }
    m_out += "</table>";
}

// ---------------------------------------------------------------------------
// Top level

bool Converter::run(const uint8_t* data, size_t size, std::string& htmlOut,
                    std::string& error) {
    if (!m_zip.open(data, size)) {
        static const uint8_t kOle2Magic[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                              0xA1, 0xB1, 0x1A, 0xE1};
        if (size >= 8 && std::memcmp(data, kOle2Magic, 8) == 0)
            error = "OLE compound file: either a legacy binary .doc or a "
                    "password-protected document (neither is supported)";
        else
            error = "not a valid .docx file (ZIP archive could not be read)";
        return false;
    }

    std::string docPath = findDocumentPath();
    std::vector<uint8_t> docXml;
    if (!loadPart(docPath, docXml)) {
        if (m_zip.find("mimetype") && m_zip.find("content.xml"))
            error = "OpenDocument file (.odt) with a .docx extension "
                    "(not supported)";
        else
            error = "not a valid .docx file (missing " + docPath + ")";
        return false;
    }

    size_t slash = docPath.rfind('/');
    m_baseDir =
        (slash == std::string::npos) ? "" : docPath.substr(0, slash + 1);
    std::string docName =
        (slash == std::string::npos) ? docPath : docPath.substr(slash + 1);

    std::vector<uint8_t> buf;
    if (loadPart(m_baseDir + "_rels/" + docName + ".rels", buf))
        parseRels(asView(buf));
    if (loadPart(m_baseDir + "styles.xml", buf)) parseStyles(asView(buf));
    if (loadPart(m_baseDir + "numbering.xml", buf)) parseNumbering(asView(buf));
    if (loadPart("docProps/core.xml", buf)) parseCoreProps(asView(buf));

    m_out.reserve(docXml.size());
    convertBody(asView(docXml));
    appendNotesSection("footnotes", true);
    appendNotesSection("endnotes", false);

    if (m_opts.fragment) {
        htmlOut = std::move(m_out);
        return true;
    }

    std::string title = m_opts.title.empty() ? m_title : m_opts.title;
    htmlOut.reserve(m_out.size() + 1024);
    htmlOut =
        "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>";
    appendEscapedHtml(title.empty() ? "Document" : title, htmlOut);
    htmlOut += "</title>\n<style>\n"
               "body{font-family:Calibri,Arial,sans-serif;line-height:1.4;"
               "max-width:60em;margin:2em auto;padding:0 1em;}\n"
               "table{border-collapse:collapse;margin:0.5em 0;}\n"
               "td,th{border:1px solid #999;padding:0.25em 0.5em;"
               "vertical-align:top;}\n"
               "th{background:#f0f0f0;text-align:left;}\n"
               "img{max-width:100%;height:auto;}\n"
               ".tab{white-space:pre;}\n"
               ".footnotes{font-size:0.9em;color:#444;}\n"
               "</style>\n</head>\n<body>\n";
    htmlOut += m_out;
    htmlOut += "\n</body>\n</html>\n";
    return true;
}

} // namespace

bool convertDocxToHtml(const uint8_t* data, size_t size,
                       const ConvertOptions& opts, std::string& htmlOut,
                       std::string& error) {
    Converter c(opts);
    return c.run(data, size, htmlOut, error);
}
