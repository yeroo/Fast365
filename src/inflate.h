#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Decompresses a raw DEFLATE stream (RFC 1951) into `out`.
// `expectedSize`, when non-zero, pre-reserves the output buffer and acts
// as a hard output cap (pass the uncompressed size from the ZIP central
// directory) so corrupt or malicious streams cannot balloon memory.
// Returns false on malformed input.
bool inflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out,
                size_t expectedSize = 0);
