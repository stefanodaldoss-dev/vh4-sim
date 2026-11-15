#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
inline bool readWav(const char* path, std::vector<double>& out, int& sr) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    char id[4]; uint32_t sz; fread(id, 1, 4, f); fread(&sz, 4, 1, f); fread(id, 1, 4, f);
    int fmt = 0, ch = 1, bits = 16; sr = 48000;
    while (fread(id, 1, 4, f) == 4 && fread(&sz, 4, 1, f) == 1) {
        if (!memcmp(id, "fmt ", 4)) { uint16_t a, c, b; uint32_t r; fread(&a, 2, 1, f); fread(&c, 2, 1, f); fread(&r, 4, 1, f); fseek(f, 6, SEEK_CUR); fread(&b, 2, 1, f); fmt = a; ch = c; sr = r; bits = b; fseek(f, sz - 16, SEEK_CUR); }
        else if (!memcmp(id, "data", 4)) {
            int bytes = bits / 8; size_t n = sz / bytes / ch; out.resize(n);
            std::vector<uint8_t> buf(sz); fread(buf.data(), 1, sz, f);
            for (size_t i = 0; i < n; i++) {
                const uint8_t* p = &buf[i * ch * bytes];
                if (fmt == 3 && bits == 32) { float v; memcpy(&v, p, 4); out[i] = v; }
                else if (bits == 16) { int16_t v; memcpy(&v, p, 2); out[i] = v / 32768.0; }
                else if (bits == 24) { int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); out[i] = (v >> 8) / 8388608.0; }
                else out[i] = 0;
            }
            fclose(f); return true;
        } else fseek(f, sz, SEEK_CUR);
    }
    fclose(f); return false;
}
inline bool writeWavF32(const char* path, const std::vector<double>& x, int sr) {
    FILE* f = fopen(path, "wb"); if (!f) return false;
    uint32_t n = x.size() * 4, riff = 36 + n, fmtsz = 16, br = sr * 4; uint16_t a = 3, c = 1, ba = 4, bits = 32;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtsz, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&c, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    for (double v : x) { float q = (float)v; fwrite(&q, 4, 1, f); }
    fclose(f); return true;
}
