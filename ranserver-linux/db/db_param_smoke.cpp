// db_param_smoke — exercises the OdbcDb WRITE path (parameterized stored
// procedures) against SQL Server on Linux, mirroring the CjADO idiom:
//   AppendIParam*/OParam*/RParam*  ->  ExecuteStoredProcedure  ->  GetParam.
//
// It is self-contained and residue-free: it CREATEs two throwaway test procs
// (CREATE OR ALTER), calls them with input/output/return params, asserts the
// round-trips, then DROPs them. Needs a live SQL Server + msodbcsql18.
#include "odbc_db.h"
#include <cstdlib>
#include <iostream>
#include <string>

static int g_fail = 0;
template <class A, class B>
static void check(const char* what, const A& got, const B& want) {
    bool okv = (got == static_cast<A>(want));
    std::cout << (okv ? "  PASS " : "  FAIL ") << what << " => " << got;
    if (!okv) { std::cout << "  (expected " << want << ")"; ++g_fail; }
    std::cout << "\n";
}

int main() {
    const char* pw     = std::getenv("SA_PASSWORD");
    const char* server = std::getenv("DB_SERVER"); if (!server) server = "ranmssql,1433";
    const char* dbname = std::getenv("DB_NAME");   if (!dbname) dbname = "RanShop";
    if (!pw) { std::cerr << "SA_PASSWORD not set\n"; return 2; }

    const std::string conn =
        std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + server +
        ";UID=sa;PWD=" + pw + ";DATABASE=" + dbname +
        ";Encrypt=yes;TrustServerCertificate=yes;";

    sc::db::OdbcDb db;
    if (!db.Open(conn)) { std::cerr << "Open failed: " << db.LastError() << "\n"; return 1; }
    std::cout << "OdbcDb.Open OK — exercising the CjADO write path on ODBC.\n";

    // --- setup: throwaway test procs (idempotent) -------------------------------
    const char* kProc1 =
        "CREATE OR ALTER PROCEDURE dbo.sp__odbc_param_smoke "
        "  @i_int INT, @i_label VARCHAR(32), @i_tiny TINYINT, @i_small SMALLINT, "
        "  @i_float FLOAT, @o_int INT OUTPUT "
        "AS BEGIN "
        "  SET NOCOUNT ON; "
        "  SET @o_int = @i_int * 2; "
        "  SELECT @i_int AS i_echo, @i_label AS l_echo, @i_tiny AS t_echo, "
        "         @i_small AS s_echo, @i_float AS f_echo; "
        "  RETURN @i_int + 100; "
        "END";
    const char* kProc2 =
        "CREATE OR ALTER PROCEDURE dbo.sp__odbc_param_smoke2 "
        "  @i_big BIGINT, @o_big BIGINT OUTPUT "
        "AS BEGIN "
        "  SET NOCOUNT ON; "
        "  SET @o_big = @i_big * 1000000; "
        "  RETURN 7; "
        "END";
    if (!db.Execute(kProc1) || !db.Execute(kProc2)) {
        std::cerr << "create test proc failed: " << db.LastError() << "\n"; return 1;
    }

    // --- proc #1: every shipped scalar type, input + result set + output + return -
    std::cout << "\n[SP+params] dbo.sp__odbc_param_smoke "
                 "(int/varchar/tinyint/smallint/float in; int out; int return)\n";
    db.AppendIParamInteger("@i_int", 21);
    db.AppendIParamVarchar("@i_label", "hello", 32);
    db.AppendIParamTinyint("@i_tiny", 200);
    db.AppendIParamSmall("@i_small", 30000);
    db.AppendIParamFloat("@i_float", 3.5);
    db.AppendOParamInteger("@o_int");
    db.AppendRParamInteger();
    if (!db.ExecuteStoredProcedure("dbo.sp__odbc_param_smoke")) {
        std::cerr << "exec proc1 failed: " << db.LastError() << "\n"; return 1;
    }

    int iEcho = 0, tEcho = 0, sEcho = 0; std::string lEcho, fEcho;
    if (!db.GetEOF()) {
        db.GetCollect("i_echo", iEcho);     // request columns in ascending order
        db.GetCollect("l_echo", lEcho);
        db.GetCollect("t_echo", tEcho);
        db.GetCollect("s_echo", sEcho);
        db.GetCollect("f_echo", fEcho);
        db.Next();
    }
    check("input int  (i_echo)",   iEcho, 21);
    check("input char (l_echo)",   lEcho, std::string("hello"));
    check("input tiny (t_echo)",   tEcho, 200);
    check("input small(s_echo)",   sEcho, 30000);
    std::cout << "  PASS input float(f_echo) bound => " << fEcho
              << (fEcho.empty() ? "  (FAIL: empty)" : "") << "\n";
    if (fEcho.empty()) ++g_fail;

    int oInt = 0, ret1 = 0;
    db.GetParam("@o_int", oInt);            // drains result set, then reads output
    db.GetParam("RETURN_VALUE", ret1);
    check("OUTPUT param (@o_int = 2*in)", oInt, 42);
    check("RETURN value (in + 100)",      ret1, 121);

    // --- proc #2: bigint > 32-bit, output, no result set (drain path) -----------
    std::cout << "\n[SP+params] dbo.sp__odbc_param_smoke2 (bigint in/out, no result set)\n";
    db.AppendIParamBigint("@i_big", 5000000000LL);   // > INT_MAX
    db.AppendOParamBigint("@o_big");
    db.AppendRParamInteger();
    if (!db.ExecuteStoredProcedure("dbo.sp__odbc_param_smoke2")) {
        std::cerr << "exec proc2 failed: " << db.LastError() << "\n"; return 1;
    }
    long long oBig = 0; int ret2 = 0;
    db.GetParam("@o_big", oBig);
    db.GetParam("RETURN_VALUE", ret2);
    check("BIGINT OUTPUT (5e9 * 1e6)", oBig, 5000000000000000LL);
    check("RETURN value",             ret2, 7);

    // --- teardown ---------------------------------------------------------------
    db.ClearParams();
    db.Execute("DROP PROCEDURE IF EXISTS dbo.sp__odbc_param_smoke");
    db.Execute("DROP PROCEDURE IF EXISTS dbo.sp__odbc_param_smoke2");
    db.Close();

    std::cout << "\n";
    if (g_fail == 0) {
        std::cout << "DB-WRITE-PATH SLICE OK: parameterized SP exec (input + output + "
                     "return) via SQLBindParameter — CjADO Append*Param replacement.\n";
        return 0;
    }
    std::cout << "DB-WRITE-PATH SLICE FAILED: " << g_fail << " assertion(s) failed.\n";
    return 1;
}
