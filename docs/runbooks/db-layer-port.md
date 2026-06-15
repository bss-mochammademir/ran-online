# Runbook — Port DB-Layer: `CjADO` (ADO/COM) → `OdbcDb` (ODBC), jalur baca + tulis

> **Tujuan**: Mem-port **jalur baca inti** kelas Windows ADO/COM `sc::db::CjADO` ([`SigmaCore/Database/Ado/AdoClass.h`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Database/Ado/AdoClass.cpp)) ke implementasi **ODBC cross-platform** (`sc::db::OdbcDb`, `msodbcsql18` — divalidasi [Spike #1](msodbcsql-spike.md)). Ini **modul `SigmaCore` nyata pertama** yang di-port (sisi DB = A2 di [doc 07 §5](../07_ai_delivery_operating_model.md#5-pola-paralelisasi-pada-roadmap-ini-contoh-konkret)).

> ✅ **Jalur baca dieksekusi 2026-06-14** — `db_smoke` connect + query + stored proc + GetCollect **hijau** terhadap DB nyata. Lihat [§Hasil](#hasil-eksekusi-2026-06-14).
>
> ✅ **Jalur tulis dieksekusi 2026-06-15** — `db_param_smoke` (stored procedure ber-parameter: input + output + return via `SQLBindParameter`) **9/9 assertion hijau**, residu nol (proc uji dibuat & di-drop). Lihat [§Jalur tulis](#jalur-tulis-parameterized-commands-2026-06-15).

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
| `AppendIParam*/OParam*/RParam*` (int/bigint/tinyint/smallint/varchar/float) + `GetParam`, SP ber-param input/output/return | `AppendIParam*/OParam*/RParam*` + `GetParam` (`SQLBindParameter` + `{? = call sp(?,?)}`) | ✅ |
| `AppendIParam*` **money/GUID/image** (`adCurrency`/`adGUID`/`adLongVarBinary`) | — | ⏳ (fidelity: scaling/biner) |
| `GetChunk` (binary BLOB) | — | ⏳ |
| `MoveFirst/Previous/Last`, `Move(idx)` (scrollable) | hanya forward-only | ⏳ |
| generic `GetCollect<T>` via `_variant_t` | diganti akses bertipe | ⏳ (ganti `_variant_t`) |

## Berkas (`ranserver-linux/db/`)
- `odbc_db.h` / `odbc_db.cpp` — kelas `sc::db::OdbcDb` (static lib `ran_db` di CMake).
- `db_smoke.cpp` — uji jalur **baca** (target `db_smoke`).
- `db_param_smoke.cpp` — uji jalur **tulis**: SP ber-parameter, self-contained & residu-nol (target `db_param_smoke`).

## Build & jalankan
**CMake (dev container, gate `make verify`):** target `ran_db` + `db_smoke` + `db_param_smoke` sudah ada di `CMakeLists.txt`; `make verify` membangunnya (tanpa hosted CI).

**Sesi ini** (apt Docker Desktop sedang rusak → pakai image `odbc-spike` yang sudah punya `msodbcsql18`, compile via g++ langsung, terhadap `ranmssql` di network `rannet`):
```bash
# Jalur baca
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux/db":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanShop" \
  odbc-spike bash -lc \
  'g++ -std=c++17 -Wall /src/odbc_db.cpp /src/db_smoke.cpp -I/src -o /tmp/db_smoke -lodbc && /tmp/db_smoke'

# Jalur tulis (SP ber-parameter) — sama, ganti db_smoke.cpp -> db_param_smoke.cpp
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux/db":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanShop" \
  odbc-spike bash -lc \
  'g++ -std=c++17 -Wall -Wextra /src/odbc_db.cpp /src/db_param_smoke.cpp -I/src -o /tmp/db_param_smoke -lodbc && /tmp/db_param_smoke'
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

## Jalur tulis (parameterized commands, 2026-06-15)
`db_param_smoke` membuat 2 stored procedure uji (`CREATE OR ALTER`, lalu `DROP`) lalu memanggilnya lewat API write yang baru:
```
[SP+params] dbo.sp__odbc_param_smoke (int/varchar/tinyint/smallint/float in; int out; int return)
  PASS input int  (i_echo) => 21
  PASS input char (l_echo) => hello
  PASS input tiny (t_echo) => 200
  PASS input small(s_echo) => 30000
  PASS input float(f_echo) bound => 3.5
  PASS OUTPUT param (@o_int = 2*in) => 42
  PASS RETURN value (in + 100) => 121

[SP+params] dbo.sp__odbc_param_smoke2 (bigint in/out, no result set)
  PASS BIGINT OUTPUT (5e9 * 1e6) => 5000000000000000
  PASS RETURN value => 7

DB-WRITE-PATH SLICE OK: parameterized SP exec (input + output + return) via SQLBindParameter.
```

**Pemetaan semantik ADO → ODBC (write):**
| ADO (`CjADO`) | ODBC (`OdbcDb`) |
| :--- | :--- |
| `m_pCommand->CreateParameter(...)` + `Parameters->Append` (akumulasi) | akumulasi `m_params` (vektor `Param`) |
| binding **posisional** mengikuti urutan `Append` | `?` posisional mengikuti urutan `Append` |
| `adParamReturnValue` (di-route ke RETURN by direction) | jadi `?` pertama di escape `{ ? = call sp(?,?) }` |
| `Execute4Cmd(sp, adCmdStoredProc)` | `SQLPrepare("{?=call sp(...)}")` + `SQLBindParameter` + `SQLExecute` |
| `GetParam(name)` → `Parameters->GetItem(name)->Value` | `GetParam(name, out)` baca balik dari buffer terikat |

**Catatan teknis jalur tulis (penting untuk tim):**
- **Output/return param hanya valid setelah seluruh result set dikuras.** Aturan ODBC: nilai output baru ter-isi setelah `SQLMoreResults` habis. `GetParam` otomatis menguras (`drainResults`) saat dipanggil pertama, jadi pola `while(!GetEOF()){GetCollect;Next;}` lalu `GetParam(...)` aman.
- **Return value lewat `{ ? = call ... }`**: param `RETURN_VALUE` selalu di-bind sebagai marker #1 (terlepas dari urutan append), meniru routing-by-direction milik ADO. Param input/output lain mengikuti urutan append sebagai marker #2..N.
- **Buffer stabil**: tiap `Param` memegang `std::vector<char> buf` + `SQLLEN ind` sendiri; `SQLBindParameter` mengikat **by reference**, jadi alamatnya harus hidup sampai setelah `SQLExecute` & pembacaan output. `m_params` tak di-resize antara bind dan execute.
- **`SET NOCOUNT ON`** di SP uji mencegah result set "rows affected" liar yang akan mengacaukan pengurasan.
- **Auto-clear**: `Append*Param` pertama setelah sebuah execute otomatis mengosongkan set param sebelumnya (atau panggil `ClearParams()` eksplisit).

## Catatan teknis
- **Posisi awal**: `Execute()` melakukan satu `SQLFetch` agar kursor berada di baris pertama (meniru ADO yang auto-posisi di record pertama) — sehingga idiom `while(!GetEOF())` bekerja.
- **Urutan kolom**: pada kursor forward-only, ambil kolom **berurutan naik** dalam satu baris (`SQLGetData`); `db_smoke` memanggil `name`(1) lalu `object_id`(2).
- **`GetCollect` by name**: nama kolom dipetakan ke indeks 1-based via `SQLDescribeCol` (case-insensitive), karena ODBC mengambil data per indeks.

## Abstraksi tipe (`win32_compat.h`)
Slice baca ini **murni std + ODBC** (tak butuh tipe Win32). Namun shim diperluas berbasis bukti dari `AdoClass.h` (dipakai modul lain & chip param berikutnya): tambah `LONGLONG`, `ULONGLONG`, `USHORT`, `LPCSTR`, `LPSTR`, `LPCTSTR` — **diverifikasi compile dengan ODBC di Linux**. Ditangguhkan (bukan typedef sederhana): `__int64`, `GUID`, `_variant_t` (yang terakhir diganti akses bertipe, bukan typedef).

## Chip berikutnya (sisa port DB-layer)
Jalur baca **dan** tulis scalar sudah tertutup. Sisa, urut prioritas:
> **A. Param tipe khusus** — `money`/`adCurrency` (perlu keputusan skala 1/10000 — `CjADO` mengoper `__int64` mentah, perlu bukti call-site + tipe kolom SP), `GUID`/`adGUID`, `image`/`adLongVarBinary`. **Butuh fidelity test sendiri** → ditangguhkan sengaja (bukan sekadar type-swap).
> **B. `GetChunk`** (BLOB biner) + scrollable cursor (`MoveFirst/Previous/Last`).
> **C. Connection pooling** (`CADOManager::EnableConnectionPooling` → atribut pooling unixODBC / driver).
>
> **Gate**: review manusia (menyentuh integritas data). **Model & effort**: Opus 4.8 @ `xhigh`.
>
> Setelah DB-layer tuntas: lanjut modul `SigmaCore` lain, lalu **fan-out 5 server** (aturan "serial dulu, baru fan-out", [doc 07 §5](../07_ai_delivery_operating_model.md)).

## Kesimpulan
Modul `SigmaCore` nyata pertama — **jalur baca dan tulis DB** — kini **ter-port & berjalan** di atas ODBC/Linux: `Execute`/`GetCollect` (baca) dan `AppendIParam*/OParam*/RParam*` + `GetParam` (tulis, SP ber-parameter via `SQLBindParameter`), keduanya hijau terhadap DB hasil restore. Bukti konkret bahwa strategi "pertahankan interface `CjADO`, ganti implementasi ke ODBC" (ADR-001) berfungsi pada kode asli, bukan hanya spike. Pola ini diulang untuk modul lain, lalu fan-out 5 server.
