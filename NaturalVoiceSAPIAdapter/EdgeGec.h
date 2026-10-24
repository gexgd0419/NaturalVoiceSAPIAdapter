#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Sec-MS-GEC v2, ported from edge-tts 7.3.0 drm.py (LGPL-3.0).
// Keep this independent of Windows/SAPI so the production algorithm can be tested.
namespace EdgeGec {
#include "EdgeGecTables.h"
inline uint32_t Ror(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}
inline uint32_t Swap(uint32_t x) {
    return Ror(x, 16);
}
inline uint32_t S0(uint32_t x) {
    return Ror(x, 7) ^ Ror(x, 18) ^ (x >> 3);
}
inline uint32_t S1(uint32_t x) {
    return Ror(x, 17) ^ Ror(x, 19) ^ (x >> 10);
}
inline uint32_t ReadBE(const unsigned char* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline uint32_t ReadLE(const unsigned char* p) {
    return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}
inline void Expand(uint32_t* w, unsigned first) {
    for (unsigned i = first; i < 64; i++) w[i] = S1(w[i - 2]) + w[i - 7] + S0(w[i - 15]) + w[i - 16];
}
inline void Compress(uint32_t* state, const uint32_t* w, unsigned first) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];
    for (unsigned i = first; i < 64; i++) {
        uint32_t k = first == 14 && i == 14 ? 0xC9DC06A7 : Rounds[i];
        uint32_t t1 = h + (Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25)) + (g ^ (e & (f ^ g))) + k + w[i];
        uint32_t t2 = (Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22)) + ((a & b) | (c & (a | b)));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    uint32_t work[] = {a, b, c, d, e, f, g, h};
    for (unsigned i = 0; i < 8; i++) state[i] = (first == 14 ? Initial[i] : state[i]) + work[i];
}
// fileTime includes the correction derived from the server's Date header.
// result must have room for 81 ASCII characters plus the terminating NUL.
inline void Generate(uint64_t fileTime, char* result) {
    fileTime -= fileTime % 3000000000ULL;
    char data[64];
    snprintf(data, sizeof(data), "%llu6A5AA1D4EAFF4E9FB37E23D68491D6F4", (unsigned long long)fileTime);
    size_t len = strlen(data);
    const auto* bytes = reinterpret_cast<const unsigned char*>(data);
    // V2 resumes a modified SHA-256 block after a fixed 57-byte prefix.
    // These schedule/state adjustments are intentional; ordinary SHA256(data)
    // produces the old v1 token and cannot replace this step.
    uint32_t w[64] = {};
    w[14] = uint32_t(bytes[0]) << 16 | uint32_t(bytes[1]) << 8 | bytes[2];
    w[15] = ReadBE(bytes + 3);
    for (unsigned i = 16; i < 30; i++) {
        uint32_t v = Schedule[i - 16];
        if (i - 2 >= 14) v += S1(w[i - 2]) ^ (i == 16 ? 0x000B92C0 : 0);
        if (i - 7 >= 14) v += w[i - 7];
        if (i - 15 >= 14) v += S0(w[i - 15]) ^ (i == 29 ? 0x059C0B80 : 0);
        w[i] = v;
    }
    w[30] = S1(w[28]) + w[23] + S0(w[15]) + w[14] + 0xC9DC06A7 - Rounds[14];
    Expand(w, 31);
    uint32_t state[8];
    memcpy(state, ResumeState, sizeof(state));
    Compress(state, w, 14);
    unsigned char padded[128] = {};
    memcpy(padded, bytes + 7, len - 7);
    padded[len - 7] = 0x80;
    size_t total = ((len + 57 + 9 + 63) / 64) * 64 - 64;
    uint64_t bits = (len + 57) * 8;
    for (unsigned i = 0; i < 8; i++) padded[total - 1 - i] = (unsigned char)(bits >> (i * 8));
    for (size_t off = 0; off < total; off += 64) {
        for (unsigned i = 0; i < 16; i++) w[i] = ReadBE(padded + off + i * 4);
        Expand(w, 16);
        Compress(state, w, 0);
    }
    unsigned char digest[32];
    for (unsigned i = 0; i < 32; i++) digest[i] = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
    // CS64 uses little-endian words; SHA-256 emits big-endian words.
    uint32_t ka = ReadLE(digest) | 1, kb = ReadLE(digest + 4) | 1;
    uint32_t factors[] = {ka * 0x82893933U, kb * 0x82893933U};
    uint32_t terms[32] = {}, offsets[32] = {};
    unsigned count = 14;
    for (unsigned i = 0; i < 14; i++) {
        terms[i] = Coefficients[i][0] * factors[i % 2];
        offsets[i] = Coefficients[i][1];
    }
    unsigned char input[72] = {};
    memcpy(input + 1, bytes, len);
    unsigned words = (unsigned)((len + 1 + 7) / 8) * 2;
    for (unsigned i = 0; i < words; i++) {
        uint32_t word = ReadLE(input + i * 4);
        terms[count] = word * (i % 2 ? kb : ka);
        offsets[count] = Swap(word) * (i % 2 ? 0xC31713DBU : 0xEF0569FBU);
        if (i == 0) {
            terms[count] += 0xF2F90B1AU * factors[0];
            offsets[count] += 0x0B1A0000;
        }
        count++;
    }
    uint32_t swapAcc = 0, swapLast = 0, s = 0, acc = 0;
    for (unsigned i = 0; i < count; i += 2) {
        uint32_t x = terms[i], y = terms[i + 1];
        uint32_t q = Swap(offsets[i] + x) * 0x76F50F34U;
        swapAcc += Swap(q) * 0xC3EFEA97U + q;
        q = Swap(offsets[i + 1] + y) * 0x1881CE3CU;
        swapLast = Swap(q) * 0x35BD1EC9U + q;
        swapAcc += swapLast;
        s = s * ka + x;
        s = Swap(s) * 0xCF98B111U;
        s = Swap(s) * 0x87085B9FU;
        s = Swap(s) * 0x12CEB96DU;
        s = Swap(s) * 0x257E1D83U;
        acc += s;
        uint32_t t = s * kb + y;
        t = Swap(t) * 0xA27416F5U;
        t = Swap(t) * 0xD38396FFU;
        s = Swap(Swap(t) * 0x7C932B89U) * 0xBFA49F69U;
        acc += s;
    }
    uint32_t cs[] = {swapLast ^ s, swapAcc ^ acc};
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 8; i++) {
        unsigned char v = (unsigned char)(cs[i / 4] >> (8 * (i % 4)));
        *result++ = hex[v >> 4];
        *result++ = hex[v & 15];
    }
    *result++ = '-';
    for (auto v : digest) {
        *result++ = hex[v >> 4];
        *result++ = hex[v & 15];
    }
    *result = 0;
}
} // namespace EdgeGec
