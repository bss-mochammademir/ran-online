// authserver_smoke — in-process end-to-end proof of the AuthServer fan-out skeleton
// on Linux: the asio lifecycle base (NetServer, IOCP->asio) starts, listens, accepts
// a real TCP client, runs the framed packet pipeline (Session -> MessageFramer ->
// MsgManager double-buffer -> UpdateProc dispatch -> framed reply), optionally
// connects the DB layer (OdbcDb), and shuts down cleanly. A live SQL Server is needed
// only for the optional DB assertion (SA_PASSWORD/DB_SERVER env).
#include "auth_server.h"
#include "packet.h"

#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

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

    const unsigned short port = 18901;
    std::string dbConn;
    const char* pw = std::getenv("SA_PASSWORD");
    const char* sv = std::getenv("DB_SERVER");
    const char* db = std::getenv("DB_NAME");
    if (pw && sv) {
        dbConn = std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + sv +
                 ";UID=sa;PWD=" + pw + ";DATABASE=" + (db ? db : "RanUser") +
                 ";Encrypt=yes;TrustServerCertificate=yes;";
    }

    std::cout << "AuthServer fan-out smoke — lifecycle + framed packet pipeline + OdbcDb.\n";
    sc::servers::AuthServer srv(port, dbConn);

    check("Start() returns 0", srv.Start() == 0);
    check("IsRunning() after Start", srv.IsRunning());
    if (!dbConn.empty()) check("DB layer connected (OdbcDb)", srv.DbReady());
    else std::cout << "  SKIP DB layer assertion (no SA_PASSWORD/DB_SERVER env)\n";

    // Client: connect, send 3 framed NET_MSG_GENERIC messages (one split across two
    // writes to exercise server-side reassembly), then read the echoed frames back.
    std::vector<Message> echoes;
    bool connected = false;
    {
        asio::io_context cio;
        tcp::socket s(cio);
        boost::system::error_code ec;
        s.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
        connected = !ec;
        if (connected) {
            auto m1 = EncodeMessage(101, "alpha", 5);
            auto m2 = EncodeMessage(202, "world", 5);
            auto m3 = EncodeMessage(303, nullptr, 0);   // header-only frame (dwSize == 8)
            asio::write(s, asio::buffer(m1), ec);
            asio::write(s, asio::buffer(m2.data(), 5), ec);                  // split: part 1
            asio::write(s, asio::buffer(m2.data() + 5, m2.size() - 5), ec);  // split: part 2
            asio::write(s, asio::buffer(m3), ec);

            MessageFramer cfr;
            auto buf = std::make_shared<std::array<char, 512>>();
            std::function<void()> rd = [&]() {
                s.async_read_some(asio::buffer(*buf),
                    [&](const boost::system::error_code& e, std::size_t n) {
                        if (e) return;
                        cfr.Feed(buf->data(), n, echoes);
                        if (echoes.size() < 3) rd();
                    });
            };
            rd();
            cio.run_for(std::chrono::milliseconds(1000));  // bounded wait for echoes
        }
    }

    check("client connected to acceptor", connected);
    check("OnAccept fired", srv.Accepted() >= 1, "accepted=" + std::to_string(srv.Accepted()));
    check("3 frames echoed back", echoes.size() == 3, "echoes=" + std::to_string(echoes.size()));
    if (echoes.size() == 3) {
        check("echo#1 type+payload", echoes[0].type == 101 &&
              std::string(echoes[0].payload(), echoes[0].payloadSize()) == "alpha");
        check("echo#2 type+payload (split msg)", echoes[1].type == 202 &&
              std::string(echoes[1].payload(), echoes[1].payloadSize()) == "world");
        check("echo#3 header-only frame", echoes[2].type == 303 && echoes[2].payloadSize() == 0);
    }
    check("server dispatched >= 3 (MsgProcess)", srv.Processed() >= 3,
          "processed=" + std::to_string(srv.Processed()));
    check("OnUpdate ticked (timer)", srv.Updates() >= 1, "updates=" + std::to_string(srv.Updates()));

    check("Stop() returns 0", srv.Stop() == 0);
    check("IsRunning() false after Stop", !srv.IsRunning());

    std::cout << "\n";
    if (g_fail == 0) {
        std::cout << "AUTHSERVER FAN-OUT SLICE OK: asio lifecycle + framed recv pipeline "
                     "(Session -> framer -> MsgManager -> dispatch -> echo)"
                  << (dbConn.empty() ? "" : " + OdbcDb connect") << ".\n";
        return 0;
    }
    std::cout << "AUTHSERVER FAN-OUT SLICE FAILED: " << g_fail << " assertion(s).\n";
    return 1;
}
