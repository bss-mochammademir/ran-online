#include "msgpack_min.h"

namespace sc { namespace mp {

void Packer::be(uint64_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i)
        put(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void Packer::Array(uint32_t n) {
    if (n <= 15) {
        put(static_cast<uint8_t>(0x90 | n));      // fixarray
    } else if (n <= 0xFFFF) {
        put(0xdc); be(n, 2);                       // array16
    } else {
        put(0xdd); be(n, 4);                       // array32
    }
}

void Packer::UInt(uint64_t v) {
    if (v <= 0x7F) {
        put(static_cast<uint8_t>(v));              // positive fixint
    } else if (v <= 0xFF) {
        put(0xcc); put(static_cast<uint8_t>(v));   // uint8
    } else if (v <= 0xFFFF) {
        put(0xcd); be(v, 2);                       // uint16
    } else if (v <= 0xFFFFFFFFULL) {
        put(0xce); be(v, 4);                       // uint32
    } else {
        put(0xcf); be(v, 8);                       // uint64
    }
}

void Packer::Int(int64_t v) {
    if (v >= 0) { UInt(static_cast<uint64_t>(v)); return; }   // canonical: >=0 -> uint
    if (v >= -32) {
        put(static_cast<uint8_t>(0xE0 | (v & 0x1F)));         // negative fixint
    } else if (v >= -128) {
        put(0xd0); put(static_cast<uint8_t>(v & 0xFF));       // int8
    } else if (v >= -32768) {
        put(0xd1); be(static_cast<uint64_t>(v), 2);           // int16
    } else if (v >= -2147483648LL) {
        put(0xd2); be(static_cast<uint64_t>(v), 4);           // int32
    } else {
        put(0xd3); be(static_cast<uint64_t>(v), 8);           // int64
    }
}

void Packer::Str(const std::string& s) {
    const size_t n = s.size();
    if (n <= 31) {
        put(static_cast<uint8_t>(0xA0 | n));       // fixstr
    } else if (n <= 0xFF) {
        put(0xd9); put(static_cast<uint8_t>(n));   // str8
    } else if (n <= 0xFFFF) {
        put(0xda); be(n, 2);                       // str16
    } else {
        put(0xdb); be(n, 4);                       // str32
    }
    for (char c : s) put(static_cast<uint8_t>(c));
}

void Packer::Bool(bool b) {
    put(b ? 0xc3 : 0xc2);
}

}} // namespace sc::mp
