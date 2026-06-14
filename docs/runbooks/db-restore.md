# Runbook — Spike #0: Restore `.bak` & Inventarisasi Stored Procedure

> **Tujuan**: Membuktikan asumsi inti [ADR-001](../adr/ADR-001-cloud-native-vs-rejuvenation.md) — bahwa stored procedure T-SQL Ran Online dapat berjalan di **Microsoft SQL Server on Linux** tanpa ditulis ulang. Output runbook ini adalah dua artefak yang menggerbangi ADR-001 (*Proposed → Accepted*) dan membuat [master plan](../06_master_plan.md) konkret:
> - **Artefak A — SP Inventory**: jumlah & daftar pasti stored procedure/function/trigger per database (mengganti perkiraan "1.000+").
> - **Artefak B — Linux Incompatibility Report**: objek yang memakai fitur yang putus/berisiko di SQL Server on Linux.

Runbook ini berjalan **100% lokal** (Docker), tanpa cloud.

> ✅ **Sudah dieksekusi 2026-06-14** — hasil (8/8 DB ONLINE, **866 SP**, incompatibility report **bersih**) tercatat di [ADR-001 §7](../adr/ADR-001-cloud-native-vs-rejuvenation.md#7-validasi--spike-0-2026-06-14).

---

## Prasyarat

- Docker terpasang & berjalan.
- 8 file backup ada di [`database/`](../../database/) — *gitignored*, jadi pastikan ada secara lokal:
  `RanGameS1.bak`, `RanUser.bak`, `RanLogS1.bak`, `RanShop.bak`, `RanMobileInterface.bak`, `WBGame.bak`, `WBLog.bak`, `WBUser.bak`.
- ~4 GB RAM bebas untuk container (minimum SQL Server on Linux = 2 GB).

> ⚠️ **Apple Silicon (M-series)**: image `mcr.microsoft.com/mssql/server` hanya `amd64`. Jalankan dengan `--platform linux/amd64` (emulasi via Rosetta/QEMU — lebih lambat, tetapi cukup untuk spike discovery). Pengguna Intel Mac/Linux bisa abaikan flag ini.

---

## Langkah 1 — Jalankan SQL Server on Linux

```bash
# Ganti password (wajib kompleks: >=8 char, huruf besar+kecil+angka+simbol)
export SA_PASSWORD='YourStrong@Passw0rd'

docker run -d --name ranmssql \
  --platform linux/amd64 \
  -e "ACCEPT_EULA=Y" \
  -e "MSSQL_SA_PASSWORD=$SA_PASSWORD" \
  -p 1433:1433 \
  mcr.microsoft.com/mssql/server:2022-latest

# Tunggu ~20 detik, lalu verifikasi versi
docker exec -it ranmssql /opt/mssql-tools18/bin/sqlcmd \
  -S localhost -U sa -P "$SA_PASSWORD" -C \
  -Q "SELECT @@VERSION;"
```

> `-C` = trust server certificate (wajib untuk `mssql-tools18` di SQL Server 2022).

## Langkah 2 — Salin `.bak` ke dalam container

```bash
docker exec ranmssql mkdir -p /var/opt/mssql/backup
for f in RanGameS1 RanUser RanLogS1 RanShop RanMobileInterface WBGame WBLog WBUser; do
  docker cp "database/$f.bak" "ranmssql:/var/opt/mssql/backup/$f.bak"
done
```

## Langkah 3 — Baca header & daftar file logis tiap backup

Penting untuk: (a) cek **versi SQL Server sumber** — backup 2008 = *compatibility level* 100, batas minimum yang bisa di-restore ke 2022; backup ≤2005 harus lewat 2014/2016 dulu. (b) dapatkan **logical file name** untuk klausa `MOVE`.

```bash
SQLCMD() { docker exec -i ranmssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P "$SA_PASSWORD" -C "$@"; }

# Versi & DatabaseName sumber
SQLCMD -Q "RESTORE HEADERONLY FROM DISK='/var/opt/mssql/backup/RanGameS1.bak';"
# Logical name (kolom LogicalName) untuk data (.mdf) & log (.ldf)
SQLCMD -Q "RESTORE FILELISTONLY FROM DISK='/var/opt/mssql/backup/RanGameS1.bak';"
```

## Langkah 4 — Restore tiap database

Ganti `<DataLogical>`/`<LogLogical>` dengan `LogicalName` dari Langkah 3. Pola untuk satu DB:

```bash
SQLCMD -Q "RESTORE DATABASE [RanGame] FROM DISK='/var/opt/mssql/backup/RanGameS1.bak' \
  WITH MOVE '<DataLogical>' TO '/var/opt/mssql/data/RanGame.mdf', \
       MOVE '<LogLogical>'  TO '/var/opt/mssql/data/RanGame_log.ldf', \
       REPLACE, RECOVERY;"
```

Ulangi untuk 8 backup → nama DB saran: `RanGame`, `RanUser`, `RanLog`, `RanShop`, `RanMobileInterface`, `WBGame`, `WBLog`, `WBUser`.

> Kalau ingin compat level naik ke 2022: `ALTER DATABASE [RanGame] SET COMPATIBILITY_LEVEL = 160;` — **tapi untuk spike, biarkan compat asli dulu** agar perilaku SP identik dengan produksi lama.

Verifikasi:
```bash
SQLCMD -Q "SELECT name, compatibility_level, state_desc FROM sys.databases ORDER BY name;"
```

## Langkah 5 — Artefak A: SP Inventory

Jalankan per database (contoh `RanGame`). Ulangi untuk semua, kumpulkan angkanya.

```sql
-- Rekap jumlah objek programmable
USE RanGame;
SELECT
  SUM(CASE WHEN type IN ('P','PC') THEN 1 ELSE 0 END) AS stored_procedures,
  SUM(CASE WHEN type IN ('FN','IF','TF','FS','FT') THEN 1 ELSE 0 END) AS functions,
  SUM(CASE WHEN type IN ('TR') THEN 1 ELSE 0 END) AS triggers,
  SUM(CASE WHEN type IN ('V') THEN 1 ELSE 0 END) AS views
FROM sys.objects WHERE is_ms_shipped = 0;

-- Daftar lengkap stored procedure (untuk lampiran inventory)
SELECT s.name AS [schema], o.name AS proc_name, o.create_date, o.modify_date
FROM sys.procedures o JOIN sys.schemas s ON s.schema_id = o.schema_id
WHERE o.is_ms_shipped = 0
ORDER BY 1,2;
```

Catat total SP gabungan dari ke-8 DB → **inilah angka pasti** yang menggantikan "1.000+".

## Langkah 6 — Artefak B: Linux Incompatibility Report

Scan definisi semua modul (`sys.sql_modules`) untuk pola fitur yang bermasalah di SQL Server on Linux. Jalankan per DB:

```sql
USE RanGame;
WITH m AS (
  SELECT o.name AS obj, mo.definition
  FROM sys.sql_modules mo JOIN sys.objects o ON o.object_id = mo.object_id
  WHERE o.is_ms_shipped = 0
)
SELECT obj,
  CASE WHEN definition LIKE '%xp_cmdshell%'         THEN 'xp_cmdshell' END                AS f_cmdshell,
  CASE WHEN definition LIKE '%sp_OA%'               THEN 'OLE Automation (sp_OA*)' END     AS f_ole,
  CASE WHEN definition LIKE '%OPENQUERY%'
        OR definition LIKE '%OPENROWSET%'           THEN 'linked server / openquery' END   AS f_linked,
  CASE WHEN definition LIKE '%FILESTREAM%'
        OR definition LIKE '%FileTable%'            THEN 'FILESTREAM/FileTable (NO Linux)' END AS f_filestream,
  CASE WHEN definition LIKE '%CONTAINS(%'
        OR definition LIKE '%FREETEXT%'             THEN 'full-text search' END             AS f_fulltext,
  CASE WHEN definition LIKE '%sp_send_dbmail%'      THEN 'Database Mail' END                AS f_dbmail,
  CASE WHEN definition LIKE '%xp_%'                 THEN 'extended proc (xp_*)' END         AS f_xp
FROM m
WHERE definition LIKE '%xp_cmdshell%' OR definition LIKE '%sp_OA%'
   OR definition LIKE '%OPENQUERY%'   OR definition LIKE '%OPENROWSET%'
   OR definition LIKE '%FILESTREAM%'  OR definition LIKE '%FileTable%'
   OR definition LIKE '%CONTAINS(%'   OR definition LIKE '%FREETEXT%'
   OR definition LIKE '%sp_send_dbmail%' OR definition LIKE '%xp_%';
```

Periksa juga di luar SP:
- **SQL Agent jobs**: `SELECT name, enabled FROM msdb.dbo.sysjobs;` (didukung di Linux tapi terbatas).
- **CLR assemblies**: `SELECT name, permission_set_desc FROM sys.assemblies WHERE is_user_defined = 1;`
- **FILESTREAM di tabel**: `SELECT OBJECT_NAME(object_id), name FROM sys.columns WHERE is_filestream = 1;`
- **Windows/AD auth di kode klien**: cari `Integrated Security=SSPI` / `Trusted_Connection` di `SigmaCore/Database` (SQL Server on Linux default = SQL auth).

| Temuan | Dampak | Mitigasi |
| :--- | :--- | :--- |
| FILESTREAM / FileTable | **Blocking** (tidak ada di Linux) | Redesain ke penyimpanan biasa/objek storage |
| Linked server / OPENQUERY | Berisiko | Ganti pola akses; cek apakah perlu |
| `xp_cmdshell` / `sp_OA*` | Anti-pola | Pindahkan logika ke layer aplikasi |
| Full-text search | Add-on | Pasang paket full-text di image |
| SQL Agent jobs | Terbatas | Pindah ke CronJob K8s bila perlu |
| CLR | Catatan | Aktifkan & uji; pertimbangkan pindah ke app |
| Windows/AD auth | Konfigurasi | Pakai SQL auth + secret manager |

## Langkah 7 — Verifikasi koneksi (jembatan ke spike `msodbcsql`)

Pastikan DB siap dihubungi konektor cross-platform yang akan dipakai server C++ (menggantikan ADO/COM):

```bash
docker exec -it ranmssql /opt/mssql-tools18/bin/sqlcmd \
  -S localhost -U sa -P "$SA_PASSWORD" -C \
  -Q "USE RanUser; SELECT TOP 1 name FROM sys.tables;"
```

Selanjutnya: spike konektor `msodbcsql` dari C++ Linux ke instance ini (lihat [master plan §6](../06_master_plan.md#6-apa-yang-perlu-disiapkan-selanjutnya), langkah 3).

---

## Output & Gate Keputusan

1. **SP Inventory** (Artefak A) → lampirkan ke ADR-001 sebagai bukti volume nyata.
2. **Linux Incompatibility Report** (Artefak B) → daftar temuan + mitigasi.
3. **Tentukan keluarga DB kanonik**: `Ran*` vs `WB*` — mana yang current/dipakai. Periksa `RanShop` (cash shop) & `RanMobileInterface` (mobile) karena relevan langsung ke reimajinasi.

**Gate ADR-001**:
- ✅ *Accept apa adanya* jika **tidak ada** temuan *blocking* (FILESTREAM dll.) atau semua punya mitigasi murah.
- 🔁 *Revisi* (mis. pertimbangkan jalur PostgreSQL Fase 2 lebih awal untuk subsistem tertentu) jika ada blocking berat.

Bersihkan setelah selesai (data masih ada di volume bila container dibuat ulang dengan named volume):
```bash
docker rm -f ranmssql
```
