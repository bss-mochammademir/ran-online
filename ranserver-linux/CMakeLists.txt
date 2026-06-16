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

# Network layer — cross-platform server base + packet codec, IOCP -> boost::asio
# port of the shared Win32 NetServer (RanLogicServer/Server/NetServer) and the
# message framing/queue (SigmaCore/Net + RanLogic/s_NetGlobal). boost::asio is
# header-only; only Threads is linked.
add_library(ran_net STATIC
    net/net_server.cpp
    net/packet.cpp        # NET_MSG_GENERIC framing (RanLogic/s_NetGlobal.h)
    net/session.cpp)      # per-connection async read loop
target_include_directories(ran_net PUBLIC ${CMAKE_SOURCE_DIR}/net ${Boost_INCLUDE_DIRS})
target_link_libraries(ran_net PUBLIC Threads::Threads)

# Packet codec unit smoke (no sockets): framing + MsgManager double-buffer.
add_executable(packet_smoke net/packet_smoke.cpp)
target_link_libraries(packet_smoke PRIVATE ran_net)

# ---- Fan-out: per-server targets, each riding the shared net/db layers ----------
# AuthServer (first fan-out server): Linux skeleton of CAuthServer on ran_net + ran_db.
add_library(authserver_lib STATIC servers/authserver/auth_server.cpp)
target_include_directories(authserver_lib PUBLIC ${CMAKE_SOURCE_DIR}/servers/authserver)
target_link_libraries(authserver_lib PUBLIC ran_net ran_db)

add_executable(authserver servers/authserver/main.cpp)
target_link_libraries(authserver PRIVATE authserver_lib)

add_executable(authserver_smoke servers/authserver/authserver_smoke.cpp)
target_link_libraries(authserver_smoke PRIVATE authserver_lib)
