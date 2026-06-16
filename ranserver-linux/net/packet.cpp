#include "packet.h"
#include "minlzo.h"
#include <cstring>
#include <vector>

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

        if (nType == NET_MSG_COMPRESS) {
            if (dwSize < 12)
                return false; // NET_COMPRESS struct size is 12 bytes minimum

            const uint8_t bCompress = h[8];
            
            if (bCompress == 1) {
                // Initialize LZO if not done yet
                CMinLzo::GetInstance().init();
                
                std::vector<uint8_t> decompBuf(32768); // 32KB client receive buffer size
                int decompSize = static_cast<int>(decompBuf.size());
                
                int res = CMinLzo::GetInstance().lzoDeCompress(
                    h + 12,
                    dwSize - 12,
                    decompBuf.data(),
                    decompSize
                );
                
                if (res != CMinLzo::MINLZO_SUCCESS) {
                    return false; // Decompression failure is a protocol error
                }
                
                // Parse nested messages from the decompressed buffer
                std::size_t decPos = 0;
                while (static_cast<int>(decompSize - decPos) >= static_cast<int>(kHeaderSize)) {
                    const unsigned char* decH = decompBuf.data() + decPos;
                    uint32_t decMsgSize = ReadLE32(decH);
                    uint32_t decMsgType = ReadLE32(decH + 4);
                    
                    if (decMsgSize < kHeaderSize || decMsgSize > (decompSize - decPos)) {
                        return false; // Malformed inner packet size
                    }
                    
                    Message innerMsg;
                    innerMsg.type = decMsgType;
                    innerMsg.bytes.assign(decompBuf.begin() + decPos, decompBuf.begin() + decPos + decMsgSize);
                    out.push_back(std::move(innerMsg));
                    decPos += decMsgSize;
                }
            } else {
                // Uncompressed batch of messages: parse directly from the envelope payload
                std::size_t rawPos = pos + 12;
                std::size_t endPos = pos + dwSize;
                
                while (endPos - rawPos >= kHeaderSize) {
                    const unsigned char* rawH = reinterpret_cast<const unsigned char*>(m_buf.data()) + rawPos;
                    uint32_t rawMsgSize = ReadLE32(rawH);
                    uint32_t rawMsgType = ReadLE32(rawH + 4);
                    
                    if (rawMsgSize < kHeaderSize || rawMsgSize > (endPos - rawPos)) {
                        return false; // Malformed inner packet size
                    }
                    
                    Message innerMsg;
                    innerMsg.type = rawMsgType;
                    innerMsg.bytes.assign(m_buf.begin() + rawPos, m_buf.begin() + rawPos + rawMsgSize);
                    out.push_back(std::move(innerMsg));
                    rawPos += rawMsgSize;
                }
            }
        } else {
            // Standard uncompressed message
            Message m;
            m.type = nType;
            m.bytes.assign(m_buf.begin() + pos, m_buf.begin() + pos + dwSize);
            out.push_back(std::move(m));
        }

        pos += dwSize;
    }

    if (pos > 0) m_buf.erase(m_buf.begin(), m_buf.begin() + pos);
    return true;
}

}} // namespace sc::net

