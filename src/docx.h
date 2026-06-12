#pragma once

#include <cstdint>
#include <string>

struct ConvertOptions {
    bool fragment = false;    // emit body content only, no <html> wrapper
    bool embedImages = true;  // inline images as base64 data URIs
    std::string title;        // <title> override (default: dc:title or filename)
};

// Converts an in-memory .docx file to HTML.
// Returns false and fills `error` on failure.
bool convertDocxToHtml(const uint8_t* data, size_t size,
                       const ConvertOptions& opts, std::string& htmlOut,
                       std::string& error);
