// Minimal protobuf wire-format writer/reader: enough for the Meshtastic
// messages this firmware speaks (Data, User, Position, Routing, Telemetry).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum PbWire : uint8_t { PB_VARINT = 0, PB_FIXED64 = 1, PB_LEN = 2, PB_FIXED32 = 5 };

class PbWriter {
public:
    PbWriter(uint8_t *buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t len() const { return len_; }
    bool ok() const { return ok_; }

    void varint(uint32_t field, uint64_t v) { key(field, PB_VARINT); raw_varint(v); }
    void boolean(uint32_t field, bool v) { varint(field, v ? 1 : 0); }
    void int32(uint32_t field, int32_t v) { varint(field, (uint64_t)(int64_t)v); } // negative = 10 bytes, as protobuf does
    void sint32(uint32_t field, int32_t v) { varint(field, (uint32_t)((v << 1) ^ (v >> 31))); }
    void fixed32(uint32_t field, uint32_t v) { key(field, PB_FIXED32); raw_le32(v); }
    void sfixed32(uint32_t field, int32_t v) { fixed32(field, (uint32_t)v); }
    void float32(uint32_t field, float f) { uint32_t v; memcpy(&v, &f, 4); fixed32(field, v); }
    void bytes(uint32_t field, const void *data, size_t n)
    {
        key(field, PB_LEN);
        raw_varint(n);
        put(data, n);
    }
    void string(uint32_t field, const char *s) { bytes(field, s, strlen(s)); }

private:
    void key(uint32_t field, PbWire w) { raw_varint((field << 3) | w); }
    void raw_varint(uint64_t v)
    {
        do {
            uint8_t b = v & 0x7f;
            v >>= 7;
            if (v) b |= 0x80;
            put(&b, 1);
        } while (v);
    }
    void raw_le32(uint32_t v)
    {
        uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
        put(b, 4);
    }
    void put(const void *d, size_t n)
    {
        if (len_ + n > cap_) { ok_ = false; return; }
        memcpy(buf_ + len_, d, n);
        len_ += n;
    }
    uint8_t *buf_;
    size_t cap_, len_ = 0;
    bool ok_ = true;
};

class PbReader {
public:
    PbReader(const uint8_t *p, size_t n) : p_(p), end_(p + n) {}
    bool ok() const { return ok_; }

    // Advance to the next field; false at end or on malformed input.
    bool next()
    {
        if (!ok_ || p_ >= end_) return false;
        uint64_t k = raw_varint();
        field = (uint32_t)(k >> 3);
        wire = (PbWire)(k & 7);
        consumed_ = false;
        return ok_ && field != 0;
    }
    uint32_t field = 0;
    PbWire wire = PB_VARINT;

    uint64_t varint() { consumed_ = true; return wire == PB_VARINT ? raw_varint() : bad(); }
    int32_t sint32() { uint32_t v = (uint32_t)varint(); return (int32_t)((v >> 1) ^ -(int32_t)(v & 1)); }
    uint32_t fixed32()
    {
        consumed_ = true;
        if (wire != PB_FIXED32 || end_ - p_ < 4) return (uint32_t)bad();
        uint32_t v = p_[0] | (p_[1] << 8) | (p_[2] << 16) | ((uint32_t)p_[3] << 24);
        p_ += 4;
        return v;
    }
    float float32() { uint32_t v = fixed32(); float f; memcpy(&f, &v, 4); return f; }
    // Returns a pointer into the buffer; n receives the length.
    const uint8_t *bytes(size_t *n)
    {
        consumed_ = true;
        if (wire != PB_LEN) { bad(); *n = 0; return nullptr; }
        uint64_t l = raw_varint();
        if (!ok_ || l > (uint64_t)(end_ - p_)) { bad(); *n = 0; return nullptr; }
        const uint8_t *r = p_;
        p_ += l;
        *n = (size_t)l;
        return r;
    }
    // Copy a string field into dst (always NUL-terminated).
    void string(char *dst, size_t cap)
    {
        size_t n;
        const uint8_t *s = bytes(&n);
        if (!s || cap == 0) return;
        if (n >= cap) n = cap - 1;
        memcpy(dst, s, n);
        dst[n] = 0;
    }
    // Call when a field is not used by the caller.
    void skip()
    {
        if (consumed_) return;
        size_t n;
        switch (wire) {
        case PB_VARINT: raw_varint(); break;
        case PB_FIXED64: if (end_ - p_ < 8) bad(); else p_ += 8; break;
        case PB_LEN: bytes(&n); break;
        case PB_FIXED32: if (end_ - p_ < 4) bad(); else p_ += 4; break;
        default: bad();
        }
        consumed_ = true;
    }

private:
    uint64_t raw_varint()
    {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p_ >= end_) return bad();
            uint8_t b = *p_++;
            v |= (uint64_t)(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        return bad();
    }
    uint64_t bad() { ok_ = false; p_ = end_; return 0; }
    const uint8_t *p_, *end_;
    bool ok_ = true, consumed_ = true;
};
