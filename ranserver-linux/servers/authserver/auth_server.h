#ifndef SC_SERVERS_AUTH_SERVER_H
#define SC_SERVERS_AUTH_SERVER_H

#include <atomic>
#include <string>
#include "net_server.h"
#include "odbc_db.h"

namespace sc { namespace servers {

// AuthServer — first fan-out server: the Linux port skeleton of CAuthServer
// (RanLogicServer/Server/AuthServer.{h,cpp}), riding the two ported shared layers:
//   * sc::net::NetServer  (IOCP -> boost::asio lifecycle base)
//   * sc::db::OdbcDb      (CjADO -> ODBC data layer)
//
// It mirrors CAuthServer's lifecycle shape: Start() connects the DB (StartDbManager)
// before serving; accepts drive per-client handling (ListenProc); a periodic tick
// drives message processing (UpdateProc). The packet protocol (MsgManager) and the
// certification handlers (MsgAuthCertificationRequest -> CertificateAuthDataFromDB)
// are NOT yet ported — follow-on chips; OnAccept currently greets + closes, marking
// exactly where that layer plugs in.
class AuthServer : public sc::net::NetServer {
public:
    // dbConn: ODBC connection string for the User DB (empty -> lifecycle-only run,
    // DB layer skipped). port: TCP listen port.
    AuthServer(unsigned short port, std::string dbConn);

    long Accepted() const { return m_accepted.load(); }
    long Updates()  const { return m_updates.load(); }
    bool DbReady()  const { return m_dbReady.load(); }

protected:
    int  OnStart() override;            // StartDbManager: open + probe the User DB
    void OnStop()  override;
    void OnAccept(boost::asio::ip::tcp::socket sock) override;  // ListenProc accept
    void OnUpdate() override;           // UpdateProc tick (stub)
    void OnRegularSchedule() override;  // RegularScheduleProc tick (stub)

private:
    std::string       m_dbConn;
    sc::db::OdbcDb    m_db;             // touched only in OnStart for now (single-thread);
                                        // per-worker pooling is a deferred DB chip.
    std::atomic<bool> m_dbReady{false};
    std::atomic<long> m_accepted{0};
    std::atomic<long> m_updates{0};
};

}} // namespace sc::servers

#endif // SC_SERVERS_AUTH_SERVER_H
