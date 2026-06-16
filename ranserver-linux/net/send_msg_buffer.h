#ifndef SC_NET_SEND_MSG_BUFFER_H
#define SC_NET_SEND_MSG_BUFFER_H

#pragma once

#include <cstdint>
#include <mutex>

namespace sc { namespace net {

#pragma pack(push, 1)
struct NET_COMPRESS {
    uint32_t dwSize;     // Total packet size on wire (header + payload)
    uint32_t nType;      // NET_MSG_COMPRESS (170)
    uint8_t  bCompress;  // 1 = compressed, 0 = uncompressed
    uint8_t  pad[3];     // Alignment padding to exactly 12 bytes
};
#pragma pack(pop)

class SendMsgBuffer
{
public:
    enum 
    { 
        BUFFER_SEND     = 1,
        BUFFER_ADDED    = 2,
        BUFFER_SEND_ADD = 3,
        BUFFER_ERROR    = -1
    };

    static constexpr uint32_t NET_DATA_BUFSIZE = 2048;
    static constexpr uint32_t NET_DATA_MSG_BUFSIZE = 4096;
    static constexpr uint32_t COMPRESS_PACKET_SIZE = 1000;

public:
    SendMsgBuffer();
    ~SendMsgBuffer();

    int addMsg(const void* pMsg, uint32_t dwSize);
    void reset();
    
    const uint8_t* getSendBuffer() const { return m_pSendBuffer; }
    uint32_t getMsgSize() const          { return m_dwPos; }
    uint16_t getMsgCount() const         { return m_usCount; }
    int getSendSize();

protected:
    uint32_t m_dwPos;
    uint16_t m_usCount;
    uint8_t* m_pBuffer;
    uint8_t* m_pSendBuffer;
    std::mutex m_mutex;
};

}} // namespace sc::net

#endif // SC_NET_SEND_MSG_BUFFER_H
