#include "auth_server.h"

#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>

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

    // Flush outgoing messages for all sessions
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        for (auto& pair : m_sessions) {
            pair.second->Flush();
        }
    }
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
    if (msg.type == NET_MSG_AUTH_CERTIFICATION_REQUEST) {
        if (msg.payloadSize() < sizeof(G_AUTH_INFO)) {
            Log("cert request payload too small: " + std::to_string(msg.payloadSize()));
            return;
        }

        const G_AUTH_INFO* gsiReq = reinterpret_cast<const G_AUTH_INFO*>(msg.payload());

        // Decrypt szAuthData
        std::string decrypted = DecryptAuthData(gsiReq->szAuthData);
        if (decrypted.empty()) {
            Log("cert request: failed to decrypt szAuthData");
            return;
        }

        // Parse CSV: ServiceProvider, ServerType, IP, Port, UniqKey
        auto f = splitFields(decrypted, ',');
        if (f.size() < 5) {
            Log("cert request: invalid decrypted CSV fields count: " + std::to_string(f.size()));
            return;
        }

        CertRequest req;
        req.country      = std::atoi(f[0].c_str());
        req.serverType   = std::atoi(f[1].c_str());
        req.ip           = f[2];
        req.port         = std::atoi(f[3].c_str());
        req.uniqKey      = f[4];
        req.isSessionSvr = (req.serverType == SERVER_SESSION) ? 1 : 0;

        CertResult res;
        bool ok = ProcessCertificationForAuth(req, res);
        Log("cert request uniqKey=" + req.uniqKey + " -> " +
            (ok ? "certification=" + std::to_string(res.certification) : "DB_ERROR: " + m_db.LastError()));

        // Build G_AUTH_INFO for the answer
        G_AUTH_INFO gsiAns = {};
        gsiAns.nCounrtyType = req.country;
        gsiAns.ServerType   = req.serverType;
        std::strncpy(gsiAns.szServerIP, req.ip.c_str(), sizeof(gsiAns.szServerIP) - 1);
        gsiAns.nServicePort = req.port;

        if (req.serverType == SERVER_SESSION) {
            gsiAns.nSessionSvrID = res.sessionSvrId;
        } else {
            gsiAns.nSessionSvrID = 0;
        }

        // Combine certified fields: ServiceProvider, ServerType, IP, Port, NewUniqKey, Certification
        std::string certPlain = std::to_string(req.country) + "," +
                                std::to_string(req.serverType) + "," +
                                req.ip + "," +
                                std::to_string(req.port) + "," +
                                res.newUniqKey + "," +
                                std::to_string(res.certification);

        std::string encryptedHex = EncryptAuthData(certPlain);
        std::strncpy(gsiAns.szAuthData, encryptedHex.c_str(), sizeof(gsiAns.szAuthData) - 1);

        // Reply with NET_MSG_AUTH_CERTIFICATION_ANS (205)
        session->Send(std::make_shared<std::vector<char>>(
            sc::net::EncodeMessage(NET_MSG_AUTH_CERTIFICATION_ANS, &gsiAns, sizeof(G_AUTH_INFO))
        ));
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
