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

# Fan-out point: add per-server targets (login/agent/field/session/cache) here,
# each reusing platform/ + the shared net/db layers.
