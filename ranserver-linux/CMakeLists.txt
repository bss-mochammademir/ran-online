cmake_minimum_required(VERSION 3.16)
project(ranserver_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared-layer dependencies (validated by Spike #1 & #2)
find_package(Boost REQUIRED)            # boost::asio (header-only) include dirs
find_package(Threads REQUIRED)          # io_context worker threads
find_library(ODBC_LIB odbc REQUIRED)    # unixODBC -> msodbcsql18 at runtime

# Foundation smoke target — the template each server port will follow.
add_executable(foundation_smoke src/foundation_smoke.cpp)
target_include_directories(foundation_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/platform
    ${Boost_INCLUDE_DIRS})
target_link_libraries(foundation_smoke PRIVATE ${ODBC_LIB} Threads::Threads)

# DB layer — ODBC port of the core CjADO read path (Spike #1). Needs only ODBC.
add_library(ran_db STATIC db/odbc_db.cpp)
target_include_directories(ran_db PUBLIC ${CMAKE_SOURCE_DIR}/db)
target_link_libraries(ran_db PUBLIC ${ODBC_LIB})

add_executable(db_smoke db/db_smoke.cpp)
target_link_libraries(db_smoke PRIVATE ran_db)

# Write-path smoke: parameterized stored procedures (CjADO Append*Param port).
add_executable(db_param_smoke db/db_param_smoke.cpp)
target_link_libraries(db_param_smoke PRIVATE ran_db)

# LZO static library target compiled from the embedded XLib_lzo repository.
# We compile with -fwrapv because miniacc.h compile-time asserts check signed integer overflows (like 1 << 31).
add_library(ran_lzo STATIC
    ${CMAKE_SOURCE_DIR}/../XLib_lzo/src/lzo_init.c
    ${CMAKE_SOURCE_DIR}/../XLib_lzo/src/lzo1x_1.c
    ${CMAKE_SOURCE_DIR}/../XLib_lzo/src/lzo1x_d2.c)
target_compile_options(ran_lzo PRIVATE -fwrapv)
target_include_directories(ran_lzo PRIVATE ${CMAKE_SOURCE_DIR}/../XLib_lzo/include)

# Network layer — cross-platform server base + packet codec, IOCP -> boost::asio
# port of the shared Win32 NetServer (RanLogicServer/Server/NetServer) and the
# message framing/queue (SigmaCore/Net + RanLogic/s_NetGlobal). boost::asio is
# header-only; only Threads is linked.
add_library(ran_net STATIC
    net/net_server.cpp
    net/packet.cpp        # NET_MSG_GENERIC framing (RanLogic/s_NetGlobal.h)
    net/session.cpp       # per-connection async read loop
    net/minlzo.cpp        # LZO wrapper
    net/send_msg_buffer.cpp) # Output compression buffer
target_include_directories(ran_net PUBLIC ${CMAKE_SOURCE_DIR}/net ${Boost_INCLUDE_DIRS})
target_link_libraries(ran_net PUBLIC Threads::Threads ran_lzo)

# Packet codec unit smoke (no sockets): framing + MsgManager double-buffer.
add_executable(packet_smoke net/packet_smoke.cpp)
target_link_libraries(packet_smoke PRIVATE ran_net)

# LZO Smoke test
add_executable(lzo_smoke net/lzo_smoke.cpp)
target_link_libraries(lzo_smoke PRIVATE ran_net)


# ---- Fan-out: per-server targets, each riding the shared net/db layers ----------
# AuthServer (first fan-out server): Linux skeleton of CAuthServer on ran_net + ran_db.
add_library(authserver_lib STATIC
    servers/authserver/auth_server.cpp
    servers/authserver/auth_crypto.cpp)
target_include_directories(authserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/authserver)
target_link_libraries(authserver_lib PUBLIC ran_net ran_db)

add_executable(authserver servers/authserver/main.cpp)
target_link_libraries(authserver PRIVATE authserver_lib)

add_executable(authserver_smoke servers/authserver/authserver_smoke.cpp)
target_link_libraries(authserver_smoke PRIVATE authserver_lib)

# LoginServer — fan-out #2: CLoginServer skeleton (lobby/directory, channel list)
add_library(loginserver_lib STATIC servers/loginserver/login_server.cpp)
target_include_directories(loginserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/loginserver)
target_link_libraries(loginserver_lib PUBLIC ran_net ran_db)

add_executable(loginserver servers/loginserver/main.cpp)
target_link_libraries(loginserver PRIVATE loginserver_lib)

add_executable(loginserver_smoke servers/loginserver/loginserver_smoke.cpp)
target_link_libraries(loginserver_smoke PRIVATE loginserver_lib)

# SessionServer — fan-out #3: CSessionServer skeleton (global state hub, chat relay)
add_library(sessionserver_lib STATIC servers/sessionserver/session_server.cpp)
target_include_directories(sessionserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/sessionserver)
target_link_libraries(sessionserver_lib PUBLIC ran_net ran_db)

add_executable(sessionserver servers/sessionserver/main.cpp)
target_link_libraries(sessionserver PRIVATE sessionserver_lib)

add_executable(sessionserver_smoke servers/sessionserver/sessionserver_smoke.cpp)
target_link_libraries(sessionserver_smoke PRIVATE sessionserver_lib)

# AgentServer — fan-out #4: CAgentServer skeleton (client gateway, Field Server proxy)
add_library(agentserver_lib STATIC servers/agentserver/agent_server.cpp)
target_include_directories(agentserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/agentserver)
target_link_libraries(agentserver_lib PUBLIC ran_net ran_db)

add_executable(agentserver servers/agentserver/main.cpp)
target_link_libraries(agentserver PRIVATE agentserver_lib)

add_executable(agentserver_smoke servers/agentserver/agentserver_smoke.cpp)
target_link_libraries(agentserver_smoke PRIVATE agentserver_lib)

# FieldServer — fan-out #5: s_CFieldServer + GLGaeaServer skeleton (gameplay engine)
add_library(fieldserver_lib STATIC servers/fieldserver/field_server.cpp)
target_include_directories(fieldserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/fieldserver)
target_link_libraries(fieldserver_lib PUBLIC ran_net ran_db)

add_executable(fieldserver servers/fieldserver/main.cpp)
target_link_libraries(fieldserver PRIVATE fieldserver_lib)

add_executable(fieldserver_smoke servers/fieldserver/fieldserver_smoke.cpp)
target_link_libraries(fieldserver_smoke PRIVATE fieldserver_lib)
