#ifndef SC_DB_ODBC_DB_H
#define SC_DB_ODBC_DB_H

#include <string>
#include <unordered_map>
#include <vector>
#include <sql.h>
#include <sqlext.h>

namespace sc { namespace db {

// OdbcDb — Linux/cross-platform replacement for the *core read path* of the
// Windows ADO/COM class `sc::db::CjADO` (SigmaCore/Database/Ado/AdoClass.h),
// backed by the Microsoft ODBC Driver (msodbcsql18, validated in Spike #1).
//
// It mirrors CjADO's dominant idiom (the BEGIN_GETCOLLECT/END_GETCOLLECT macro):
//     db.Execute(query);
//     while (!db.GetEOF()) { db.GetCollect("col", var); db.Next(); }
// so most read-oriented call sites need only a type swap, not a rewrite.
//
// The WRITE path (parameterized stored procedures) is ported too, mirroring
// CjADO's AppendIParam*/OParam*/RParam* + GetParam over SQLBindParameter:
//     db.AppendIParamInteger("@CharID", id);
//     db.AppendOParamInteger("@Result");
//     db.AppendRParamInteger();
//     db.ExecuteStoredProcedure("dbo.sp_Foo");   // builds {? = call sp(?,?)}
//     db.GetParam("@Result", out);  db.GetParam("RETURN_VALUE", ret);
//
// NOT yet ported (next chips): money/currency (adCurrency scaling fidelity),
// GUID (adGUID) and binary image (adLongVarBinary) parameters, binary GetChunk,
// scrollable cursor (MoveFirst/Previous/Last), the _variant_t generic GetCollect<T>.
class OdbcDb {
public:
    OdbcDb() = default;
    ~OdbcDb();

    OdbcDb(const OdbcDb&) = delete;
    OdbcDb& operator=(const OdbcDb&) = delete;

    // connStr = full ODBC connection string, e.g.
    //   "DRIVER={ODBC Driver 18 for SQL Server};SERVER=host,1433;UID=sa;PWD=..;
    //    DATABASE=RanShop;Encrypt=yes;TrustServerCertificate=yes;"
    bool Open(const std::string& connStr);
    bool IsOpened() const { return m_conn != SQL_NULL_HANDLE; }
    void Close();

    bool Execute(const std::string& sql);                 // raw query / statement
    bool ExecuteStoredProcedure(const std::string& sp);   // EXEC <sp>

    // Forward-only cursor over the current result set. After Execute(), the
    // cursor is positioned on the first row (GetEOF()==false if any rows).
    bool GetEOF() const { return m_eof; }
    bool Next();   // advance one row (SQLFetch); sets EOF when past the end

    // Column access by name (ADO semantics). NOTE: with a forward-only cursor,
    // request columns in ascending order within a row (SQLGetData ordering).
    bool GetCollect(const std::string& column, std::string& out);
    bool GetCollect(const std::string& column, int& out);
    bool GetCollect(const std::string& column, long long& out);

    // --- Parameterized commands (write path) — mirrors CjADO Append*Param ------
    // Accumulate parameters, then call ExecuteStoredProcedure(sp). The first
    // Append* after an execute auto-clears the previous set. Append order ==
    // the SP's positional parameter order (ADO binds appended params by
    // position); a return-value param is routed to the SP RETURN regardless of
    // where it is appended (mirrors ADO's direction-based routing).
    void AppendIParamInteger(const std::string& name, int var);     // INT  in
    void AppendOParamInteger(const std::string& name);              // INT  out
    void AppendRParamInteger();                                     // RETURN (int)
    void AppendIParamBigint(const std::string& name, long long var);// BIGINT in
    void AppendOParamBigint(const std::string& name);               // BIGINT out
    void AppendIParamTinyint(const std::string& name, unsigned char var);
    void AppendOParamTinyint(const std::string& name);
    void AppendIParamSmall(const std::string& name, unsigned short var);
    void AppendOParamSmall(const std::string& name);
    void AppendIParamVarchar(const std::string& name, const std::string& var, long size = 0);
    void AppendOParamVarchar(const std::string& name, long size);   // size = out buffer chars
    void AppendIParamFloat(const std::string& name, double var);    // FLOAT (adDouble) in
    void AppendOParamFloat(const std::string& name);

    void ClearParams();   // drop all accumulated params (also auto-done on next Append after execute)

    // Read back an output / return parameter after ExecuteStoredProcedure().
    // NOTE (ODBC): output params are only valid once all result sets are
    // consumed — GetParam drains them automatically on first call.
    bool GetParam(const std::string& name, int& out);
    bool GetParam(const std::string& name, long long& out);
    bool GetParam(const std::string& name, std::string& out);

    const std::string& LastError() const { return m_err; }

private:
    // One accumulated bound parameter. `buf` is stable storage so its address
    // stays valid across SQLExecute and the later output read-back; `ind` is the
    // StrLen_or_Ind, bound by reference (the driver writes the output length).
    struct Param {
        std::string       name;          // compared lowercased in GetParam
        SQLSMALLINT       ioType = SQL_PARAM_INPUT;   // SQL_PARAM_INPUT/_OUTPUT
        bool              isReturn = false;           // adParamReturnValue -> {?=call}
        SQLSMALLINT       cType = 0;     // SQL_C_*
        SQLSMALLINT       sqlType = 0;   // SQL_*
        SQLULEN           columnSize = 0;
        SQLSMALLINT       decimalDigits = 0;
        std::vector<char> buf;           // value storage (in & out)
        SQLLEN            ind = 0;        // StrLen_or_Ind
    };

    bool execDirect(const std::string& sql);
    bool execCall(const std::string& sp);  // parameterized {? = call sp(?,?)} path
    void buildColumnMap();
    short colIndex(const std::string& column);
    bool fetchRow();                       // single SQLFetch, updates m_eof
    void drainResults();                   // exhaust SQLMoreResults -> output params valid
    void captureDiag(SQLSMALLINT type, SQLHANDLE h, const char* ctx);
    void freeStmt();
    void beginParam();                     // auto-clear params after an execute
    Param* findParam(const std::string& name);
    // construct + accumulate a parameter (caller fills buf/ind for value/output)
    Param& addParam(const std::string& name, SQLSMALLINT ioType, bool isReturn,
                    SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN columnSize);
    template <class T> void fillInput(Param& p, T v);     // store value bytes, ind=sizeof
    void fillOutput(Param& p, std::size_t capacity);      // alloc buffer, ind=NULL until driver writes

    SQLHENV  m_env  = SQL_NULL_HANDLE;
    SQLHDBC  m_conn = SQL_NULL_HANDLE;
    SQLHSTMT m_stmt = SQL_NULL_HANDLE;
    bool m_eof = true;
    bool m_resultsDrained = false;         // have output params been made valid?
    bool m_paramsExecuted = false;         // params consumed by an execute?
    std::unordered_map<std::string, short> m_cols;  // lowercased name -> 1-based idx
    std::vector<Param> m_params;
    std::string m_err;
};

}} // namespace sc::db

#endif // SC_DB_ODBC_DB_H
