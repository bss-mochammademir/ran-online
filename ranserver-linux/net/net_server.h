#ifndef SC_NET_NET_SERVER_H
#define SC_NET_NET_SERVER_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <boost/asio.hpp>

namespace sc { namespace net {

// NetServer — cross-platform (Linux) port of the lifecycle/threading contract of
// the Windows IOCP server base `NetServer` (RanLogicServer/Server/NetServer.{h,cpp}),
// which every game server (Auth/Login/Agent/Session/Field) inherits from.
//
// The Win32 original is built on IOCP + raw worker/accept/update/schedule threads;
// this port reimplements the same control structure on boost::asio (validated by
// Spike #2). Mapping:
//
//   IOCP (CreateIoCompletionPort) + m_hWorkerThread[] running WorkProc
//        -> boost::asio::io_context + a pool of threads running io_context.run()
//   accept thread (m_hAcceptThread) + SOCKET m_sServer + ListenProc
//        -> tcp::acceptor + async_accept loop  -> OnAccept(socket)
//   update thread (m_hUpdateThread) + UpdateProc at FPS (setUpdateFrame)
//        -> steady_timer periodic            -> OnUpdate()
//   regular-schedule thread + RegularScheduleProc
//        -> steady_timer periodic            -> OnRegularSchedule()
//   HWND EditBox console
//        -> Log() to stderr (headless; the GUI dialog is replaced by main()+signals)
//
// This is the FAN-OUT TEMPLATE: a concrete server subclasses it (see AuthServer)
// and overrides the hooks. The packet/protocol layer (PER_IO_OPERATION_DATA pools,
// MsgManager double-buffer, ZLIB) is intentionally a follow-on chip — the base
// hands OnAccept() the connected socket so that layer can be slotted in later.
class NetServer {
public:
    NetServer(std::string name, unsigned short port,
              int workerThreads = 0,          // 0 -> hardware_concurrency()
              int updateIntervalMs = 50,      // ~20 Hz update tick (UpdateProc)
              int scheduleIntervalMs = 120000 // 2 min maintenance (RegularScheduleProc)
    );
    virtual ~NetServer();

    NetServer(const NetServer&) = delete;
    NetServer& operator=(const NetServer&) = delete;

    // Start: OnStart() (DB/config) -> open acceptor -> arm timers -> spin worker pool.
    // Returns 0 on success, non-zero on failure (server left stopped). Mirrors the
    // Win32 Start() orchestration (StartCfg/StartIOCP/StartWorkThread/StartListen...).
    int  Start();
    // Stop: graceful shutdown — stop accepting, cancel timers, drain & join workers,
    // then OnStop(). Idempotent. Mirrors Win32 Stop()/Stop*Thread()/StopIOCP().
    int  Stop();

    bool IsRunning() const { return m_running.load(); }
    unsigned short Port() const { return m_port; }
    const std::string& Name() const { return m_name; }

protected:
    // ---- hooks a concrete server overrides (mirror NetServer's pure virtuals) ----
    virtual int  OnStart()  { return 0; }   // connect DB (StartDbManager), load config
    virtual void OnStop()   {}              // teardown
    virtual void OnAccept(boost::asio::ip::tcp::socket sock) = 0;  // ListenProc accept
    virtual void OnUpdate() {}              // UpdateProc tick
    virtual void OnRegularSchedule() {}     // RegularScheduleProc tick

    void Log(const std::string& msg) const; // replaces the HWND EditBox console
    boost::asio::io_context& Io() { return m_io; }

private:
    void doAccept();        // async_accept loop (ListenProc)
    void armUpdateTimer();
    void armScheduleTimer();

    std::string    m_name;
    unsigned short m_port;
    int            m_workerCount;
    int            m_updateMs;
    int            m_scheduleMs;

    boost::asio::io_context m_io;
    using work_guard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    std::unique_ptr<work_guard>    m_work;        // keep run() alive while idle
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::steady_timer      m_updateTimer;
    boost::asio::steady_timer      m_scheduleTimer;
    std::vector<std::thread>       m_workers;      // <- m_hWorkerThread[] (IOCP workers)
    std::atomic<bool>              m_running{false};
};

}} // namespace sc::net

#endif // SC_NET_NET_SERVER_H
