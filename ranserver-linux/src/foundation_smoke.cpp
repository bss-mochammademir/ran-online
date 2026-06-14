// foundation_smoke — proves the unified Linux build foundation: the three
// shared-layer pieces of Fase 1 compile & link in ONE CMake target.
//   1) Win32 type abstraction  (platform/win32_compat.h)
//   2) network layer           (boost::asio — Spike #2)
//   3) DB driver layer         (unixODBC / ODBC API — Spike #1)
// No external services required: it links + initialises each layer, then exits.
#include <sql.h>          // ODBC first: its <sqltypes.h> owns WORD/DWORD/BYTE
#include <sqlext.h>
#include "win32_compat.h" // shim defers those to ODBC via __SQLTYPES_H guard
#include <boost/asio.hpp>
#include <iostream>

int main(){
    // (1) Win32 type via shim
    DWORD tick = 42;
    HANDLE h = nullptr;
    (void)h;

    // (2) network event loop (boost::asio) — IOCP/epoll dispatcher
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor probe(io);   // construct without binding
    (void)probe;

    // (3) ODBC environment (unixODBC) — entry point of the CjADO replacement
    SQLHENV env = SQL_NULL_HANDLE;
    SQLRETURN r = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    bool odbcOk = (r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO);
    if (odbcOk){
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
    }

    std::cout << "foundation_smoke OK:\n"
              << "  [1] win32_compat: DWORD=" << tick << " (uint32_t shim)\n"
              << "  [2] boost::asio io_context constructed\n"
              << "  [3] unixODBC env alloc: " << (odbcOk ? "ok" : "FAILED") << "\n"
              << "  -> unified cross-platform build foundation compiles, links & runs.\n";
    return odbcOk ? 0 : 1;
}
