#include "auth_server.h"

#include <boost/asio/write.hpp>

namespace sc { namespace servers {

namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

AuthServer::AuthServer(unsigned short port, std::string dbConn)
    : sc::net::NetServer("AuthServer", port),
      m_dbConn(std::move(dbConn)) {}

int AuthServer::OnStart() {
    // CAuthServer::StartDbManager opens the User/Log DBs (COdbcManager) + the Auth DB.
    // The skeleton proves the ported data layer by opening the User DB and running a
    // trivial probe query. Empty conn -> lifecycle-only run (DB skipped).
    if (m_dbConn.empty()) {
        Log("no DB connection string -> running lifecycle-only (DB layer skipped)");
        return 0;
    }
    if (!m_db.Open(m_dbConn)) {
        Log("StartDbManager: User DB open FAILED: " + m_db.LastError());
        return -1;  // faithful: StartDbManager failure aborts Start()
    }
    if (!m_db.Execute("SELECT TOP 1 name FROM sys.tables ORDER BY name")) {
        Log("StartDbManager: probe query FAILED: " + m_db.LastError());
        return -1;
    }
    std::string firstTable;
    if (!m_db.GetEOF()) m_db.GetCollect("name", firstTable);
    m_dbReady.store(true);
    Log("StartDbManager: User DB connected via sc::db::OdbcDb (probe row name=" + firstTable + ")");
    return 0;
}

void AuthServer::OnStop() {
    if (m_db.IsOpened()) m_db.Close();
    Log("auth manager + DB released");
}

void AuthServer::OnAccept(tcp::socket sock) {
    long n = ++m_accepted;
    boost::system::error_code ec;
    auto ep = sock.remote_endpoint(ec);
    Log("client #" + std::to_string(n) + " connected" +
        (ec ? std::string() : " from " + ep.address().to_string()));

    // TODO(fan-out follow-on): feed bytes to a ported MsgManager + dispatch through
    // AuthPacketFunc (MsgAuthCertificationRequest -> CertificateAuthDataFromDB ...).
    // Skeleton: greet + close so the connection path is observable end-to-end.
    const std::string banner = "AUTHSERVER\n";
    asio::write(sock, asio::buffer(banner), ec);
    sock.shutdown(tcp::socket::shutdown_both, ec);
}

void AuthServer::OnUpdate() {
    ++m_updates;  // UpdateProc: would drain the recv MsgManager + run MsgProcess()
}

void AuthServer::OnRegularSchedule() {
    Log("regular schedule tick (heartbeat / session check stub)");
}

}} // namespace sc::servers
