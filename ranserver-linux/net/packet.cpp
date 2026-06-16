#include "packet.h"

#include <cstring>

namespace sc { namespace net {

std::vector<char> EncodeMessage(uint32_t type, const void* payload, std::size_t payloadLen) {
    const uint32_t total = static_cast<uint32_t>(kHeaderSize + payloadLen);
    std::vector<char> out(total);
    unsigned char* p = reinterpret_cast<unsigned char*>(out.data());
    WriteLE32(p,     total);   // dwSize = header + payload
    WriteLE32(p + 4, type);    // nType
    if (payloadLen && payload)
        std::memcpy(out.data() + kHeaderSize, payload, payloadLen);
    return out;
}

bool MessageFramer::Feed(const char* data, std::size_t len, std::vector<Message>& out) {
    m_buf.insert(m_buf.end(), data, data + len);

    std::size_t pos = 0;
    while (m_buf.size() - pos >= kHeaderSize) {
        const unsigned char* h = reinterpret_cast<const unsigned char*>(m_buf.data()) + pos;
        const uint32_t dwSize = ReadLE32(h);
        const uint32_t nType  = ReadLE32(h + 4);

        if (dwSize < kHeaderSize || dwSize > kMaxMessageSize)
            return false;  // protocol error — malformed length

        if (m_buf.size() - pos < dwSize)
            break;         // incomplete frame; wait for more bytes

        Message m;
        m.type = nType;
        m.bytes.assign(m_buf.begin() + pos, m_buf.begin() + pos + dwSize);
        out.push_back(std::move(m));
        pos += dwSize;
    }

    if (pos > 0) m_buf.erase(m_buf.begin(), m_buf.begin() + pos);
    return true;
}

}} // namespace sc::net
