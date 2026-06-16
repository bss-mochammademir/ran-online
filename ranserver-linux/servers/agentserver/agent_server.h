#ifndef SC_SERVERS_AGENT_SERVER_H
#define SC_SERVERS_AGENT_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "net_server.h"
#include "msg_manager.h"
#include "session.h"
#include "odbc_db.h"

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

    std::string       m_dbConn;
    sc::db::OdbcDb    m_db;
    std::atomic<bool> m_dbReady{false};
    std::atomic<long> m_accepted{0};
    std::atomic<long> m_updates{0};
    std::atomic<long> m_processed{0};

    sc::net::MsgManager m_recvMsgs;
    std::atomic<uint32_t> m_nextClientId{0};
    std::mutex m_sessMx;
    std::unordered_map<uint32_t, std::shared_ptr<sc::net::Session>> m_sessions;
};

}} // namespace sc::servers

#endif // SC_SERVERS_AGENT_SERVER_H
