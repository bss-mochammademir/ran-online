#include "login_server.h"

#include <string>
#include <vector>

namespace sc { namespace servers {

using tcp = boost::asio::ip::tcp;

LoginServer::LoginServer(unsigned short port, std::string dbConn)
    : sc::net::NetServer("LoginServer", port),
      m_dbConn(std::move(dbConn)) {}

int LoginServer::OnStart() {
    if (m_dbConn.empty()) {
        Log("no DB connection string -> running lifecycle-only (DB layer skipped)");
        return 0;
    }
    if (!m_db.Open(m_dbConn)) {
        Log("StartDbManager: User DB open FAILED: " + m_db.LastError());
        return -1;
    }
    if (!m_db.Execute("SELECT TOP 1 name FROM sys.tables ORDER BY name")) {
        Log("StartDbManager: probe query FAILED: " + m_db.LastError());
        return -1;
    }
    std::string firstTable;
    if (!m_db.GetEOF()) m_db.GetCollect("name", firstTable);
    m_dbReady.store(true);
    Log("StartDbManager: User DB connected (probe row name=" + firstTable + ")");
    return 0;
}

void LoginServer::OnStop() {
    if (m_db.IsOpened()) m_db.Close();
    Log("DB released");
}

void LoginServer::OnAccept(tcp::socket sock) {
    const uint32_t clientId = ++m_nextClientId;
    ++m_accepted;
    boost::system::error_code ec;
    auto ep = sock.remote_endpoint(ec);
    Log("client #" + std::to_string(clientId) + " connected" +
        (ec ? std::string() : " from " + ep.address().to_string()));

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

void LoginServer::OnUpdate() {
    ++m_updates;
    m_recvMsgs.MsgQueueFlip();
    sc::net::Message msg;
    while (m_recvMsgs.GetMsg(msg)) dispatch(msg);
}

void LoginServer::dispatch(const sc::net::Message& msg) {
    ++m_processed;
    std::shared_ptr<sc::net::Session> session;
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        auto it = m_sessions.find(msg.clientId);
        if (it != m_sessions.end()) session = it->second;
    }
    if (!session) return;
    // echo back until handlers are ported (version check, server-list query)
    session->Send(std::make_shared<std::vector<char>>(
        sc::net::EncodeMessage(msg.type, msg.payload(), msg.payloadSize())));
}

void LoginServer::OnRegularSchedule() {
    // stub: in production syncs server-list from SessionServer
    Log("regular schedule tick (session-server sync stub)");
}

}} // namespace sc::servers
