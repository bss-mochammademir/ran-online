#ifndef SC_SERVERS_FIELD_SERVER_H
#define SC_SERVERS_FIELD_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "net_server.h"
#include "msg_manager.h"
#include "session.h"
#include "odbc_db.h"

namespace sc { namespace servers {

// FieldServer — fan-out server #5: Linux skeleton of s_CFieldServer + GLGaeaServer
// (RanLogicServer/Server/s_CFieldServer.{h,cpp} + FieldServer/GLGaeaServer.{h,cpp}).
// Gameplay engine: combat, AI, map management, inventory, dungeon instances.
// Agent Server sends client packets here; computed results (HP changes, drops, etc.)
// are routed back via Agent Server.
//
// Full features deferred: Gaea Engine (GLGaeaServer — combat/AI/map simulation),
// CacheServer slot (async DB writes), instance maps (GLGaeaServerInstantMap),
// memory auto-restart guard (m_dwMemoryCheckTime / ForceStop),
// packet handlers (GLGaeaServerMsg), config XML.
//
// DB note: FieldServer's primary DB is RanGame (character game data: inventory,
// skills, quests). Writes go through CacheServer for async I/O; this skeleton
// opens RanGame directly for the lifecycle probe.
class FieldServer : public sc::net::NetServer {
public:
    FieldServer(unsigned short port, std::string dbConn);

    long Accepted()  const { return m_accepted.load(); }
    long Updates()   const { return m_updates.load(); }
    long Processed() const { return m_processed.load(); }
    bool DbReady()   const { return m_dbReady.load(); }

protected:
    int  OnStart() override;
    void OnStop()  override;
    void OnAccept(boost::asio::ip::tcp::socket sock) override;
    void OnUpdate() override;
    void OnRegularSchedule() override;

private:
    void dispatch(const sc::net::Message& msg);

    std::string       m_dbConn;
    sc::db::OdbcDb    m_db;
    std::atomic<bool> m_dbReady{false};
    std::atomic<long> m_accepted{0};
    std::atomic<long> m_updates{0};
    std::atomic<long> m_processed{0};

    sc::net::MsgManager m_recvMsgs;
    std::atomic<uint32_t> m_nextClientId{0};
    std::mutex m_sessMx;
    std::unordered_map<uint32_t, std::shared_ptr<sc::net::Session>> m_sessions;
};

}} // namespace sc::servers

#endif // SC_SERVERS_FIELD_SERVER_H
