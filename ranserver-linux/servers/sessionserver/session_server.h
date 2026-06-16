#ifndef SC_SERVERS_SESSION_SERVER_H
#define SC_SERVERS_SESSION_SERVER_H

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

// SessionServer — fan-out server #3: Linux skeleton of CSessionServer
// (RanLogicServer/Server/SessionServer.{h,cpp}). Global state coordinator:
// tracks character sessions, relays inter-server chat, distributes channel
// load info to LoginServer, and verifies sessions with AuthServer.
//
// Full features deferred: character tracking (MsgChaIncrease/Decrease),
// chat relay (MsgChatProcess/MsgSndChatGlobal), server-channel sync
// (m_sServerChannel[][] heartbeat), GM command distribution, config XML.
class SessionServer : public sc::net::NetServer {
public:
    SessionServer(unsigned short port, std::string dbConn);

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

#endif // SC_SERVERS_SESSION_SERVER_H
