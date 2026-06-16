// authserver_smoke — in-process proof that the AuthServer fan-out skeleton works
// end-to-end on Linux: the asio lifecycle base (NetServer, IOCP->asio) starts,
// listens, accepts a real TCP client, ticks its update timer, optionally connects
// the DB layer (OdbcDb), and shuts down cleanly. Needs a live SQL Server only for
// the optional DB assertion (SA_PASSWORD/DB_SERVER env); the lifecycle runs without.
#include "auth_server.h"

#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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

    std::cout << "AuthServer fan-out smoke — NetServer(IOCP->asio) lifecycle + OdbcDb wiring.\n";
    sc::servers::AuthServer srv(port, dbConn);

    check("Start() returns 0", srv.Start() == 0);
    check("IsRunning() after Start", srv.IsRunning());
    if (!dbConn.empty()) check("DB layer connected (OdbcDb)", srv.DbReady());
    else std::cout << "  SKIP DB layer assertion (no SA_PASSWORD/DB_SERVER env)\n";

    // Connect a real client and read the banner -> proves acceptor + OnAccept loop.
    std::string banner;
    bool connected = false;
    try {
        asio::io_context cio;
        tcp::socket s(cio);
        s.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
        connected = true;
        char buf[64];
        boost::system::error_code ec;
        std::size_t n = s.read_some(asio::buffer(buf), ec);
        if (n) banner.assign(buf, n);
    } catch (const std::exception& e) {
        std::cout << "  (client error: " << e.what() << ")\n";
    }
    check("client connected to acceptor", connected);
    check("OnAccept fired", srv.Accepted() >= 1, "accepted=" + std::to_string(srv.Accepted()));
    check("client received banner", banner.rfind("AUTHSERVER", 0) == 0, banner);

    // Let the periodic update timer tick (UpdateProc).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check("OnUpdate ticked (timer)", srv.Updates() >= 1, "updates=" + std::to_string(srv.Updates()));

    check("Stop() returns 0", srv.Stop() == 0);
    check("IsRunning() false after Stop", !srv.IsRunning());

    std::cout << "\n";
    if (g_fail == 0) {
        std::cout << "AUTHSERVER FAN-OUT SLICE OK: lifecycle (start/accept/update/stop) on "
                     "boost::asio + per-server entry pattern"
                  << (dbConn.empty() ? "" : " + OdbcDb connect") << ".\n";
        return 0;
    }
    std::cout << "AUTHSERVER FAN-OUT SLICE FAILED: " << g_fail << " assertion(s).\n";
    return 1;
}
