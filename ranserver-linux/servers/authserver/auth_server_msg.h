#ifndef SC_SERVERS_AUTH_SERVER_MSG_H
#define SC_SERVERS_AUTH_SERVER_MSG_H

#include <cstdint>

namespace sc { namespace servers {

// Production message types from s_NetGlobal.h
constexpr uint32_t NET_MSG_AUTH_CERTIFICATION_REQUEST = 204;
constexpr uint32_t NET_MSG_AUTH_CERTIFICATION_ANS     = 205;

// Constants from NetLogicDefine.h
constexpr std::size_t MAX_IP_LENGTH       = 20;
constexpr std::size_t SERVER_NAME_LENGTH  = 50;
constexpr std::size_t AUTH_DATA_LENGTH    = 500;

// Server types from NetLogicDefine.h
enum EMSERVERTYPE {
    SERVER_UNKNOWN            = 0,
    SERVER_LOGIN              = 1,
    SERVER_SESSION            = 2,
    SERVER_FIELD              = 3,
    SERVER_AGENT              = 4,
    SERVER_BOARD              = 5,
    SERVER_CACHE              = 6,
    SERVER_MATCH              = 7,
    SERVER_INSTANCE           = 8,
    SERVER_AUTH               = 9,
    SERVER_INTEGRATION_CACHE  = 10
};

// G_AUTH_INFO (592 bytes total size)
#pragma pack(push, 1)
struct G_AUTH_INFO {
    int  ServerType;                    // 4 bytes
    int  nCounrtyType;                  // 4 bytes
    char szServerIP[MAX_IP_LENGTH + 1]; // 21 bytes
    char pad1[3];                       // 3 bytes padding (align next int to 4-byte boundary)
    int  nServicePort;                  // 4 bytes
    int  nSessionSvrID;                 // 4 bytes
    char szServerName[SERVER_NAME_LENGTH + 1]; // 51 bytes
    char szAuthData[AUTH_DATA_LENGTH + 1];     // 501 bytes
    char pad2[3];                       // 3 bytes padding (align structure size to 8-byte boundary)
};
#pragma pack(pop)

// Real wire structures (600 bytes total size)
#pragma pack(push, 1)
struct NET_AUTH_CERTIFICATION_REQUEST {
    uint32_t    dwSize;                 // 4 bytes
    uint32_t    nType;                  // 4 bytes
    G_AUTH_INFO gsi;                    // 592 bytes
};

struct NET_AUTH_CERTIFICATION_ANS {
    uint32_t    dwSize;                 // 4 bytes
    uint32_t    nType;                  // 4 bytes
    G_AUTH_INFO gsi;                    // 592 bytes
};
#pragma pack(pop)

}} // namespace sc::servers

#endif // SC_SERVERS_AUTH_SERVER_MSG_H
