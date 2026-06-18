#ifndef SC_SERVERS_AGENT_SERVER_MSG_H
#define SC_SERVERS_AGENT_SERVER_MSG_H

#include <cstdint>

namespace sc { namespace servers {

// EMNET_MSG ordinals. s_NetGlobal.h has _USE_MESSAGE_SHUFFLE_ DISABLED, so the
// enum takes sequential positional values. Computed by counting enumerators from
// the last explicit anchor NET_MSG_UPDATE_TRACING_CHARACTER=221 (no #ifdef gaps
// in the range). NOTE: the inline `// 279/280/281/291` comments in the source
// are STALE — four *_TEST_CHA_* entries were inserted later, shifting everything
// by +4. Trust the computed ordinal, not the comment. See memory: enum-ordinal-not-comment.
constexpr uint32_t IDN_NET_MSG_LOGIN          = 233; // Client->Agent : IDN login request
constexpr uint32_t NET_MSG_LOGIN_FB           = 223; // Agent->Client : login result
constexpr uint32_t NET_MSG_REQ_CHA_BINFO_CA   = 283; // Client->Agent : request single char detail
constexpr uint32_t NET_MSG_REQ_CHA_BAINFO     = 284; // Client->Agent : request char ID list
constexpr uint32_t NET_MSG_CHA_BAINFO_AC      = 285; // Agent->Client : char ID list response
constexpr uint32_t NET_MSG_LOBBY_CHARINFO_AC      = 298; // Agent->Client : char lobby detail (msgpack)
constexpr uint32_t NET_MSG_LOBBY_CHARINFO_AC_END  = 299; // Agent->Client : end of char detail stream

constexpr int MAX_ONESERVERCHAR_NUM = 30; // per RanLogic/Network/NetLogicDefine.h

// NET_MSG_PACK envelope head (RanLogic/s_NetGlobal.h:3272). On the original
// Win32 32-bit client/server: HEAD_SIZE = sizeof(NET_MSG_GENERIC)=8 + m_Crc=4 +
// m_DataSize=4 = 16. CRITICAL: m_DataSize is declared `size_t` in the source —
// 4 bytes on Win32, but 8 on Linux x64. To stay wire-compatible with the 32-bit
// client we MUST encode it as a fixed uint32, NOT size_t. The 8-byte generic
// header (dwSize,nType) is emitted by EncodeMessage; the bytes below follow it.
struct NET_MSG_PACK_BODY {
    uint32_t m_Crc;       // 0 unless UseCrc (default false in SetData)
    uint32_t m_DataSize;  // msgpack payload length (fixed 32-bit; NOT size_t)
    // followed by m_DataSize bytes of msgpack
};
constexpr uint32_t NET_MSG_PACK_HEAD_SIZE = 16; // 8 (generic) + 4 (crc) + 4 (datasize)

#define USR_ID_LENGTH       20
#define MD5_MAX_LENGTH      32
#define MAX_IP_LENGTH       20
#define DAUM_MAX_GID_LENGTH 20

// EM_LOGIN_FB_SUB result codes
enum EM_LOGIN_FB_SUB {
    EM_LOGIN_FB_SUB_OK             = 0,
    EM_LOGIN_FB_SUB_FAIL           = 1,
    EM_LOGIN_FB_SUB_SYSTEM         = 2,
    EM_LOGIN_FB_SUB_USAGE          = 3,
    EM_LOGIN_FB_SUB_DUP            = 4,
    EM_LOGIN_FB_SUB_INCORRECT      = 5,
    EM_LOGIN_FB_SUB_IP_BAN         = 6,
    EM_LOGIN_FB_SUB_BLOCK          = 7,
    EM_LOGIN_FB_CH_FULL            = 15,
    EM_LOGIN_FB_SUB_RANDOM_PASS    = 19,   // gs_user_verify return 7
    EM_LOGIN_FB_SUB_ALREADYOFFLINE = 21,
    EM_LOGIN_FB_SUB_BETAKEY        = 23,   // gs_user_verify return 23 (GS_PARAM)
    EM_LOGIN_FB_WRONG_SP           = 25,
    EM_LOGIN_FB_KR_OK_USE_PASS     = 27,   // success + existing 2nd password (return 31)
    EM_LOGIN_FB_KR_OK_NEW_PASS     = 28,   // success + new 2nd password (return 30)
};

// ---- Character list wire structs -----------------------------------------------
// Faithful to s_NetGlobal.h NET_CHA_REQ_BA_INFO / NET_CHA_BA_INFO_CA /
// Msg/ServerMsg.h NET_CHA_BBA_INFO_AC.  All LE, no alignment padding assumed.

// Client->Agent : request character ID list (NET_MSG_REQ_CHA_BAINFO)
struct NET_CHA_REQ_BAINFO_DATA {
    uint32_t dwSize;
    uint32_t nType;
    int32_t  m_LauncherVer;
    int32_t  m_PatchVer;
    uint32_t m_Crc;
};

// Client->Agent : request single character detail (NET_MSG_REQ_CHA_BINFO_CA)
struct NET_CHA_BA_INFO_CA_DATA {
    uint32_t dwSize;
    uint32_t nType;
    int32_t  m_ChaDbNum;
};

// Agent->Client : character ID list response (NET_MSG_CHA_BAINFO_AC)
struct NET_CHA_BAINFO_AC_DATA {
    uint32_t dwSize;
    uint32_t nType;
    int32_t  m_ChaServerTotalNum;
    int32_t  m_ChaDbNum[MAX_ONESERVERCHAR_NUM];
};

// ---- Login request packet from client ------------------------------------------
// Request packet from client
struct IDN_NET_LOGIN_DATA {
    uint32_t dwSize;     // 8-byte header part 1
    uint32_t nType;      // 8-byte header part 2
    int      m_nChannel;
    char     m_szUserid[USR_ID_LENGTH + 1];      // char[21]
    char     m_szPassword[MD5_MAX_LENGTH + 1];  // char[33]
};

// Response packet to client
struct NET_LOGIN_FEEDBACK_DATA {
    uint32_t dwSize;     // 8-byte header part 1
    uint32_t nType;      // 8-byte header part 2
    int      m_Result;   // EM_LOGIN_FB_SUB
    int      m_Extreme;
    int      m_bActor;
    int      m_CheckFlag;
    int      m_LauncherVersion;
    int      m_GameVersion;
    uint16_t m_ChaRemain;
    char     m_szDaumGID[DAUM_MAX_GID_LENGTH + 1]; // char[21]
    char     pad1[1];
    int      m_Country;
    uint32_t m_sCountryInfo;
    int      m_bJumping;
};

// Internal database record structure read from GSUserInfo
struct DBUserInfo {
    int         userNum = 0;
    int         userType = 0;
    uint16_t    chaRemain = 0;
    uint16_t    chaTestRemain = 0;
    std::string premiumDate;
    std::string chatBlockDate;
    std::string lastLoginDate;
};

}} // namespace sc::servers

#endif // SC_SERVERS_AGENT_SERVER_MSG_H
