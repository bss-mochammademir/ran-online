#include "auth_server.h"

#include <vector>

namespace sc { namespace servers {

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
    const uint32_t clientId = ++m_nextClientId;
    ++m_accepted;
    boost::system::error_code ec;
    auto ep = sock.remote_endpoint(ec);
    Log("client #" + std::to_string(clientId) + " connected" +
        (ec ? std::string() : " from " + ep.address().to_string()));

    // Per-client Session (NetAuthClientMan slot): framed messages -> recv MsgManager
    // Front queue; link-drop -> drop the session.
    auto session = std::make_shared<sc::net::Session>(
        std::move(sock), clientId,
        [this](uint32_t /*id*/, sc::net::Message&& m) { m_recvMsgs.MsgQueueInsert(std::move(m)); },
        [this](uint32_t id) {
            std::lock_guard<std::mutex> lk(m_sessMx);
            m_sessions.erase(id);
        });
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        m_sessions[clientId] = session;
    }
    session->Start();
}

void AuthServer::OnUpdate() {
    ++m_updates;
    // UpdateProc: flip the double-buffer, then MsgProcess() each queued message.
    m_recvMsgs.MsgQueueFlip();
    sc::net::Message msg;
    while (m_recvMsgs.GetMsg(msg)) dispatch(msg);
}

void AuthServer::dispatch(const sc::net::Message& msg) {
    ++m_processed;
    // TODO(follow-on): route via AuthPacketFunc (MsgAuthCertificationRequest ->
    // CertificateAuthDataFromDB, session/event/server-state). Skeleton: echo the
    // frame back to its sender so the full recv->process->send path is observable.
    std::shared_ptr<sc::net::Session> session;
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        auto it = m_sessions.find(msg.clientId);
        if (it != m_sessions.end()) session = it->second;
    }
    if (session) {
        auto framed = std::make_shared<std::vector<char>>(
            sc::net::EncodeMessage(msg.type, msg.payload(), msg.payloadSize()));
        session->Send(framed);
    }
}

void AuthServer::OnRegularSchedule() {
    Log("regular schedule tick (heartbeat / session check stub)");
}

}} // namespace sc::servers
