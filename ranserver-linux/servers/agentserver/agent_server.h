#ifndef SC_SERVERS_AGENT_SERVER_H
#define SC_SERVERS_AGENT_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "net_server.h"
#include "msg_manager.h"
#include "session.h"
#include "odbc_db.h"
#include "agent_server_msg.h"
#include "char_lobby.h"

namespace sc { namespace servers {

// AgentServer — fan-out server #4: Linux skeleton of CAgentServer
// (RanLogicServer/Server/AgentServer.{h,cpp}). Main client gateway: all
// in-game TCP connections are held here; the server proxies packets between
// clients and the Field Servers they inhabit.
//
// Full features deferred: regional login handlers (IdnMsgLogIn / ThaiMsgLogin
// etc.), Field Server connection pool (F_SERVER_INFO m_FieldServerInfo[][]),
// packet routing (SendField/SendFieldSvr/BroadcastToField), anti-cheat
// nProtect handshake, AgentServerSession sync with SessionServer, config XML.
class AgentServer : public sc::net::NetServer {
public:
    AgentServer(unsigned short port, std::string dbConn);

    long Accepted()  const { return m_accepted.load(); }
    long Updates()   const { return m_updates.load(); }
    long Processed() const { return m_processed.load(); }
    bool DbReady()   const { return m_dbReady.load(); }

protected:
    int  OnStart() override;
    void OnStop()  override;
    void OnAccept(boost::asio::ip::tcp::socket sock) override;
    void OnUpdate() override;
    void OnRegularSchedule() override;

private:
    void dispatch(const sc::net::Message& msg);

    bool ProcessUserLogin(const std::string& username, const std::string& password, const std::string& ip,
                         int& userNum, int& userType, uint16_t& chaRemain, uint16_t& chaTestRemain,
                         std::string& premiumDate, std::string& chatBlockDate, std::string& lastLoginDate,
                         int& errCode);

    // Query dbo.sp_ChaListAgent(@UserNum, @ServerGroup) and fill charNums.
    // Must be called with m_dbMx held by the caller.
    bool ProcessCharList(int userNum, std::vector<int>& charNums);

    // Query dbo.sp_GetChaLobbyInfo(@UserNum, @ChaNum) and fill out for the
    // char-select detail card. Must be called with m_dbMx held by the caller.
    // Equipment (m_PutOnItems) left as SLOT_TSIZE empties — populating it needs
    // the item subsystem (ItemSelect/INVEN_PUTON), a deferred follow-on.
    bool ProcessCharDetail(int userNum, int chaNum, CharInfoLobby& out);

    // Per-client state set on successful login (mirrors CClientManager::UserDbNum).
    // Protected by m_sessMx (reuses the session map lock).
    struct LoginSession {
        int userNum  = 0;
        int userType = 0;
    };

    std::string       m_dbConn;
    sc::db::OdbcDb    m_db;
    std::mutex        m_dbMx;
    std::atomic<bool> m_dbReady{false};
    std::atomic<long> m_accepted{0};
    std::atomic<long> m_updates{0};
    std::atomic<long> m_processed{0};

    sc::net::MsgManager m_recvMsgs;
    std::atomic<uint32_t> m_nextClientId{0};
    std::mutex m_sessMx;
    std::unordered_map<uint32_t, std::shared_ptr<sc::net::Session>> m_sessions;
    std::unordered_map<uint32_t, LoginSession> m_loginSessions; // keyed by clientId
};

}} // namespace sc::servers

#endif // SC_SERVERS_AGENT_SERVER_H
