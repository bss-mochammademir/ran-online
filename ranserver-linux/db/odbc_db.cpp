#include "odbc_db.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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
    m_resultsDrained = false;
}

void OdbcDb::Close() {
    freeStmt();
    m_params.clear();
    m_paramsExecuted = false;
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
    // No accumulated params -> the simple, already-validated EXEC path.
    if (m_params.empty()) return Execute("EXEC " + sp);
    // Parameterized -> bind via the ODBC call escape (mirrors CjADO Execute4Cmd
    // with adCmdStoredProc, which binds the appended params positionally).
    return execCall(sp);
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

// ===========================================================================
// Parameterized commands (write path) — CjADO AppendIParam*/OParam*/RParam*
// over SQLBindParameter, executed via the ODBC {? = call sp(?,?)} escape.
// ===========================================================================

void OdbcDb::beginParam() {
    if (m_paramsExecuted) { m_params.clear(); m_paramsExecuted = false; }
}

void OdbcDb::ClearParams() {
    m_params.clear();
    m_paramsExecuted = false;
}

OdbcDb::Param* OdbcDb::findParam(const std::string& name) {
    const std::string key = lower(name);
    for (auto& p : m_params) if (lower(p.name) == key) return &p;
    return nullptr;
}

OdbcDb::Param& OdbcDb::addParam(const std::string& name, SQLSMALLINT ioType, bool isReturn,
                                SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN columnSize) {
    beginParam();
    Param p;
    p.name = name;
    p.ioType = ioType;
    p.isReturn = isReturn;
    p.cType = cType;
    p.sqlType = sqlType;
    p.columnSize = columnSize;
    p.decimalDigits = 0;
    m_params.push_back(std::move(p));
    return m_params.back();
}

// --- input helper: fill buf with the value bytes, ind = byte length ----------
template <class T>
void OdbcDb::fillInput(Param& p, T v) {
    p.buf.resize(sizeof(T));
    std::memcpy(p.buf.data(), &v, sizeof(T));
    p.ind = static_cast<SQLLEN>(sizeof(T));
}

// --- output helper: allocate buf, mark NULL until the driver writes back ------
void OdbcDb::fillOutput(Param& p, std::size_t capacity) {
    p.buf.assign(capacity, '\0');
    p.ind = SQL_NULL_DATA;
}

//! INT  <-> int
void OdbcDb::AppendIParamInteger(const std::string& name, int var) {
    fillInput<int>(addParam(name, SQL_PARAM_INPUT, false, SQL_C_SLONG, SQL_INTEGER, 10), var);
}
void OdbcDb::AppendOParamInteger(const std::string& name) {
    fillOutput(addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_SLONG, SQL_INTEGER, 10), sizeof(int));
}
void OdbcDb::AppendRParamInteger() {
    fillOutput(addParam("RETURN_VALUE", SQL_PARAM_OUTPUT, true, SQL_C_SLONG, SQL_INTEGER, 10), sizeof(int));
}

//! BIGINT <-> long long
void OdbcDb::AppendIParamBigint(const std::string& name, long long var) {
    fillInput<long long>(addParam(name, SQL_PARAM_INPUT, false, SQL_C_SBIGINT, SQL_BIGINT, 19), var);
}
void OdbcDb::AppendOParamBigint(const std::string& name) {
    fillOutput(addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_SBIGINT, SQL_BIGINT, 19), sizeof(long long));
}

//! TINYINT <-> unsigned char (SQL Server tinyint is unsigned 0..255)
void OdbcDb::AppendIParamTinyint(const std::string& name, unsigned char var) {
    fillInput<unsigned char>(addParam(name, SQL_PARAM_INPUT, false, SQL_C_UTINYINT, SQL_TINYINT, 3), var);
}
void OdbcDb::AppendOParamTinyint(const std::string& name) {
    fillOutput(addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_UTINYINT, SQL_TINYINT, 3), sizeof(unsigned char));
}

//! SMALLINT <-> short (CjADO passes unsigned short; values <=32767 round-trip)
void OdbcDb::AppendIParamSmall(const std::string& name, unsigned short var) {
    fillInput<short>(addParam(name, SQL_PARAM_INPUT, false, SQL_C_SSHORT, SQL_SMALLINT, 5),
                     static_cast<short>(var));
}
void OdbcDb::AppendOParamSmall(const std::string& name) {
    fillOutput(addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_SSHORT, SQL_SMALLINT, 5), sizeof(short));
}

//! VARCHAR <-> std::string
void OdbcDb::AppendIParamVarchar(const std::string& name, const std::string& var, long size) {
    SQLULEN colSize = size > 0 ? static_cast<SQLULEN>(size)
                               : (var.empty() ? 1 : static_cast<SQLULEN>(var.size()));
    Param& p = addParam(name, SQL_PARAM_INPUT, false, SQL_C_CHAR, SQL_VARCHAR, colSize);
    p.buf.assign(var.begin(), var.end());
    p.buf.push_back('\0');                       // NUL terminate (defensive)
    p.ind = static_cast<SQLLEN>(var.size());     // exact byte length bound
}
void OdbcDb::AppendOParamVarchar(const std::string& name, long size) {
    std::size_t cap = size > 0 ? static_cast<std::size_t>(size) : 256;
    Param& p = addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_CHAR, SQL_VARCHAR,
                        static_cast<SQLULEN>(cap));
    fillOutput(p, cap + 1);                       // +1 for the driver's NUL
}

//! FLOAT (adDouble) <-> double
void OdbcDb::AppendIParamFloat(const std::string& name, double var) {
    fillInput<double>(addParam(name, SQL_PARAM_INPUT, false, SQL_C_DOUBLE, SQL_DOUBLE, 15), var);
}
void OdbcDb::AppendOParamFloat(const std::string& name) {
    fillOutput(addParam(name, SQL_PARAM_OUTPUT, false, SQL_C_DOUBLE, SQL_DOUBLE, 15), sizeof(double));
}

bool OdbcDb::execCall(const std::string& sp) {
    if (m_conn == SQL_NULL_HANDLE) { m_err = "not connected"; return false; }
    freeStmt();
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, m_conn, &m_stmt))) {
        captureDiag(SQL_HANDLE_DBC, m_conn, "alloc stmt"); return false;
    }

    // Build the ODBC call escape. A return-value param becomes the leading
    // "? =" (ODBC parameter #1); every other param is a positional "?".
    bool hasReturn = false;
    int markers = 0;
    for (const auto& p : m_params) { if (p.isReturn) hasReturn = true; else ++markers; }

    std::string call = "{ ";
    if (hasReturn) call += "? = ";
    call += "call " + sp;
    if (markers > 0) {
        call += " (";
        for (int i = 0; i < markers; ++i) call += (i ? ",?" : "?");
        call += ")";
    }
    call += " }";

    if (!ok(SQLPrepare(m_stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(call.c_str())), SQL_NTS))) {
        captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLPrepare(call)"); return false;
    }

    // Bind: return value first (ODBC #1), then the rest in append order.
    SQLUSMALLINT num = 1;
    auto bind = [&](Param& p) -> bool {
        SQLRETURN r = SQLBindParameter(
            m_stmt, num++, p.ioType, p.cType, p.sqlType,
            p.columnSize, p.decimalDigits,
            p.buf.data(), static_cast<SQLLEN>(p.buf.size()), &p.ind);
        if (!ok(r)) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLBindParameter"); return false; }
        return true;
    };
    for (auto& p : m_params) if (p.isReturn) { if (!bind(p)) return false; }
    for (auto& p : m_params) if (!p.isReturn) { if (!bind(p)) return false; }

    SQLRETURN r = SQLExecute(m_stmt);
    if (!ok(r) && r != SQL_NO_DATA) { captureDiag(SQL_HANDLE_STMT, m_stmt, "SQLExecute(call)"); return false; }

    // Expose the first result set (if any) to the read API; output/return
    // params are not valid until results are drained (see GetParam).
    buildColumnMap();
    fetchRow();
    m_resultsDrained = false;
    m_paramsExecuted = true;
    return true;
}

void OdbcDb::drainResults() {
    if (m_stmt != SQL_NULL_HANDLE) {
        while (SQLMoreResults(m_stmt) == SQL_SUCCESS) { /* skip remaining result sets */ }
    }
    m_resultsDrained = true;
    m_eof = true;
}

bool OdbcDb::GetParam(const std::string& name, int& out) {
    out = 0;
    if (!m_resultsDrained) drainResults();
    Param* p = findParam(name);
    if (!p) { m_err = "unknown param: " + name; return false; }
    if (p->ind == SQL_NULL_DATA) return false;
    if (p->buf.size() < sizeof(int)) { m_err = "param buffer too small: " + name; return false; }
    int v = 0; std::memcpy(&v, p->buf.data(), sizeof(int)); out = v;
    return true;
}

bool OdbcDb::GetParam(const std::string& name, long long& out) {
    out = 0;
    if (!m_resultsDrained) drainResults();
    Param* p = findParam(name);
    if (!p) { m_err = "unknown param: " + name; return false; }
    if (p->ind == SQL_NULL_DATA) return false;
    if (p->buf.size() < sizeof(long long)) { m_err = "param buffer too small: " + name; return false; }
    long long v = 0; std::memcpy(&v, p->buf.data(), sizeof(long long)); out = v;
    return true;
}

bool OdbcDb::GetParam(const std::string& name, std::string& out) {
    out.clear();
    if (!m_resultsDrained) drainResults();
    Param* p = findParam(name);
    if (!p) { m_err = "unknown param: " + name; return false; }
    if (p->ind == SQL_NULL_DATA) return false;
    std::size_t len = (p->ind >= 0) ? static_cast<std::size_t>(p->ind)
                                    : std::char_traits<char>::length(p->buf.data());
    if (len > p->buf.size()) len = p->buf.size();
    out.assign(p->buf.data(), len);
    return true;
}

}} // namespace sc::db
