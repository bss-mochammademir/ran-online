#include "auth_server.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace sc { namespace servers {

using tcp = boost::asio::ip::tcp;

namespace {
// Split the slice cert payload "a;b;c;..." (production uses the binary
// NET_AUTH_CERTIFICATION_REQUEST struct; that wire format is a deferred follow-on).
std::vector<std::string> splitFields(const std::string& s, char d) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
} // namespace

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

    // Resolve the sender's session for the reply.
    std::shared_ptr<sc::net::Session> session;
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        auto it = m_sessions.find(msg.clientId);
        if (it != m_sessions.end()) session = it->second;
    }
    if (!session) return;

    auto reply = [&](uint32_t type, const std::string& body) {
        session->Send(std::make_shared<std::vector<char>>(
            sc::net::EncodeMessage(type, body.data(), body.size())));
    };

    // Route by message type (the AuthPacketFunc dispatch table). The cert request
    // is the first real handler ported (MsgAuthCertificationRequest); other types
    // are echoed for observability until ported.
    if (msg.type == kMsgAuthCertReq) {
        std::string payload(msg.payload(), msg.payloadSize());
        auto f = splitFields(payload, ';');
        if (f.size() < 6) { reply(kMsgAuthCertAns, "0;0;0;"); return; }
        CertRequest req;
        req.country      = std::atoi(f[0].c_str());
        req.serverType   = std::atoi(f[1].c_str());
        req.ip           = f[2];
        req.port         = std::atoi(f[3].c_str());
        req.uniqKey      = f[4];
        req.isSessionSvr = std::atoi(f[5].c_str());

        CertResult res;
        bool ok = ProcessCertificationForAuth(req, res);
        Log("cert request uniqKey=" + req.uniqKey + " -> " +
            (ok ? "certification=" + std::to_string(res.certification) : "DB_ERROR: " + m_db.LastError()));
        // response: ok;certification;sessionSvrId;newUniqKey
        const std::string out = std::string(ok && res.ok ? "1" : "0") + ";" +
                                std::to_string(res.certification) + ";" +
                                std::to_string(res.sessionSvrId) + ";" + res.newUniqKey;
        reply(kMsgAuthCertAns, out);
        return;
    }

    // default: echo back (un-ported message types) so the path stays observable.
    reply(msg.type, std::string(msg.payload(), msg.payloadSize()));
}

bool AuthServer::ProcessCertificationForAuth(const CertRequest& r, CertResult& out) {
    // Verbatim port of AdoManager::ProcessCertificationForAuth: same param order,
    // same EXEC, same result-set columns — now over OdbcDb (params via the write
    // path, result row via the read path). dwSize/types handled by OdbcDb.
    m_db.ClearParams();
    m_db.AppendIParamInteger("@country", r.country);
    m_db.AppendIParamInteger("@servertype", r.serverType);
    m_db.AppendIParamVarchar("@serverip", r.ip, static_cast<long>(r.ip.size()));
    m_db.AppendIParamInteger("@serverport", r.port);
    m_db.AppendIParamVarchar("@uniqKey", r.uniqKey, static_cast<long>(r.uniqKey.size()));
    m_db.AppendIParamInteger("@IsSessionSvr", r.isSessionSvr);

    if (!m_db.ExecuteStoredProcedure("dbo.sp_CertificationUniqKey")) return false;

    if (m_db.GetEOF()) { out.ok = true; return true; }   // DB_OK with no row
    do {
        // forward-only cursor -> read columns in ascending result-set order
        m_db.GetCollect("SessionSvrID", out.sessionSvrId);
        m_db.GetCollect("Certification", out.certification);
        m_db.GetCollect("UniqKey", out.newUniqKey);
    } while (m_db.Next());
    out.ok = true;
    return true;
}

void AuthServer::OnRegularSchedule() {
    Log("regular schedule tick (heartbeat / session check stub)");
}

}} // namespace sc::servers
