#ifndef SC_NET_MIN_LZO_H_
#define SC_NET_MIN_LZO_H_

#pragma once

#include <string>
#include <mutex>
#include <cstdint>

// Forward declare lzo types to avoid pulling heavy headers in minlzo.h
typedef unsigned char* lzo_bytep;

namespace sc { namespace net {

class CMinLzo
{
public:
    enum
    {
        MINLZO_SUCCESS          =  0,
        MINLZO_ERROR            = -1,
        MINLZO_INPUT_DATA_ERROR = -2,
        MINLZO_INTERNAL_ERROR   = -3,
        MINLZO_CAN_NOT_COMPRESS = -4,
    };

public:
    static CMinLzo& GetInstance();

private:
    CMinLzo(void);
    ~CMinLzo(void);

public:
    int init();

    int lzoCompress(const uint8_t* pInBuffer, 
                    int nInLength, 
                    uint8_t* pOutBuffer, 
                    int &nOutLength);

    int lzoDeCompress(const uint8_t* pOutBuffer,
                      int nOutLength, 
                      uint8_t* pInBuffer, 
                      int &nNewLength);

    std::string& getErrorString();

protected:
    lzo_bytep m_pWorkmem;
    bool m_bInit;
    std::string m_strError;
    std::mutex m_mutex;
};

}} // namespace sc::net

#endif // SC_NET_MIN_LZO_H_
