# Runbook — Port DB-Layer: `CjADO` (ADO/COM) → `OdbcDb` (ODBC), jalur baca inti

> **Tujuan**: Mem-port **jalur baca inti** kelas Windows ADO/COM `sc::db::CjADO` ([`SigmaCore/Database/Ado/AdoClass.h`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Database/Ado/AdoClass.cpp)) ke implementasi **ODBC cross-platform** (`sc::db::OdbcDb`, `msodbcsql18` — divalidasi [Spike #1](msodbcsql-spike.md)). Ini **modul `SigmaCore` nyata pertama** yang di-port (sisi DB = A2 di [doc 07 §5](../07_ai_delivery_operating_model.md#5-pola-paralelisasi-pada-roadmap-ini-contoh-konkret)).

> ✅ **Sudah dieksekusi 2026-06-14** — `db_smoke` connect + query + stored proc + GetCollect **hijau** terhadap DB nyata. Lihat [§Hasil](#hasil-eksekusi-2026-06-14).

---

## Idiom dominan yang di-port
`CjADO` (via macro `BEGIN_GETCOLLECT`/`END_GETCOLLECT` di `AdoClass.h`):
```cpp
db.Execute(query);
if (db.GetEOF()) return FALSE;
do { db.GetCollect("col", var); } while (db.Next());
```
`OdbcDb` mempertahankan idiom yang sama → kebanyakan call-site **baca** cukup ganti tipe, bukan ditulis ulang.

## Pemetaan `CjADO` → `OdbcDb`
| `CjADO` (ADO/COM) | `OdbcDb` (ODBC) | Status |
| :--- | :--- | :---: |
| `Open(conn)` / `IsOpened()` / `Close()` | sama | ✅ |
| `Execute(query)` | `Execute(sql)` (`SQLExecDirect`) | ✅ |
| `Execute4Cmd(q, adCmdStoredProc)` / `ExecuteStoredProcedure(sp)` | `ExecuteStoredProcedure(sp)` (`EXEC <sp>`) | ✅ |
| `GetEOF()` / `Next()` | sama (forward-only `SQLFetch`) | ✅ |
| `GetCollect("col", T&)` (string/int/__int64) | `GetCollect(name, std::string/int/long long)` (`SQLGetData` by name) | ✅ |
| `AppendIParam*/OParam*/RParam*`, `GetParam` (param & output/return) | — | ⏳ chip berikutnya |
| `GetChunk` (binary BLOB) | — | ⏳ |
| `MoveFirst/Previous/Last`, `Move(idx)` (scrollable) | hanya forward-only | ⏳ |
| generic `GetCollect<T>` via `_variant_t` | diganti akses bertipe | ⏳ (ganti `_variant_t`) |

## Berkas (`ranserver-linux/db/`)
- `odbc_db.h` / `odbc_db.cpp` — kelas `sc::db::OdbcDb` (static lib `ran_db` di CMake).
- `db_smoke.cpp` — uji nyata (target `db_smoke`).

## Build & jalankan
**CMake (dev container, gate `make verify`):** target `ran_db` + `db_smoke` sudah ada di `CMakeLists.txt`; `make verify` membangunnya (tanpa hosted CI).

**Sesi ini** (apt Docker Desktop sedang rusak → pakai image `odbc-spike` yang sudah punya `msodbcsql18`, compile via g++ langsung, terhadap `ranmssql` di network `rannet`):
```bash
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux/db":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanShop" \
  odbc-spike bash -lc \
  'g++ -std=c++17 -Wall /src/odbc_db.cpp /src/db_smoke.cpp -I/src -o /tmp/db_smoke -lodbc && /tmp/db_smoke'
```

---

## Hasil Eksekusi (2026-06-14)
```
OdbcDb.Open OK — core CjADO read-path ported onto ODBC.

[Query] SELECT TOP 3 name, object_id FROM sys.tables ORDER BY name
  row: name=_ShopPurchase_091012  object_id=293576084
  row: name=_ShopPurchase_100121  object_id=277576027
  row: name=AttendLog  object_id=245575913
  [3 rows]

[Stored proc] EXEC dbo.sp_PointShopSelect
  [0 rows]

DB-LAYER SLICE OK: connect + raw query + stored procedure + name-based GetCollect via msodbcsql18.
```

## Catatan teknis
- **Posisi awal**: `Execute()` melakukan satu `SQLFetch` agar kursor berada di baris pertama (meniru ADO yang auto-posisi di record pertama) — sehingga idiom `while(!GetEOF())` bekerja.
- **Urutan kolom**: pada kursor forward-only, ambil kolom **berurutan naik** dalam satu baris (`SQLGetData`); `db_smoke` memanggil `name`(1) lalu `object_id`(2).
- **`GetCollect` by name**: nama kolom dipetakan ke indeks 1-based via `SQLDescribeCol` (case-insensitive), karena ODBC mengambil data per indeks.

## Abstraksi tipe (`win32_compat.h`)
Slice baca ini **murni std + ODBC** (tak butuh tipe Win32). Namun shim diperluas berbasis bukti dari `AdoClass.h` (dipakai modul lain & chip param berikutnya): tambah `LONGLONG`, `ULONGLONG`, `USHORT`, `LPCSTR`, `LPSTR`, `LPCTSTR` — **diverifikasi compile dengan ODBC di Linux**. Ditangguhkan (bukan typedef sederhana): `__int64`, `GUID`, `_variant_t` (yang terakhir diganti akses bertipe, bukan typedef).

## Chip berikutnya (sisa port DB-layer)
> **Judul**: Port parameterized commands `CjADO` → `OdbcDb`
> **IN**: `AppendParam` + keluarga `AppendIParam*/OParam*/RParam*` (int/bigint/money/guid/tinyint/smallint/varchar/image/float), `GetParam`, pemanggilan SP ber-parameter input/output/return (`SQLBindParameter`). Ini menutup jalur **tulis** (mayoritas SP tulis).
> **OUT**: scrollable cursor, `GetChunk` binary, connection pooling.
> **Verifikasi**: panggil satu SP ber-parameter (input + output/return) terhadap DB hasil restore, bandingkan hasil.
> **Gate**: review manusia (menyentuh integritas data) — bukan fire-and-forget.
> **Model & effort**: Opus 4.8 @ `xhigh`.

## Kesimpulan
Modul `SigmaCore` nyata pertama (DB read-path) **ter-port & berjalan** di atas ODBC/Linux — bukti konkret bahwa strategi "pertahankan interface `CjADO`, ganti implementasi ke ODBC" (ADR-001) berfungsi pada kode asli, bukan hanya spike. Pola ini diulang untuk chip param, lalu modul lain, lalu fan-out 5 server.
