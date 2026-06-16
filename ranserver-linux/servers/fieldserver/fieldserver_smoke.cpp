#include "field_server.h"
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

    const unsigned short port = 18905;
    std::string dbConn;
    const char* pw = std::getenv("SA_PASSWORD");
    const char* sv = std::getenv("DB_SERVER");
    const char* db = std::getenv("DB_NAME");
    if (pw && sv) {
        // FieldServer uses RanGameS1 (character data) — the original production DB name;
        // the .bak ships logical name RanGameS1, and SPs in RanShop/RanLog cross-reference
        // RanGameS1.dbo.* by three-part name, so the restored DB MUST keep the S1 suffix.
        dbConn = std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + sv +
                 ";UID=sa;PWD=" + pw + ";DATABASE=" + (db ? db : "RanGameS1") +
                 ";Encrypt=yes;TrustServerCertificate=yes;";
    }

    std::cout << "FieldServer smoke — lifecycle + framed packet pipeline.\n";
    sc::servers::FieldServer srv(port, dbConn);

    check("Start() returns 0", srv.Start() == 0);
    check("IsRunning() after Start", srv.IsRunning());
    if (!dbConn.empty()) check("DB layer connected (RanGameS1)", srv.DbReady());
    else std::cout << "  SKIP DB layer assertion (no SA_PASSWORD/DB_SERVER env)\n";

    // simulate an AgentServer sending 3 framed messages to FieldServer
    std::vector<Message> echoes;
    bool connected = false;
    {
        asio::io_context cio;
        tcp::socket s(cio);
        boost::system::error_code ec;
        s.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
        connected = !ec;
        if (connected) {
            auto m1 = EncodeMessage(101, "move", 4);
            auto m2 = EncodeMessage(202, "attackhit", 9);
            auto m3 = EncodeMessage(303, nullptr, 0);
            asio::write(s, asio::buffer(m1), ec);
            asio::write(s, asio::buffer(m2.data(), 5), ec);
            asio::write(s, asio::buffer(m2.data() + 5, m2.size() - 5), ec);
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
            cio.run_for(std::chrono::milliseconds(1000));
        }
    }

    check("AgentServer connection accepted", connected);
    check("OnAccept fired", srv.Accepted() >= 1);
    check("3 frames echoed back", echoes.size() == 3, "echoes=" + std::to_string(echoes.size()));
    if (echoes.size() == 3) {
        check("echo#1 type+payload", echoes[0].type == 101 &&
              std::string(echoes[0].payload(), echoes[0].payloadSize()) == "move");
        check("echo#2 type+payload (split)", echoes[1].type == 202 &&
              std::string(echoes[1].payload(), echoes[1].payloadSize()) == "attackhit");
        check("echo#3 header-only", echoes[2].type == 303 && echoes[2].payloadSize() == 0);
    }
    check("server dispatched >= 3", srv.Processed() >= 3,
          "processed=" + std::to_string(srv.Processed()));
    check("OnUpdate ticked", srv.Updates() >= 1);

    check("Stop() returns 0", srv.Stop() == 0);
    check("IsRunning() false after Stop", !srv.IsRunning());

    std::cout << "\n";
    if (g_fail == 0) {
        std::cout << "FIELDSERVER SMOKE OK: asio lifecycle + framed packet pipeline"
                  << (dbConn.empty() ? "" : " + OdbcDb connect (RanGameS1)") << ".\n";
        return 0;
    }
    std::cout << "FIELDSERVER SMOKE FAILED: " << g_fail << " assertion(s).\n";
    return 1;
}
