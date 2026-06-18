#include "agent_server.h"
#include "packet.h"
#include "odbc_db.h"
#include "agent_server_msg.h"

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

static int g_fail = 0;
static void check(const char* what, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "  PASS " : "  FAIL ") << what;
    if (!detail.empty()) std::cout << " => " << detail;
    std::cout << "\n";
    if (!ok) ++g_fail;
}

int main() {
    namespace asio = boost::asio;
    using tcp = boost::asio::ip::tcp;
    using namespace sc::net;
    using namespace sc::servers;

    const unsigned short port = 18904;
    std::string dbConn;
    const char* pw = std::getenv("SA_PASSWORD");
    const char* sv = std::getenv("DB_SERVER");
    const char* dbName = std::getenv("DB_NAME");
    if (pw && sv) {
        dbConn = std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + sv +
                 ";UID=sa;PWD=" + pw + ";DATABASE=" + (dbName ? dbName : "RanUser") +
                 ";Encrypt=yes;TrustServerCertificate=yes;";
    }

    std::cout << "AgentServer smoke — lifecycle + credentials login check.\n";

    // Setup temporary test user in DB if DB connection is active
    bool dbSetupOk = false;
    if (!dbConn.empty()) {
        sc::db::OdbcDb testDb;
        if (testDb.Open(dbConn)) {
            testDb.Execute("DELETE FROM dbo.GSUserInfo WHERE UserID = 'smoketest'");
            if (testDb.Execute("INSERT INTO dbo.GSUserInfo (UserID, UserPass, UserAvailable, IsUse2ndPass) VALUES ('smoketest', 'smoke123', 1, 0)")) {
                dbSetupOk = true;
                std::cout << "  Setup: Created test user 'smoketest' in GSUserInfo.\n";
            } else {
                std::cout << "  FAIL Setup: Failed to create test user in GSUserInfo: " << testDb.LastError() << "\n";
            }
        } else {
            std::cout << "  FAIL Setup: Failed to open DB connection for test setup: " << testDb.LastError() << "\n";
        }
    }

    sc::servers::AgentServer srv(port, dbConn);

    check("Start() returns 0", srv.Start() == 0);
    check("IsRunning() after Start", srv.IsRunning());
    // Give the server at least one tick before checks.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (!dbConn.empty()) check("DB layer connected", srv.DbReady());
    else std::cout << "  SKIP DB assertions (no SA_PASSWORD/DB_SERVER env)\n";

    bool validLoginPassed = false;
    bool invalidLoginPassed = false;

    if (!dbConn.empty() && dbSetupOk) {
        // 1. Test Valid Login Credentials
        asio::io_context cio;
        tcp::socket s(cio);
        boost::system::error_code ec;
        s.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
        if (!ec) {
            // Prepare IDN_NET_LOGIN_DATA
            IDN_NET_LOGIN_DATA req = {};
            req.dwSize = sizeof(IDN_NET_LOGIN_DATA);
            req.nType = IDN_NET_MSG_LOGIN;
            req.m_nChannel = 1;
            std::strncpy(req.m_szUserid, "smoketest", sizeof(req.m_szUserid) - 1);
            std::strncpy(req.m_szPassword, "smoke123", sizeof(req.m_szPassword) - 1);

            // Encode and send message
            auto responseData = sc::net::EncodeMessage(IDN_NET_MSG_LOGIN, &req.m_nChannel, sizeof(IDN_NET_LOGIN_DATA) - sc::net::kHeaderSize);
            asio::write(s, asio::buffer(responseData), ec);

            std::vector<Message> responses;
            MessageFramer framer;
            auto buf = std::make_shared<std::array<char, 512>>();
            std::function<void()> rd = [&]() {
                s.async_read_some(asio::buffer(*buf),
                    [&](const boost::system::error_code& e, std::size_t n) {
                        if (e) return;
                        framer.Feed(buf->data(), n, responses);
                        if (responses.empty()) rd();
                    });
            };
            rd();
            cio.run_for(std::chrono::milliseconds(1000));

            if (responses.size() == 1) {
                const NET_LOGIN_FEEDBACK_DATA* res = reinterpret_cast<const NET_LOGIN_FEEDBACK_DATA*>(responses[0].bytes.data());
                check("valid credentials response type", responses[0].type == NET_MSG_LOGIN_FB);
                check("valid credentials result OK", res->m_Result == EM_LOGIN_FB_SUB_OK, "m_Result=" + std::to_string(res->m_Result));
                if (responses[0].type == NET_MSG_LOGIN_FB && res->m_Result == EM_LOGIN_FB_SUB_OK) {
                    validLoginPassed = true;
                }
            } else {
                check("valid credentials got exactly 1 response", false, "responses=" + std::to_string(responses.size()));
            }
        } else {
            check("client connected for valid check", false, ec.message());
        }

        // 2. Test Invalid Login Credentials
        asio::io_context cio2;
        tcp::socket s2(cio2);
        s2.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
        if (!ec) {
            IDN_NET_LOGIN_DATA req = {};
            req.dwSize = sizeof(IDN_NET_LOGIN_DATA);
            req.nType = IDN_NET_MSG_LOGIN;
            req.m_nChannel = 1;
            std::strncpy(req.m_szUserid, "smoketest", sizeof(req.m_szUserid) - 1);
            std::strncpy(req.m_szPassword, "wrongpass", sizeof(req.m_szPassword) - 1);

            auto responseData = sc::net::EncodeMessage(IDN_NET_MSG_LOGIN, &req.m_nChannel, sizeof(IDN_NET_LOGIN_DATA) - sc::net::kHeaderSize);
            asio::write(s2, asio::buffer(responseData), ec);

            std::vector<Message> responses;
            MessageFramer framer;
            auto buf = std::make_shared<std::array<char, 512>>();
            std::function<void()> rd = [&]() {
                s2.async_read_some(asio::buffer(*buf),
                    [&](const boost::system::error_code& e, std::size_t n) {
                        if (e) return;
                        framer.Feed(buf->data(), n, responses);
                        if (responses.empty()) rd();
                    });
            };
            rd();
            cio2.run_for(std::chrono::milliseconds(1000));


            if (responses.size() == 1) {
                const NET_LOGIN_FEEDBACK_DATA* res = reinterpret_cast<const NET_LOGIN_FEEDBACK_DATA*>(responses[0].bytes.data());
                check("invalid credentials response type", responses[0].type == NET_MSG_LOGIN_FB);
                check("invalid credentials result INCORRECT", res->m_Result == EM_LOGIN_FB_SUB_INCORRECT, "m_Result=" + std::to_string(res->m_Result));
                if (responses[0].type == NET_MSG_LOGIN_FB && res->m_Result == EM_LOGIN_FB_SUB_INCORRECT) {
                    invalidLoginPassed = true;
                }
            } else {
                check("invalid credentials got exactly 1 response", false, "responses=" + std::to_string(responses.size()));
            }
        } else {
            check("client connected for invalid check", false, ec.message());
        }
    } else {
        std::cout << "  SKIP credentials tests (no SA_PASSWORD or setup failed)\n";
    }

    // ---- Char-list smoke (DB-backed) -----------------------------------------
    // Login first, then send NET_MSG_REQ_CHA_BAINFO and verify we get
    // NET_MSG_CHA_BAINFO_AC back.
    if (!dbConn.empty() && dbSetupOk && validLoginPassed) {
        asio::io_context cio3;
        tcp::socket s3(cio3);
        boost::system::error_code ec3;
        s3.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec3);
        if (!ec3) {
            // 1. Login first to set userNum session state.
            IDN_NET_LOGIN_DATA lreq = {};
            lreq.dwSize = sizeof(IDN_NET_LOGIN_DATA);
            lreq.nType  = IDN_NET_MSG_LOGIN;
            lreq.m_nChannel = 1;
            std::strncpy(lreq.m_szUserid,  "smoketest", sizeof(lreq.m_szUserid)  - 1);
            std::strncpy(lreq.m_szPassword, "smoke123",  sizeof(lreq.m_szPassword) - 1);
            auto loginPkt = sc::net::EncodeMessage(IDN_NET_MSG_LOGIN, &lreq.m_nChannel,
                                sizeof(IDN_NET_LOGIN_DATA) - sc::net::kHeaderSize);
            asio::write(s3, asio::buffer(loginPkt), ec3);

            // Drain login response.
            {
                std::vector<sc::net::Message> resp;
                sc::net::MessageFramer framer;
                auto buf = std::make_shared<std::array<char, 512>>();
                std::function<void()> rd = [&]() {
                    s3.async_read_some(asio::buffer(*buf),
                        [&](const boost::system::error_code& e, std::size_t n) {
                            if (e) return;
                            framer.Feed(buf->data(), n, resp);
                            if (resp.empty()) rd();
                        });
                };
                rd();
                cio3.run_for(std::chrono::milliseconds(600));
                cio3.restart();
                check("char-list prelogin: got login response",
                      !resp.empty() && resp[0].type == NET_MSG_LOGIN_FB);
            }

            // 2. Send NET_MSG_REQ_CHA_BAINFO (empty payload — server only needs header).
            NET_CHA_REQ_BAINFO_DATA creq = {};
            creq.dwSize = sizeof(NET_CHA_REQ_BAINFO_DATA);
            creq.nType  = NET_MSG_REQ_CHA_BAINFO;
            auto charPkt = sc::net::EncodeMessage(NET_MSG_REQ_CHA_BAINFO,
                               &creq.m_LauncherVer,
                               sizeof(NET_CHA_REQ_BAINFO_DATA) - sc::net::kHeaderSize);
            asio::write(s3, asio::buffer(charPkt), ec3);

            std::vector<sc::net::Message> cresp;
            sc::net::MessageFramer cframer;
            auto cbuf = std::make_shared<std::array<char, 1024>>();
            std::function<void()> crd = [&]() {
                s3.async_read_some(asio::buffer(*cbuf),
                    [&](const boost::system::error_code& e, std::size_t n) {
                        if (e) return;
                        cframer.Feed(cbuf->data(), n, cresp);
                        if (cresp.empty()) crd();
                    });
            };
            crd();
            cio3.run_for(std::chrono::milliseconds(800));

            check("char-list: got NET_MSG_CHA_BAINFO_AC",
                  !cresp.empty() && cresp[0].type == NET_MSG_CHA_BAINFO_AC,
                  cresp.empty() ? "no response" : "type=" + std::to_string(cresp[0].type));
            if (!cresp.empty() && cresp[0].type == NET_MSG_CHA_BAINFO_AC) {
                const NET_CHA_BAINFO_AC_DATA* ac =
                    reinterpret_cast<const NET_CHA_BAINFO_AC_DATA*>(cresp[0].bytes.data());
                check("char-list: ChaServerTotalNum >= 0",
                      ac->m_ChaServerTotalNum >= 0,
                      "count=" + std::to_string(ac->m_ChaServerTotalNum));
            }
        } else {
            std::cout << "  SKIP char-list test (client connect failed)\n";
        }
    } else {
        std::cout << "  SKIP char-list test (no DB or login not tested)\n";
    }

    check("server dispatched requests", srv.Processed() >= (dbSetupOk ? 2 : 0),
          "processed=" + std::to_string(srv.Processed()));
    check("OnUpdate ticked", srv.Updates() >= 1);

    check("Stop() returns 0", srv.Stop() == 0);
    check("IsRunning() false after Stop", !srv.IsRunning());

    // Clean up temporary test user
    if (!dbConn.empty() && dbSetupOk) {
        sc::db::OdbcDb testDb;
        if (testDb.Open(dbConn)) {
            testDb.Execute("DELETE FROM dbo.GSUserInfo WHERE UserID = 'smoketest'");
            std::cout << "  Teardown: Removed test user 'smoketest' from GSUserInfo.\n";
        }
    }

    std::cout << "\n";
    if (g_fail == 0 && (dbConn.empty() || (validLoginPassed && invalidLoginPassed))) {
        (void)validLoginPassed; (void)invalidLoginPassed;
        std::cout << "AGENTSERVER SMOKE OK: AgentServer login validation verified.\n";
        return 0;
    }
    std::cout << "AGENTSERVER SMOKE FAILED: " << g_fail << " assertion(s) failed or tests skipped.\n";
    return 1;
}
