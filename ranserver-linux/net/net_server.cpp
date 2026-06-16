#include "net_server.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>

namespace sc { namespace net {

namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

NetServer::NetServer(std::string name, unsigned short port,
                     int workerThreads, int updateIntervalMs, int scheduleIntervalMs)
    : m_name(std::move(name)),
      m_port(port),
      m_workerCount(workerThreads),
      m_updateMs(updateIntervalMs),
      m_scheduleMs(scheduleIntervalMs),
      m_acceptor(m_io),
      m_updateTimer(m_io),
      m_scheduleTimer(m_io) {}

NetServer::~NetServer() { Stop(); }

void NetServer::Log(const std::string& msg) const {
    std::time_t t = std::time(nullptr);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));
    std::cerr << "[" << ts << "] [" << m_name << "] " << msg << "\n";
}

int NetServer::Start() {
    if (m_running.exchange(true)) return 0;  // already running

    // (1) server-specific startup: DB connection, config load (StartDbManager etc.)
    int rc = OnStart();
    if (rc != 0) { Log("OnStart failed (rc=" + std::to_string(rc) + ")"); m_running.store(false); return rc; }

    // (2) open the listen socket (StartListenThread + IOCP listen socket)
    boost::system::error_code ec;
    tcp::endpoint ep(tcp::v4(), m_port);
    m_acceptor.open(ep.protocol(), ec);
    if (!ec) m_acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    if (!ec) m_acceptor.bind(ep, ec);
    if (!ec) m_acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        Log("listen setup failed on port " + std::to_string(m_port) + ": " + ec.message());
        m_acceptor.close(ec);
        m_running.store(false);
        OnStop();
        return -1;
    }

    // (3) keep io_context.run() alive even when momentarily idle (work guard).
    m_work = std::make_unique<work_guard>(asio::make_work_guard(m_io));

    // (4) post the first async accept + arm the periodic timers.
    doAccept();
    armUpdateTimer();
    armScheduleTimer();

    // (5) spin the worker pool — these run the io_context (IOCP worker threads).
    int n = m_workerCount > 0 ? m_workerCount
                              : std::max(1u, std::thread::hardware_concurrency());
    m_workers.reserve(n);
    for (int i = 0; i < n; ++i) m_workers.emplace_back([this] { m_io.run(); });

    Log("started on port " + std::to_string(m_port) + " with " + std::to_string(n) + " worker(s)");
    return 0;
}

int NetServer::Stop() {
    if (!m_running.exchange(false)) return 0;  // already stopped / not started

    boost::system::error_code ec;
    m_acceptor.close(ec);          // stop accepting (cancels async_accept)
    m_updateTimer.cancel();
    m_scheduleTimer.cancel();
    m_work.reset();                // let run() return once outstanding work drains
    m_io.stop();                   // stop promptly

    for (auto& t : m_workers) if (t.joinable()) t.join();
    m_workers.clear();

    OnStop();
    Log("stopped");

    m_io.restart();                // ready for a possible subsequent Start()
    return 0;
}

void NetServer::doAccept() {
    m_acceptor.async_accept([this](const boost::system::error_code& ec, tcp::socket sock) {
        if (ec) {
            // operation_aborted is the expected result of Stop() closing the acceptor.
            if (ec != asio::error::operation_aborted && m_running.load())
                Log("accept error: " + ec.message());
            return;
        }
        OnAccept(std::move(sock));         // hand the connection to the concrete server
        if (m_running.load()) doAccept();  // keep accepting (ListenProc loop)
    });
}

void NetServer::armUpdateTimer() {
    m_updateTimer.expires_after(std::chrono::milliseconds(m_updateMs));
    m_updateTimer.async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;  // cancelled by Stop()
        OnUpdate();
        if (m_running.load()) armUpdateTimer();
    });
}

void NetServer::armScheduleTimer() {
    m_scheduleTimer.expires_after(std::chrono::milliseconds(m_scheduleMs));
    m_scheduleTimer.async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;  // cancelled by Stop()
        OnRegularSchedule();
        if (m_running.load()) armScheduleTimer();
    });
}

}} // namespace sc::net
