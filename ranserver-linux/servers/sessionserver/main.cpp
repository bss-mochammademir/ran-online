#include "session_server.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace {
std::atomic<bool>       g_stop{false};
std::condition_variable g_cv;
std::mutex              g_mx;
void onSignal(int) { g_stop.store(true); g_cv.notify_all(); }

std::string dbConnFromEnv() {
    const char* pw = std::getenv("SA_PASSWORD");
    const char* sv = std::getenv("DB_SERVER");
    const char* db = std::getenv("DB_NAME");
    if (!pw || !sv) return {};
    return std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + sv +
           ";UID=sa;PWD=" + pw + ";DATABASE=" + (db ? db : "RanUser") +
           ";Encrypt=yes;TrustServerCertificate=yes;";
}
} // namespace

int main(int argc, char** argv) {
    unsigned short port = 0;
    std::string dbConn;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = static_cast<unsigned short>(std::atoi(argv[++i]));
        else if (a == "--db" && i + 1 < argc) dbConn = argv[++i];
    }
    if (port == 0) {
        const char* p = std::getenv("SESSION_PORT");
        port = p ? static_cast<unsigned short>(std::atoi(p)) : 9401;
    }
    if (dbConn.empty()) dbConn = dbConnFromEnv();

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    sc::servers::SessionServer srv(port, dbConn);
    if (srv.Start() != 0) { std::cerr << "SessionServer failed to start\n"; return 1; }
    std::cerr << "SessionServer running on port " << port << " (SIGINT/SIGTERM to stop)\n";

    {
        std::unique_lock<std::mutex> lk(g_mx);
        g_cv.wait(lk, [] { return g_stop.load(); });
    }

    srv.Stop();
    return 0;
}
