#include "agent_server.h"

#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace sc { namespace servers {

using tcp = boost::asio::ip::tcp;

AgentServer::AgentServer(unsigned short port, std::string dbConn)
    : sc::net::NetServer("AgentServer", port),
      m_dbConn(std::move(dbConn)) {}

int AgentServer::OnStart() {
    if (m_dbConn.empty()) {
        Log("no DB connection string -> running lifecycle-only (DB layer skipped)");
        return 0;
    }
    // AgentServer opens the User DB for account login validation.
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
    // stub: production also calls FieldConnectAll() to connect to all Field Servers
    Log("FieldConnectAll stub: Field Server connections deferred");
    return 0;
}

void AgentServer::OnStop() {
    if (m_db.IsOpened()) m_db.Close();
    Log("DB + Field Server connections released");
}

void AgentServer::OnAccept(tcp::socket sock) {
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

void AgentServer::OnUpdate() {
    ++m_updates;
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

namespace {
uint64_t ParseSqlDatetime(const std::string& str) {
    if (str.empty()) return 0;
    std::tm t = {};
    std::istringstream ss(str);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    std::time_t epoch = std::mktime(&t);
    return (epoch == -1) ? 0 : static_cast<uint64_t>(epoch);
}
}

bool AgentServer::ProcessUserLogin(const std::string& username, const std::string& password, const std::string& ip,
                                  int& userNum, int& userType, uint16_t& chaRemain, uint16_t& chaTestRemain,
                                  std::string& premiumDate, std::string& chatBlockDate, std::string& lastLoginDate,
                                  int& errCode) {
    std::lock_guard<std::mutex> lock(m_dbMx);
    if (!m_db.IsOpened()) {
        errCode = EM_LOGIN_FB_SUB_SYSTEM;
        return false;
    }

    // Call stored procedure dbo.gs_user_verify
    m_db.ClearParams();
    m_db.AppendRParamInteger(); // RETURN_VALUE
    m_db.AppendIParamVarchar("@userId", username, 20);
    m_db.AppendIParamVarchar("@userPass", password, 20);
    m_db.AppendIParamVarchar("@userIp", ip, 20);
    m_db.AppendIParamInteger("@SvrGrpNum", 1);
    m_db.AppendIParamInteger("@SvrNum", 0);
    m_db.AppendOParamInteger("@nReturn");

    if (!m_db.ExecuteStoredProcedure("dbo.gs_user_verify")) {
        Log("ProcessUserLogin: gs_user_verify execution failed: " + m_db.LastError());
        errCode = EM_LOGIN_FB_SUB_SYSTEM;
        return false;
    }

    int nResult = 0;
    if (!m_db.GetParam("RETURN_VALUE", nResult)) {
        Log("ProcessUserLogin: failed to get RETURN_VALUE: " + m_db.LastError());
        errCode = EM_LOGIN_FB_SUB_SYSTEM;
        return false;
    }

    if (nResult != 2 && nResult != 3) {
        // Map SP return code to EM_LOGIN_FB_SUB
        if (nResult == 0) errCode = EM_LOGIN_FB_SUB_INCORRECT;
        else if (nResult == 4) errCode = EM_LOGIN_FB_SUB_IP_BAN;
        else if (nResult == 5) errCode = EM_LOGIN_FB_SUB_DUP;
        else if (nResult == 6) errCode = EM_LOGIN_FB_SUB_BLOCK;
        else errCode = EM_LOGIN_FB_SUB_FAIL;
        return false;
    }

    // Login SP succeeded -> Retrieve user info from GSUserInfo
    // Safely construct simple alphanumeric query
    std::string cleanUser = "";
    for (char c : username) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') cleanUser += c;
    }

    std::string query = "SELECT UserNum, UserType, ChaRemain, ChaTestRemain, PremiumDate, ChatBlockDate, LastLoginDate FROM dbo.GSUserInfo WHERE UserID = '" + cleanUser + "'";
    if (!m_db.Execute(query)) {
        Log("ProcessUserLogin: GSUserInfo query failed: " + m_db.LastError());
        errCode = EM_LOGIN_FB_SUB_SYSTEM;
        return false;
    }

    if (m_db.GetEOF()) {
        Log("ProcessUserLogin: GSUserInfo query returned EOF for user: " + cleanUser);
        errCode = EM_LOGIN_FB_SUB_INCORRECT;
        return false;
    }

    m_db.GetCollect("UserNum", userNum);
    m_db.GetCollect("UserType", userType);
    
    int tempCha = 0;
    m_db.GetCollect("ChaRemain", tempCha);
    chaRemain = static_cast<uint16_t>(tempCha);

    int tempTest = 0;
    m_db.GetCollect("ChaTestRemain", tempTest);
    chaTestRemain = static_cast<uint16_t>(tempTest);

    m_db.GetCollect("PremiumDate", premiumDate);
    m_db.GetCollect("ChatBlockDate", chatBlockDate);
    m_db.GetCollect("LastLoginDate", lastLoginDate);

    errCode = EM_LOGIN_FB_SUB_OK;
    return true;
}

void AgentServer::dispatch(const sc::net::Message& msg) {
    ++m_processed;
    std::shared_ptr<sc::net::Session> session;
    {
        std::lock_guard<std::mutex> lk(m_sessMx);
        auto it = m_sessions.find(msg.clientId);
        if (it != m_sessions.end()) session = it->second;
    }
    if (!session) return;

    if (msg.type == IDN_NET_MSG_LOGIN) {
        constexpr std::size_t kPayloadSize = sizeof(IDN_NET_LOGIN_DATA) - sc::net::kHeaderSize;
        if (msg.payloadSize() < kPayloadSize) {
            Log("dispatch: IDN_NET_MSG_LOGIN wrong payload size: " +
                std::to_string(msg.payloadSize()) + " expected at least " +
                std::to_string(kPayloadSize));
            session->Close();
            return;
        }

        const IDN_NET_LOGIN_DATA* req = reinterpret_cast<const IDN_NET_LOGIN_DATA*>(msg.bytes.data());
        
        std::string username(req->m_szUserid, std::min<size_t>(std::strlen(req->m_szUserid), USR_ID_LENGTH));
        std::string password(req->m_szPassword, std::min<size_t>(std::strlen(req->m_szPassword), MD5_MAX_LENGTH));
        std::string ip = session->RemoteIp();

        // Validate username to prevent injection
        bool valid = true;
        if (username.empty() || username.size() > USR_ID_LENGTH) valid = false;
        for (char c : username) {
            if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') { valid = false; break; }
        }

        NET_LOGIN_FEEDBACK_DATA fb = {};
        fb.dwSize = sizeof(NET_LOGIN_FEEDBACK_DATA);
        fb.nType  = NET_MSG_LOGIN_FB;
        fb.m_Result = EM_LOGIN_FB_SUB_INCORRECT;

        if (valid) {
            int userNum = 0, userType = 0, errCode = 0;
            uint16_t chaRemain = 0, chaTestRemain = 0;
            std::string premiumDate, chatBlockDate, lastLoginDate;

            bool success = ProcessUserLogin(username, password, ip,
                                           userNum, userType, chaRemain, chaTestRemain,
                                           premiumDate, chatBlockDate, lastLoginDate,
                                           errCode);
            fb.m_Result = errCode;
            if (success) {
                fb.m_ChaRemain = chaRemain;
                fb.m_Country = 0;
            }
        }

        Log("Login request for account [" + username + "] from IP " + ip + " -> result=" + std::to_string(fb.m_Result));

        auto response = std::make_shared<std::vector<char>>(
            sc::net::EncodeMessage(NET_MSG_LOGIN_FB, &fb.m_Result, sizeof(NET_LOGIN_FEEDBACK_DATA) - sc::net::kHeaderSize)
        );
        session->Send(response);
        return;
    }

    // echo back until handlers are ported (field routing, anti-cheat)
    session->Send(std::make_shared<std::vector<char>>(
        sc::net::EncodeMessage(msg.type, msg.payload(), msg.payloadSize())));
}

void AgentServer::OnRegularSchedule() {
    // stub: in production re-checks Field Server connections and sends heartbeats
    Log("regular schedule tick (Field Server heartbeat stub)");
}

}} // namespace sc::servers
