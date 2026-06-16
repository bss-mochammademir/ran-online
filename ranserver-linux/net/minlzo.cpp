#include "minlzo.h"
#include <lzo/lzo1x.h>
#include <cstdlib>

namespace sc { namespace net {

CMinLzo& CMinLzo::GetInstance()
{
    static CMinLzo Instance;
    return Instance;
}

CMinLzo::CMinLzo(void)
    : m_pWorkmem(nullptr),
      m_bInit(false) {}

CMinLzo::~CMinLzo(void)
{
    if (m_pWorkmem != nullptr)
    {
        free(m_pWorkmem);
        m_pWorkmem = nullptr;
    }
}

int CMinLzo::init()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bInit)
    {
        return MINLZO_SUCCESS;
    }

    if (lzo_init() != LZO_E_OK)
    {
        m_strError = "internal error - lzo_init() failed !";
        m_bInit = false;
        return MINLZO_ERROR;
    }

    if (m_pWorkmem != nullptr)
    {
        free(m_pWorkmem);
        m_pWorkmem = nullptr;
    }

    m_pWorkmem = (lzo_bytep) malloc(LZO1X_1_MEM_COMPRESS);
    if (m_pWorkmem == nullptr)
    {
        m_strError = "out of memory";
        m_bInit = false;
        return MINLZO_ERROR;
    }

    m_bInit = true;
    return MINLZO_SUCCESS;
}

int CMinLzo::lzoCompress(
    const uint8_t* pInBuffer, 
    int nInLength, 
    uint8_t* pOutBuffer, 
    int &nOutLength)
{
    if (pInBuffer == nullptr || nInLength == 0 || pOutBuffer == nullptr)
    {
        m_strError = "lzoCompress input data error";
        return MINLZO_INPUT_DATA_ERROR;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    lzo_uint outLen = nOutLength;
    int nReturn = lzo1x_1_compress(pInBuffer,
                                   nInLength,
                                   pOutBuffer,
                                   &outLen,
                                   m_pWorkmem);

    nOutLength = static_cast<int>(outLen);

    if (nReturn == LZO_E_OK)
    {
        if (nOutLength >= nInLength)
        {
            m_strError = "lzoCompress can not compress";
            return MINLZO_CAN_NOT_COMPRESS;
        }
        else
        {
            return MINLZO_SUCCESS;
        }
    }
    else
    {
        m_strError = "lzoCompress internal error";
        return MINLZO_INTERNAL_ERROR;
    }
}

int CMinLzo::lzoDeCompress(
    const uint8_t* pOutBuffer, 
    int nOutLength, 
    uint8_t* pInBuffer, 
    int &nNewLength)
{
    if (pOutBuffer == nullptr || nOutLength == 0 || pInBuffer == nullptr)
    {
        m_strError = "lzoDeCompress input data error";
        return MINLZO_INPUT_DATA_ERROR;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    lzo_uint newLen = nNewLength;
    int nReturn = lzo1x_decompress_safe(pOutBuffer, 
                                        nOutLength,
                                        pInBuffer,
                                        &newLen,
                                        nullptr);

    nNewLength = static_cast<int>(newLen);

    if (nReturn == LZO_E_OK && nNewLength >= nOutLength)
    {
        return MINLZO_SUCCESS;
    }
    else
    {
        m_strError = "lzoDeCompress can not decompress";
        return MINLZO_ERROR;
    }
}

std::string& CMinLzo::getErrorString()
{
    return m_strError;
}

}} // namespace sc::net
