#ifndef SC_NET_MSGPACK_MIN_H
#define SC_NET_MSGPACK_MIN_H

#include <cstdint>
#include <string>
#include <vector>

// Minimal, self-contained MessagePack encoder.
//
// Why hand-rolled instead of the vendored =MsgPack/ (msgpack-c): that copy is a
// ~2014 vintage that uses std::auto_ptr (removed in C++17). ranserver-linux is
// C++17 and msgpack-c is header-only, so its templates would be instantiated in
// our C++17 TUs and fail to compile. Rather than vendor a new external library
// (which conflicts with the self-contained / cloud-exit boundary), we own a tiny
// encoder here.
//
// Fidelity note: the client decodes via msgpack-c, whose `convert`/`as` for a
// field is *width-agnostic* — it accepts any valid MessagePack encoding of a
// value and range-checks into the target type. So interop requires STRUCTURAL
// fidelity (array shapes, field order, value types), NOT byte-identical integer
// widths. We still encode canonically (minimal width by value) so that output is
// byte-for-byte comparable against Python's `msgpack` package (the independent
// oracle used by msgpack_min_smoke golden vectors).
//
// MSGPACK_DEFINE(a, b, c) in msgpack-c serializes a struct as a fixarray of its
// members in order (confirmed: type/define.hpp emits pk.pack_array(N)). Mirror a
// struct by calling Array(N) then packing each member in declaration order.
namespace sc { namespace mp {

class Packer {
public:
    explicit Packer(std::vector<char>& out) : m_out(out) {}

    void Array(uint32_t n);          // fixarray / array16 / array32
    void UInt(uint64_t v);           // canonical unsigned (value-minimized)
    void Int(int64_t v);             // canonical signed (non-negative -> UInt path)
    void Str(const std::string& s);  // fixstr / str8 / str16 / str32
    void Bool(bool b);               // 0xc2 / 0xc3

private:
    void put(uint8_t b) { m_out.push_back(static_cast<char>(b)); }
    void be(uint64_t v, int bytes);  // big-endian, `bytes` wide

    std::vector<char>& m_out;
};

}} // namespace sc::mp

#endif // SC_NET_MSGPACK_MIN_H
