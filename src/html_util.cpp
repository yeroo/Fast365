#include "html_util.h"

void appendEscapedHtml(std::string_view s, std::string& out) {
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
    }
}

void appendEscapedAttr(std::string_view s, std::string& out) {
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
}

std::string base64Encode(const uint8_t* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        s += tbl[(v >> 18) & 63];
        s += tbl[(v >> 12) & 63];
        s += tbl[(v >> 6) & 63];
        s += tbl[v & 63];
    }
    if (i + 1 == len) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        s += tbl[(v >> 18) & 63];
        s += tbl[(v >> 12) & 63];
        s += "==";
    } else if (i + 2 == len) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        s += tbl[(v >> 18) & 63];
        s += tbl[(v >> 12) & 63];
        s += tbl[(v >> 6) & 63];
        s += '=';
    }
    return s;
}
