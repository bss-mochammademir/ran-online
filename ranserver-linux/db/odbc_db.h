#ifndef SC_DB_ODBC_DB_H
#define SC_DB_ODBC_DB_H

#include <string>
#include <unordered_map>
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
// NOT yet ported (next chips): parameterized commands & output/return params
// (AppendParam / AppendIParam* family), binary GetChunk, scrollable cursor
// (MoveFirst/Previous/Last), and the _variant_t-based generic GetCollect<T>.
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

    const std::string& LastError() const { return m_err; }

private:
    bool execDirect(const std::string& sql);
    void buildColumnMap();
    short colIndex(const std::string& column);
    bool fetchRow();                       // single SQLFetch, updates m_eof
    void captureDiag(SQLSMALLINT type, SQLHANDLE h, const char* ctx);
    void freeStmt();

    SQLHENV  m_env  = SQL_NULL_HANDLE;
    SQLHDBC  m_conn = SQL_NULL_HANDLE;
    SQLHSTMT m_stmt = SQL_NULL_HANDLE;
    bool m_eof = true;
    std::unordered_map<std::string, short> m_cols;  // lowercased name -> 1-based idx
    std::string m_err;
};

}} // namespace sc::db

#endif // SC_DB_ODBC_DB_H
