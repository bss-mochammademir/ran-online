#include "field_server.h"

#include <string>
#include <vector>

namespace sc { namespace servers {

using tcp = boost::asio::ip::tcp;

FieldServer::FieldServer(unsigned short port, std::string dbConn)
    : sc::net::NetServer("FieldServer", port),
      m_dbConn(std::move(dbConn)) {}

int FieldServer::OnStart() {
    if (m_dbConn.empty()) {
        Log("no DB connection string -> running lifecycle-only (DB layer skipped)");
        return 0;
    }
    // FieldServer opens the Game DB (RanGame) for character data: inventory,
    // skills, quests. Production writes go through CacheServer (deferred).
    if (!m_db.Open(m_dbConn)) {
        Log("StartDbManager: Game DB open FAILED: " + m_db.LastError());
        return -1;
    }
    if (!m_db.Execute("SELECT TOP 1 name FROM sys.tables ORDER BY name")) {
        Log("StartDbManager: probe query FAILED: " + m_db.LastError());
        return -1;
    }
    std::string firstTable;
    if (!m_db.GetEOF()) m_db.GetCollect("name", firstTable);
    m_dbReady.store(true);
    Log("StartDbManager: Game DB connected (probe row name=" + firstTable + ")");
    // stub: production loads GLGaeaServer (map data, monster spawns, quest scripts)
    Log("GLGaeaServer stub: Gaea Engine init deferred");
    return 0;
}

void FieldServer::OnStop() {
    if (m_db.IsOpened()) m_db.Close();
    // stub: production would flush CacheServer write queue before close
    Log("DB + Gaea Engine released");
}

void FieldServer::OnAccept(tcp::socket sock) {
    // FieldServer only accepts connections from AgentServer, not directly from clients.
    const uint32_t clientId = ++m_nextClientId;
    ++m_accepted;
    boost::system::error_code ec;
    auto ep = sock.remote_endpoint(ec);
    Log("AgentServer connection #" + std::to_string(clientId) +
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

void FieldServer::OnUpdate() {
    ++m_updates;
    // UpdateProc: in production calls GLGaeaServer::Update (AI tick, combat calc, map update).
    m_recvMsgs.MsgQueueFlip();
    sc::net::Message msg;
    while (m_recvMsgs.GetMsg(msg)) dispatch(msg);
}

void FieldServer::dispatch(const sc::net::Message& msg) {
    ++m_processed;
    std::shared_ptr<sc::net::Session> session;
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        auto it = m_sessions.find(msg.clientId);
        if (it != m_sessions.end()) session = it->second;
    }
    if (!session) return;
    // echo back until GLGaeaServerMsg handlers are ported (combat, inventory, AI)
    session->Send(std::make_shared<std::vector<char>>(
        sc::net::EncodeMessage(msg.type, msg.payload(), msg.payloadSize())));
}

void FieldServer::OnRegularSchedule() {
    // stub: in production monitors memory (m_dwMemoryCheckTime) and triggers ForceStop
    // if approaching the 32-bit address space limit of the original server.
    Log("regular schedule tick (memory guard + CacheServer flush stub)");
}

}} // namespace sc::servers
