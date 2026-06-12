// DEFLATE decoder (RFC 1951), written from scratch — no zlib.
// Canonical-Huffman bit-at-a-time decode in the style of Mark Adler's puff.

#include "inflate.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr int kMaxBits = 15;    // longest Huffman code
constexpr int kMaxLCodes = 286; // literal/length symbols
constexpr int kMaxDCodes = 30;  // distance symbols
constexpr int kMaxCodes = kMaxLCodes + kMaxDCodes;
constexpr int kFixLCodes = 288; // fixed literal/length table size

struct BitReader {
    const uint8_t* data;
    size_t len;
    size_t pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool error = false;

    // Returns `need` bits (LSB first). Sets `error` on input underrun.
    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= len) {
                error = true;
                return 0;
            }
            val |= static_cast<uint32_t>(data[pos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return static_cast<int>(val & ((1u << need) - 1));
    }
};

struct Huffman {
    short count[kMaxBits + 1]; // number of codes of each length
    short symbol[kFixLCodes];  // symbols ordered canonically
};

// Builds decode tables from a code-length list. Returns 0 for a complete
// code set, >0 for an incomplete one, <0 if over-subscribed (invalid).
int construct(Huffman& h, const short* length, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    for (int sym = 0; sym < n; sym++) h.count[length[sym]]++;
    if (h.count[0] == n) return 0; // no codes at all

    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }

    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++)
        offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);

    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0)
            h.symbol[offs[length[sym]]++] = static_cast<short>(sym);
    return left;
}

int decode(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; len++) {
        code |= br.bits(1);
        if (br.error) return -1;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

bool codes(BitReader& br, std::vector<uint8_t>& out, const Huffman& lencode,
           const Huffman& distcode, size_t cap) {
    static const short lens[29] = {3,  4,  5,  6,   7,   8,   9,   10, 11, 13,
                                   15, 17, 19, 23,  27,  31,  35,  43, 51, 59,
                                   67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {
        1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
        33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                   4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                   9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int sym;
    do {
        sym = decode(br, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym > 256) {
            sym -= 257;
            if (sym >= 29) return false;
            int len = lens[sym] + br.bits(lext[sym]);
            int dsym = decode(br, distcode);
            if (dsym < 0 || dsym >= kMaxDCodes) return false;
            size_t dist = static_cast<size_t>(dists[dsym]) +
                          static_cast<size_t>(br.bits(dext[dsym]));
            if (br.error || dist > out.size()) return false;
            size_t from = out.size() - dist;
            for (int i = 0; i < len; i++)
                out.push_back(out[from + static_cast<size_t>(i)]);
        }
        if (cap && out.size() > cap) return false;
    } while (sym != 256);
    return !br.error;
}

struct FixedTables {
    Huffman lencode, distcode;
    FixedTables() {
        short lengths[kFixLCodes];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < kFixLCodes; sym++) lengths[sym] = 8;
        construct(lencode, lengths, kFixLCodes);
        for (sym = 0; sym < kMaxDCodes; sym++) lengths[sym] = 5;
        construct(distcode, lengths, kMaxDCodes);
    }
};

bool storedBlock(BitReader& br, std::vector<uint8_t>& out, size_t cap) {
    br.bitbuf = 0;
    br.bitcnt = 0;
    if (br.pos + 4 > br.len) return false;
    unsigned len = br.data[br.pos] | (br.data[br.pos + 1] << 8);
    unsigned nlen = br.data[br.pos + 2] | (br.data[br.pos + 3] << 8);
    if ((len ^ 0xFFFFu) != nlen) return false;
    br.pos += 4;
    if (br.pos + len > br.len) return false;
    if (cap && out.size() + len > cap) return false;
    out.insert(out.end(), br.data + br.pos, br.data + br.pos + len);
    br.pos += len;
    return true;
}

bool dynamicBlock(BitReader& br, std::vector<uint8_t>& out, size_t cap) {
    static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
    int nlen = br.bits(5) + 257;
    int ndist = br.bits(5) + 1;
    int ncode = br.bits(4) + 4;
    if (br.error || nlen > kMaxLCodes || ndist > kMaxDCodes) return false;

    short lengths[kMaxCodes];
    for (int i = 0; i < ncode; i++)
        lengths[order[i]] = static_cast<short>(br.bits(3));
    for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
    if (br.error) return false;

    Huffman lencode, distcode;
    if (construct(lencode, lengths, 19) != 0) return false;

    int index = 0;
    while (index < nlen + ndist) {
        int sym = decode(br, lencode);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = static_cast<short>(sym);
        } else {
            int repeat = 0; // length to repeat
            if (sym == 16) {
                if (index == 0) return false;
                repeat = lengths[index - 1];
                sym = 3 + br.bits(2);
            } else if (sym == 17) {
                sym = 3 + br.bits(3);
            } else {
                sym = 11 + br.bits(7);
            }
            if (br.error || index + sym > nlen + ndist) return false;
            while (sym--) lengths[index++] = static_cast<short>(repeat);
        }
    }
    if (lengths[256] == 0) return false; // end-of-block code must exist

    int err = construct(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
    err = construct(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;

    return codes(br, out, lencode, distcode, cap);
}

} // namespace

bool inflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out,
                size_t expectedSize) {
    // Reserve is bounded: a corrupt size claim must not allocate gigabytes.
    if (expectedSize)
        out.reserve(out.size() +
                    std::min(expectedSize, static_cast<size_t>(1) << 26));
    const size_t cap = expectedSize ? out.size() + expectedSize : 0;
    BitReader br{src, srcLen};
    int last;
    do {
        last = br.bits(1);
        int type = br.bits(2);
        if (br.error) return false;
        bool ok = false;
        switch (type) {
        case 0:
            ok = storedBlock(br, out, cap);
            break;
        case 1: {
            static const FixedTables ft;
            ok = codes(br, out, ft.lencode, ft.distcode, cap);
            break;
        }
        case 2:
            ok = dynamicBlock(br, out, cap);
            break;
        default:
            return false;
        }
        if (!ok) return false;
    } while (!last);
    return true;
}
