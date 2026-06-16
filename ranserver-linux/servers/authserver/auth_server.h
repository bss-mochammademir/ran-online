#ifndef SC_SERVERS_AUTH_SERVER_H
#define SC_SERVERS_AUTH_SERVER_H

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

// AuthServer — first fan-out server: the Linux port skeleton of CAuthServer
// (RanLogicServer/Server/AuthServer.{h,cpp}), riding the two ported shared layers:
//   * sc::net::NetServer  (IOCP -> boost::asio lifecycle base)
//   * sc::db::OdbcDb      (CjADO -> ODBC data layer)
//
// It mirrors CAuthServer's lifecycle shape: Start() connects the DB (StartDbManager)
// before serving; accepts spin up a per-client Session whose framed messages land in
// the recv MsgManager (m_pRecvMsgManager in NetServer); the UpdateProc tick flips the
// double-buffer and dispatches each message (MsgProcess). The certification handlers
// themselves (MsgAuthCertificationRequest -> CertificateAuthDataFromDB) and the
// NET_COMPRESS/LZO + msgpack + CRC layers are NOT yet ported — follow-on chips. The
// current dispatch echoes each frame back, so the full receive pipeline is observable.
class AuthServer : public sc::net::NetServer {
public:
    // dbConn: ODBC connection string for the User DB (empty -> lifecycle-only run,
    // DB layer skipped). port: TCP listen port.
    AuthServer(unsigned short port, std::string dbConn);

    long Accepted()  const { return m_accepted.load(); }
    long Updates()   const { return m_updates.load(); }
    long Processed() const { return m_processed.load(); }  // messages dispatched
    bool DbReady()   const { return m_dbReady.load(); }

protected:
    int  OnStart() override;            // StartDbManager: open + probe the User DB
    void OnStop()  override;
    void OnAccept(boost::asio::ip::tcp::socket sock) override;  // ListenProc accept
    void OnUpdate() override;           // UpdateProc: flip recv queue + MsgProcess
    void OnRegularSchedule() override;  // RegularScheduleProc tick (stub)

private:
    void dispatch(const sc::net::Message& msg);   // MsgProcess() stub (echo)

    std::string       m_dbConn;
    sc::db::OdbcDb    m_db;             // touched only in OnStart for now (single-thread);
                                        // per-worker pooling is a deferred DB chip.
    std::atomic<bool> m_dbReady{false};
    std::atomic<long> m_accepted{0};
    std::atomic<long> m_updates{0};
    std::atomic<long> m_processed{0};

    sc::net::MsgManager m_recvMsgs;     // double-buffer recv queue (NetServer m_pRecvMsgManager)
    std::atomic<uint32_t> m_nextClientId{0};
    std::mutex m_sessMx;
    std::unordered_map<uint32_t, std::shared_ptr<sc::net::Session>> m_sessions;  // NetAuthClientMan
};

}} // namespace sc::servers

#endif // SC_SERVERS_AUTH_SERVER_H
