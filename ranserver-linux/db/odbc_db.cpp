#include "odbc_db.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace sc { namespace db {

static bool ok(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

OdbcDb::~OdbcDb() { Close(); }

void OdbcDb::captureDiag(SQLSMALLINT type, SQLHANDLE h, const char* ctx) {
    SQLCHAR state[6], msg[1024];
    SQLINTEGER native; SQLSMALLINT len, i = 1;
    m_err = ctx;
    while (SQLGetDiagRec(type, h, i++, state, &native, msg, sizeof(msg), &len) == SQL_SUCCESS) {
        m_err += " | [";
        m_err += reinterpret_cast<char*>(state);
        m_err += "] ";
        m_err += reinterpret_cast<char*>(msg);
    }
}

bool OdbcDb::Open(const std::string& connStr) {
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env))) { m_err = "alloc env failed"; return false; }
    SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_conn))) { captureDiag(SQL_HANDLE_ENV, m_env, "alloc dbc"); Close(); return false; }

    SQLCHAR outStr[1024]; SQLSMALLINT outLen = 0;
    SQLRETURN r = SQLDriverConnect(
        m_conn, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(connStr.c_str())), SQL_NTS,
        outStr, sizeof(outStr), &outLen, SQL_DRIVER_NOPROMPT);
    if (!ok(r)) { captureDiag(SQL_HANDLE_DBC, m_conn, "SQLDriverConnect"); Close(); return false; }
    return true;
}

void OdbcDb::freeStmt() {
    if (m_stmt != SQL_NULL_HANDLE) {
        SQLFreeHandle(SQL_HANDLE_STMT, m_stmt);
        m_stmt = SQL_NULL_HANDLE;
    }
    m_cols.clear();
    m_eof = true;
}

void OdbcDb::Close() {
    freeStmt();
    if (m_conn != SQL_NULL_HANDLE) { SQLDisconnect(m_conn); SQLFreeHandle(SQL_HANDLE_DBC, m_conn); m_conn = SQL_NULL_HANDLE; }
    if (m_env  != SQL_NULL_HANDLE) { SQLFreeHandle(SQL_HANDLE_ENV, m_env);  m_env  = SQL_NULL_HANDLE; }
}

bool OdbcDb::execDirect(const std::string& sql) {
    if (m_conn == SQL_NULL_HANDLE) { m_err = "not connected"; return false; }
    freeStmt();
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, m_conn, &m_stmt))) { captureDiag(SQL_HANDLE_DBC, m_conn, "alloc stmt"); return false; }
    SQLRETURN r = SQLExecDirect(m_stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
    if (!ok(r) && r != SQL_NO_DATA) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLExecDirect"); return false; }
    return true;
}

void OdbcDb::buildColumnMap() {
    m_cols.clear();
    SQLSMALLINT cols = 0;
    if (!ok(SQLNumResultCols(m_stmt, &cols))) return;
    for (SQLSMALLINT i = 1; i <= cols; ++i) {
        SQLCHAR name[256]; SQLSMALLINT nameLen = 0, dataType = 0, decDigits = 0, nullable = 0;
        SQLULEN colSize = 0;
        if (ok(SQLDescribeCol(m_stmt, i, name, sizeof(name), &nameLen, &dataType, &colSize, &decDigits, &nullable))) {
            m_cols[lower(std::string(reinterpret_cast<char*>(name), nameLen))] = i;
        }
    }
}

bool OdbcDb::fetchRow() {
    SQLRETURN r = SQLFetch(m_stmt);
    if (r == SQL_NO_DATA) { m_eof = true; return false; }
    if (!ok(r)) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLFetch"); m_eof = true; return false; }
    m_eof = false;
    return true;
}

bool OdbcDb::Execute(const std::string& sql) {
    if (!execDirect(sql)) return false;
    buildColumnMap();
    fetchRow();   // position on the first row (ADO semantics)
    return true;
}

bool OdbcDb::ExecuteStoredProcedure(const std::string& sp) {
    return Execute("EXEC " + sp);
}

bool OdbcDb::Next() {
    if (m_stmt == SQL_NULL_HANDLE) { m_eof = true; return false; }
    return fetchRow();
}

short OdbcDb::colIndex(const std::string& column) {
    auto it = m_cols.find(lower(column));
    return it == m_cols.end() ? 0 : it->second;
}

bool OdbcDb::GetCollect(const std::string& column, std::string& out) {
    out.clear();
    short idx = colIndex(column);
    if (idx == 0) { m_err = "unknown column: " + column; return false; }
    char buf[4096]; SQLLEN ind = 0;
    SQLRETURN r = SQLGetData(m_stmt, idx, SQL_C_CHAR, buf, sizeof(buf), &ind);
    if (!ok(r)) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLGetData(str)"); return false; }
    if (ind == SQL_NULL_DATA) return false;
    out.assign(buf, (ind == SQL_NTS || ind < 0) ? std::char_traits<char>::length(buf)
                                                 : static_cast<size_t>(ind));
    return true;
}

bool OdbcDb::GetCollect(const std::string& column, int& out) {
    out = 0;
    short idx = colIndex(column);
    if (idx == 0) { m_err = "unknown column: " + column; return false; }
    SQLINTEGER v = 0; SQLLEN ind = 0;
    SQLRETURN r = SQLGetData(m_stmt, idx, SQL_C_SLONG, &v, sizeof(v), &ind);
    if (!ok(r)) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLGetData(int)"); return false; }
    if (ind == SQL_NULL_DATA) return false;
    out = static_cast<int>(v);
    return true;
}

bool OdbcDb::GetCollect(const std::string& column, long long& out) {
    out = 0;
    short idx = colIndex(column);
    if (idx == 0) { m_err = "unknown column: " + column; return false; }
    SQLBIGINT v = 0; SQLLEN ind = 0;
    SQLRETURN r = SQLGetData(m_stmt, idx, SQL_C_SBIGINT, &v, sizeof(v), &ind);
    if (!ok(r)) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLGetData(bigint)"); return false; }
    if (ind == SQL_NULL_DATA) return false;
    out = static_cast<long long>(v);
    return true;
}

}} // namespace sc::db
