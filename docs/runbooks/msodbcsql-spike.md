# Runbook — Spike #1: Konektor C++ `msodbcsql` (sisi aplikasi ADR-001)

> **Tujuan**: Membuktikan paruh **aplikasi** dari [ADR-001](../adr/ADR-001-cloud-native-vs-rejuvenation.md) — bahwa kode **C++ di Linux** dapat terhubung & mengeksekusi query + stored procedure ke **SQL Server on Linux** menggunakan **Microsoft ODBC Driver 18 (`msodbcsql18`)**, menggantikan jalur Windows ADO/COM (`CjADO` di [`AdoClass.cpp`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Database/Ado/AdoClass.cpp)). Spike #0 ([db-restore](db-restore.md)) sudah membuktikan paruh **database**; spike ini menutup paruh aplikasi.

> ✅ **Sudah dieksekusi 2026-06-14** — hasil di [§Hasil](#hasil-eksekusi-2026-06-14). Status: **LULUS**.

Berjalan **100% lokal** (Docker), tanpa cloud.

---

## Prasyarat
- Spike #0 selesai: container `ranmssql` (SQL Server 2022 on Linux) hidup dengan 8 DB ter-restore.
- Docker. Apple Silicon → image dibangun `--platform linux/amd64` (emulasi).

## Langkah 1 — Jaringan Docker (agar container spike menghubungi DB by name)
```bash
docker network create rannet 2>/dev/null
docker network connect rannet ranmssql 2>/dev/null   # gabungkan DB ke jaringan
```
Container spike lalu memakai `SERVER=ranmssql,1433`.

## Langkah 2 — Program C++ (ODBC API)
`main.cpp` — connect → `SELECT @@VERSION` → baca `sys.tables` → `EXEC` stored procedure:

```cpp
#include <sql.h>
#include <sqlext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool ok(SQLRETURN r){ return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

static void diag(SQLSMALLINT type, SQLHANDLE h, const char* ctx){
    SQLCHAR state[6], msg[1024]; SQLINTEGER nativeErr; SQLSMALLINT len, i = 1;
    fprintf(stderr, "[ERR] %s\n", ctx);
    while (SQLGetDiagRec(type, h, i++, state, &nativeErr, msg, sizeof(msg), &len) == SQL_SUCCESS)
        fprintf(stderr, "   SQLSTATE=%s native=%d %s\n", state, (int)nativeErr, msg);
}

static int run(SQLHDBC dbc, const char* label, const char* sql, int maxRows){
    SQLHSTMT st;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) { diag(SQL_HANDLE_DBC, dbc, "alloc stmt"); return -1; }
    printf("\n--- %s ---\n%s\n", label, sql);
    if (!ok(SQLExecDirect(st, (SQLCHAR*)sql, SQL_NTS))) { diag(SQL_HANDLE_STMT, st, "exec"); SQLFreeHandle(SQL_HANDLE_STMT, st); return -1; }
    SQLSMALLINT cols = 0; SQLNumResultCols(st, &cols);
    for (SQLSMALLINT c = 1; c <= cols; c++){ SQLCHAR name[128]; SQLSMALLINT nlen;
        SQLDescribeCol(st, c, name, sizeof(name), &nlen, 0, 0, 0, 0);
        printf("%s%s", c > 1 ? " | " : "", (char*)name); }
    printf("\n");
    int rows = 0;
    while (SQLFetch(st) == SQL_SUCCESS){
        if (rows < maxRows){
            for (SQLSMALLINT c = 1; c <= cols; c++){ char buf[4096]; SQLLEN ind;
                if (!ok(SQLGetData(st, c, SQL_C_CHAR, buf, sizeof(buf), &ind))) std::strcpy(buf, "<err>");
                printf("%s%s", c > 1 ? " | " : "", ind == SQL_NULL_DATA ? "NULL" : buf); }
            printf("\n");
        }
        rows++;
    }
    printf("[rows=%d]\n", rows);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return rows;
}

int main(){
    const char* pw     = getenv("SA_PASSWORD");
    const char* server = getenv("DB_SERVER"); if (!server) server = "ranmssql,1433";
    const char* db     = getenv("DB_NAME");   if (!db)     db     = "RanShop";
    if (!pw){ fprintf(stderr, "SA_PASSWORD not set\n"); return 2; }
    std::string conn = std::string("DRIVER={ODBC Driver 18 for SQL Server};SERVER=") + server +
                       ";UID=sa;PWD=" + pw + ";DATABASE=" + db + ";Encrypt=yes;TrustServerCertificate=yes;";
    SQLHENV env; SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    SQLHDBC dbc; SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    printf("Connecting to %s / DB=%s via ODBC Driver 18 for SQL Server ...\n", server, db);
    SQLCHAR outc[1024]; SQLSMALLINT outlen;
    if (!ok(SQLDriverConnect(dbc, NULL, (SQLCHAR*)conn.c_str(), SQL_NTS, outc, sizeof(outc), &outlen, SQL_DRIVER_NOPROMPT))){
        diag(SQL_HANDLE_DBC, dbc, "connect"); return 1; }
    printf("CONNECTED — ODBC replaces the Windows ADO/COM (CjADO) path.\n");
    int rc = 0;
    if (run(dbc, "Query: server version", "SELECT @@VERSION", 1) < 0) rc = 1;
    if (run(dbc, "Query: first 3 tables", "SELECT TOP 3 name FROM sys.tables ORDER BY name", 3) < 0) rc = 1;
    if (run(dbc, "Stored procedure: EXEC dbo.sp_PointShopSelect", "EXEC dbo.sp_PointShopSelect", 5) < 0) rc = 1;
    SQLDisconnect(dbc); SQLFreeHandle(SQL_HANDLE_DBC, dbc); SQLFreeHandle(SQL_HANDLE_ENV, env);
    printf("\n%s\n", rc == 0
        ? "SPIKE OK: connect + query + stored-procedure all succeeded via msodbcsql18 on Linux."
        : "SPIKE FAILED");
    return rc;
}
```

## Langkah 3 — Dockerfile (Ubuntu + unixODBC + msodbcsql18 + g++)
```dockerfile
FROM --platform=linux/amd64 ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive ACCEPT_EULA=Y
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl gnupg ca-certificates apt-transport-https g++ unixodbc unixodbc-dev \
 && curl -fsSL https://packages.microsoft.com/keys/microsoft.asc \
        | gpg --dearmor -o /usr/share/keyrings/microsoft-prod.gpg \
 && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/microsoft-prod.gpg] https://packages.microsoft.com/ubuntu/22.04/prod jammy main" \
        > /etc/apt/sources.list.d/mssql-release.list \
 && apt-get update && apt-get install -y --no-install-recommends msodbcsql18 \
 && rm -rf /var/lib/apt/lists/*
COPY main.cpp /spike/main.cpp
RUN g++ -O2 -Wall -o /spike/odbc_spike /spike/main.cpp -lodbc
CMD ["/spike/odbc_spike"]
```

## Langkah 4 — Build & jalankan
```bash
docker build --platform linux/amd64 -t odbc-spike .
docker run --rm --platform linux/amd64 --network rannet \
  -e SA_PASSWORD="$SA_PASSWORD" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanShop" \
  odbc-spike
```

---

## Hasil Eksekusi (2026-06-14)
```
Connecting to ranmssql,1433 / DB=RanShop via ODBC Driver 18 for SQL Server ...
CONNECTED — ODBC replaces the Windows ADO/COM (CjADO) path.

--- Query: server version ---
Microsoft SQL Server 2022 (RTM-CU25) ... Developer Edition (64-bit) on Linux (Ubuntu 22.04.5 LTS) <X64>
[rows=1]

--- Query: first 3 tables ---
name: _ShopPurchase_091012 | _ShopPurchase_100121 | AttendLog
[rows=3]

--- Stored procedure: EXEC dbo.sp_PointShopSelect ---
ItemMain | ItemSub | ItemPrice
[rows=0]

SPIKE OK: connect + query + stored-procedure all succeeded via msodbcsql18 on Linux.
```

## Kesimpulan & implikasi
- **LULUS.** Jalur akses data cross-platform terbukti: `connect → query → EXEC stored procedure` semua jalan dari C++ Linux via `msodbcsql18`. Tabel `sys.tables` mengembalikan data nyata; SP mengembalikan metadata kolom (baris 0 karena restore schema-only).
- **Penutup ADR-001**: digabung [Spike #0](db-restore.md) (DB + 866 SP, audit bersih), **kedua paruh taruhan Hybrid tervalidasi empiris** → risiko terbesar Fase 1 (konektor DB) ditutup.
- **Strategi porting nyata**: pertahankan *interface* `CjADO`, ganti *implementasi* di belakangnya dengan ODBC handle (`SQLDriverConnect`/`SQLExecDirect`/`SQLFetch`) — perubahan terlokalisir di [`SigmaCore/Database`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Database), logika SP & pemanggil tak berubah.
- **Sisa de-risk Fase 1**: porting jaringan IOCP → `boost::asio` (spike berikutnya).

### Bersih-bersih (opsional)
```bash
docker rm -f ranmssql           # hentikan SQL Server
docker network rm rannet        # hapus jaringan
docker rmi odbc-spike           # hapus image spike
```
