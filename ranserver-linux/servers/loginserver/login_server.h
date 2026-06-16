#ifndef SC_SERVERS_LOGIN_SERVER_H
#define SC_SERVERS_LOGIN_SERVER_H

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

// LoginServer — fan-out server #2: Linux skeleton of CLoginServer
// (RanLogicServer/Server/LoginServer.{h,cpp}). Lobby/directory: clients connect,
// receive the list of available Agent Server channels, then disconnect and connect
// directly to their chosen Agent Server. Rides the shared sc::net + sc::db layers.
//
// Full features deferred: GeoIP filtering (GeoIP* m_pGeoIP), version check
// (MsgVersion), server-list sync from SessionServer (G_SERVER_CUR_INFO_LOGIN),
// NET_MSG_REQ_GAME_SVR / NET_MSG_SND_GAME_SVR handlers, config XML.
class LoginServer : public sc::net::NetServer {
public:
    LoginServer(unsigned short port, std::string dbConn);

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

#endif // SC_SERVERS_LOGIN_SERVER_H
