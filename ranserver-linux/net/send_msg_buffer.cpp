#include "send_msg_buffer.h"
#include "minlzo.h"
#include "packet.h" // for NET_MSG_COMPRESS
#include <cstring>

namespace sc { namespace net {

SendMsgBuffer::SendMsgBuffer()
    : m_dwPos(0),
      m_usCount(0)
{
    m_pBuffer     = new uint8_t[NET_DATA_MSG_BUFSIZE];
    m_pSendBuffer = new uint8_t[NET_DATA_MSG_BUFSIZE];
    std::memset(m_pBuffer, 0, NET_DATA_MSG_BUFSIZE);
    std::memset(m_pSendBuffer, 0, NET_DATA_MSG_BUFSIZE);
}

SendMsgBuffer::~SendMsgBuffer()
{
    delete [] m_pBuffer;
    delete [] m_pSendBuffer;
}

void SendMsgBuffer::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dwPos = 0;
    m_usCount = 0;
}

int SendMsgBuffer::addMsg(const void* pMsg, uint32_t dwSize)
{
    if (!pMsg || dwSize > NET_DATA_BUFSIZE) 
    {
        return BUFFER_ERROR;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint32_t dwTotal = m_dwPos + dwSize;    
    
    if (dwTotal < COMPRESS_PACKET_SIZE)
    {
        std::memcpy(m_pBuffer + m_dwPos, pMsg, dwSize);
        m_dwPos += dwSize;
        m_usCount++;
        return BUFFER_ADDED;
    }
    else 
    {
        if (m_dwPos == 0)
        {
            std::memcpy(m_pBuffer, pMsg, dwSize);
            m_dwPos = dwSize;
            m_usCount++;
            return BUFFER_SEND;
        }
        else 
        {
            return BUFFER_SEND_ADD;
        }
    }
}

int SendMsgBuffer::getSendSize()
{
    if (m_dwPos == 0)
        return 0;
    
    std::lock_guard<std::mutex> lock(m_mutex);

    int nOutLength = static_cast<int>(NET_DATA_MSG_BUFSIZE - sizeof(NET_COMPRESS));
    int nCompressResult = 0;
    NET_COMPRESS* pNmc = nullptr;
    uint32_t dwSendSize = 0;
    
    // Ensure LZO is initialized
    CMinLzo::GetInstance().init();

    nCompressResult = CMinLzo::GetInstance().lzoCompress(
        m_pBuffer, 
        m_dwPos, 
        m_pSendBuffer + sizeof(NET_COMPRESS),
        nOutLength);

    if (nCompressResult == CMinLzo::MINLZO_SUCCESS)
    {
        pNmc = reinterpret_cast<NET_COMPRESS*>(m_pSendBuffer);
        pNmc->nType = NET_MSG_COMPRESS;
        dwSendSize = sizeof(NET_COMPRESS) + nOutLength;
        pNmc->dwSize = dwSendSize;
        pNmc->bCompress = 1;
        std::memset(pNmc->pad, 0, sizeof(pNmc->pad));
    }
    else
    {
        std::memcpy(m_pSendBuffer + sizeof(NET_COMPRESS), m_pBuffer, m_dwPos);
        pNmc = reinterpret_cast<NET_COMPRESS*>(m_pSendBuffer);
        pNmc->nType = NET_MSG_COMPRESS;
        dwSendSize = sizeof(NET_COMPRESS) + m_dwPos;
        pNmc->dwSize = dwSendSize;
        pNmc->bCompress = 0;
        std::memset(pNmc->pad, 0, sizeof(pNmc->pad));
    }
    
    m_dwPos = 0;
    m_usCount = 0;
    return static_cast<int>(dwSendSize);
}

}} // namespace sc::net
