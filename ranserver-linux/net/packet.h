#ifndef SC_NET_PACKET_H
#define SC_NET_PACKET_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sc { namespace net {

// Wire format — faithful port of the Ran Online binary message header
// NET_MSG_GENERIC (RanLogic/s_NetGlobal.h): an 8-byte little-endian header
//
//     struct NET_MSG_GENERIC { DWORD dwSize; EMNET_MSG nType; };   // 8 bytes
//
// where dwSize is the *total* message length INCLUDING the 8-byte header
// (an empty message has dwSize == 8; NET_MSG_CHARACTER is 12; etc.), and nType
// is the message-type enum (treated here as an opaque uint32; the EMNET_MSG
// values live in s_NetGlobal.h and are mapped when the handlers are ported).
//
// We serialize the header explicitly as little-endian (the native order on the
// x86/x64 Windows original and on the Linux target) rather than reinterpret_cast
// a struct — no packing/alignment/aliasing assumptions.
//
// NOT in this layer (follow-on chips): the NET_COMPRESS envelope + LZO
// compression (CSendMsgBuffer / SigmaCore CMinLzo), msgpack payloads, and the
// per-message CRC (NET_MSG_GCTRL). Those wrap/transform the payload and are
// additive on top of this framing.

constexpr std::size_t kHeaderSize     = 8;     // NET_MSG_GENERIC: dwSize(4) + nType(4)
constexpr std::size_t kMaxMessageSize = 2048;  // NET_DATA_BUFSIZE (addMsg() rejects above this)

inline uint32_t ReadLE32(const unsigned char* p) {
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}
inline void WriteLE32(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}

// A decoded inbound message. `bytes` is the full on-wire frame (header + payload),
// so bytes.size() == dwSize. Mirrors MSG_LIST (dwClient + Buffer) but dynamically
// sized instead of a fixed CHAR[2048].
struct Message {
    uint32_t          type     = 0;
    uint32_t          clientId = 0;
    std::vector<char> bytes;   // full frame incl. the 8-byte header

    const char* payload()      const { return bytes.data() + kHeaderSize; }
    std::size_t payloadSize()  const { return bytes.size() > kHeaderSize ? bytes.size() - kHeaderSize : 0; }
};

// Build a framed NET_MSG_GENERIC: [dwSize][nType][payload], dwSize = 8 + payloadLen.
std::vector<char> EncodeMessage(uint32_t type, const void* payload, std::size_t payloadLen);

// MessageFramer — reassembles a TCP byte stream into discrete NET_MSG_GENERIC
// messages (the "packet cutting" the IOCP worker did). Stateful: buffers partial
// messages across Feed() calls and emits every complete frame available.
class MessageFramer {
public:
    // Append `len` bytes; push every complete message onto `out`. Returns false on
    // a protocol error (dwSize < 8 or > kMaxMessageSize) — caller drops the link.
    bool Feed(const char* data, std::size_t len, std::vector<Message>& out);

    std::size_t Buffered() const { return m_buf.size(); }
    void        Reset()          { m_buf.clear(); }

private:
    std::vector<char> m_buf;   // accumulation across reads (holds a partial frame)
};

}} // namespace sc::net

#endif // SC_NET_PACKET_H
