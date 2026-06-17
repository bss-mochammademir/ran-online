#ifndef SC_SERVERS_AGENT_SERVER_MSG_H
#define SC_SERVERS_AGENT_SERVER_MSG_H

#include <cstdint>

namespace sc { namespace servers {

constexpr uint32_t IDN_NET_MSG_LOGIN = 233;
constexpr uint32_t NET_MSG_LOGIN_FB  = 223;

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
