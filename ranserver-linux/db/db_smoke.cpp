// db_smoke — exercises the OdbcDb read-path port against SQL Server on Linux,
// mirroring the CjADO idiom: Execute -> while(!GetEOF()){ GetCollect; Next(); }.
// Needs a live SQL Server (Spike #0 container) + the msodbcsql18 driver.
#include "odbc_db.h"
#include <cstdlib>
#include <iostream>
#include <string>

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
    std::cout << "OdbcDb.Open OK — core CjADO read-path ported onto ODBC.\n";

    // (1) Raw query, name-based GetCollect, CjADO BEGIN/END_GETCOLLECT idiom.
    std::cout << "\n[Query] SELECT TOP 3 name, object_id FROM sys.tables ORDER BY name\n";
    if (!db.Execute("SELECT TOP 3 name, object_id FROM sys.tables ORDER BY name")) {
        std::cerr << db.LastError() << "\n"; return 1;
    }
    int rows = 0;
    while (!db.GetEOF()) {
        std::string name; long long oid = 0;
        db.GetCollect("name", name);          // request columns in ascending order
        db.GetCollect("object_id", oid);
        std::cout << "  row: name=" << name << "  object_id=" << oid << "\n";
        ++rows;
        db.Next();
    }
    std::cout << "  [" << rows << " rows]\n";

    // (2) Stored procedure via the same path.
    std::cout << "\n[Stored proc] EXEC dbo.sp_PointShopSelect\n";
    if (!db.ExecuteStoredProcedure("dbo.sp_PointShopSelect")) {
        std::cerr << db.LastError() << "\n"; return 1;
    }
    int sprows = 0;
    while (!db.GetEOF()) { ++sprows; db.Next(); }
    std::cout << "  [" << sprows << " rows]\n";

    db.Close();
    std::cout << "\nDB-LAYER SLICE OK: connect + raw query + stored procedure + "
                 "name-based GetCollect, all via msodbcsql18 (CjADO read-path replacement).\n";
    return 0;
}
