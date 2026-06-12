#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Escapes & < > for HTML text content.
void appendEscapedHtml(std::string_view s, std::string& out);

// Escapes & < > " for double-quoted HTML attribute values.
void appendEscapedAttr(std::string_view s, std::string& out);

std::string base64Encode(const uint8_t* data, size_t len);
