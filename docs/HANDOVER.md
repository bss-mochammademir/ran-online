# HANDOVER — Modernisasi Ran Online (untuk agen AI penerus, mis. Antigravity)

> **Dokumen ini self-contained.** Agen penerus tidak punya akses ke memori/percakapan sebelumnya. Semua fakta non-obvious diinline di sini. Baca penuh sebelum mulai.
>
> Status per **2026-06-16**. Bahasa dokumen proyek = **Indonesia** (kode/identifier tetap Inggris). Ikuti gaya itu.

---

## 0. TL;DR — di mana kita sekarang

Memodernisasi MMORPG **Ran Online** (sumber C++ Windows ~20 thn, VS2008) agar jalan di **Linux** tanpa menulis ulang logika game. Strategi (ADR-001, **Accepted**): **Hybrid** — port kode C++ ke lintas-platform + jalankan **SQL Server di Linux** (stored procedure T-SQL jalan apa adanya).

**Sudah selesai & ada di `main`:**
- Lapisan **DB** (`sc::db::OdbcDb`) — port `CjADO` (ADO/COM) → ODBC. Jalur **baca** + **tulis** (stored procedure ber-parameter).
- Lapisan **Net** (`sc::net::NetServer`) — port `NetServer` (Win32 IOCP) → `boost::asio`.
- Lapisan **Paket** — framing `NET_MSG_GENERIC`, `MsgManager` double-buffer, `Session` async read.
- **5 server** ter-fan-out di Linux: **Auth, Login, Session, Agent, Field** (skeleton lifecycle + pipeline paket; handler pesan spesifik belum).
- **1 irisan vertikal nyata**: AuthServer cert flow (`ProcessCertificationForAuth` → `sp_CertificationUniqKey` end-to-end).

**`main` bersih** (semua PR #1–#18 merged). Tidak ada PR terbuka. Mulai chip baru dengan branch dari `main`.

**Langkah berikutnya** = isi tiap server dengan handler pesan nyata (irisan vertikal per fitur). Lihat §6.

---

## 1. Konteks proyek (kenapa & batasan)

- **Tujuan utama**: kendaraan belajar Enterprise Architecture + reborn Ran Online sebagai game mobile. Ekonomi player-positive, anti-judi, digerakkan indeks pasar.
- **Kepatuhan**: data residency Indonesia (**POJK 11/2022**) — pertimbangkan saat memilih infra.
- **5 pilar desain** ada di `docs/00_design_pillars.md` (**baca pertama**). Pilar 4 = OpEx rendah. Prinsip A2 = *cloud-exit* (minimalkan ketergantungan vendor/brand).
- **Topologi server asli**: Login (lobby/direktori) → Agent (gateway client + proxy) ↔ Field (engine gameplay/Gaea) ; Session (koordinator state global) ; Auth (sertifikasi akun lintas-region). Detail: `docs/02_server_components/`.

---

## 2. Arsitektur kode yang sudah jadi

Semua kode Linux ada di **`ranserver-linux/`** (satu-satunya direktori non-docs yang di-track git; sisanya gitignored).

```
ranserver-linux/
├── CMakeLists.txt          # semua target build
├── Makefile                # `make verify` (lihat catatan §3 — apt rusak)
├── Dockerfile.dev          # image dev apt-based (GPG RUSAK di Docker Desktop ini)
├── platform/win32_compat.h # shim tipe Win32 (DWORD/LONGLONG/dst) untuk Linux
├── db/
│   ├── odbc_db.{h,cpp}      # sc::db::OdbcDb — port CjADO → ODBC (baca + tulis)
│   ├── db_smoke.cpp         # smoke jalur baca
│   └── db_param_smoke.cpp   # smoke jalur tulis (SP ber-parameter) — 9/9
├── net/
│   ├── net_server.{h,cpp}   # sc::net::NetServer — IOCP → boost::asio lifecycle base
│   ├── packet.{h,cpp}       # EncodeMessage / MessageFramer (header 8-byte NET_MSG_GENERIC)
│   ├── msg_manager.h        # MsgManager double-buffer (Front/Back/Flip) header-only
│   ├── session.{h,cpp}      # Session async read loop per-koneksi
│   └── packet_smoke.cpp     # 22/22 unit framing + MsgManager
└── servers/
    ├── authserver/   auth_server.{h,cpp} main.cpp authserver_smoke.cpp  # + cert flow nyata
    ├── loginserver/  login_server.{h,cpp} main.cpp loginserver_smoke.cpp
    ├── sessionserver/session_server.{h,cpp} main.cpp sessionserver_smoke.cpp
    ├── agentserver/  agent_server.{h,cpp} main.cpp agentserver_smoke.cpp
    └── fieldserver/  field_server.{h,cpp} main.cpp fieldserver_smoke.cpp
```

### Pemetaan port (asli → Linux)
| Konsep Windows | Port Linux |
| :--- | :--- |
| `CjADO` (ADO/COM, `SigmaCore/Database/Ado`) | `sc::db::OdbcDb` (unixODBC + `msodbcsql18`) |
| ADO `{ ? = call sp(?,?) }` direction-routing | `SQLBindParameter`; RETURN_VALUE selalu marker #1 |
| IOCP + `m_hWorkerThread[]` | `io_context` + thread pool |
| accept thread / `ListenProc` | `tcp::acceptor` + `async_accept` → `OnAccept` |
| update/schedule thread | `steady_timer` → `OnUpdate` / `OnRegularSchedule` |
| `WinMain` + dialog MFC (Start/Stop/tray) | `main()` + `SIGINT`/`SIGTERM` → `Stop()` |
| `NET_MSG_GENERIC{DWORD dwSize; EMNET_MSG nType;}` | header **8 byte** little-endian (dwSize = TOTAL incl header) |
| `MsgList` double-buffer | `MsgManager` (semua op di-lock; tick jalan di asio pool) |

### Bentuk lifecycle tiap server (identik)
```
OnStart           : buka DB (OdbcDb.Open) + probe query → buka koneksi tambahan (stub)
OnAccept(socket)  : buat Session → simpan di m_sessions; framed msg → MsgManager.MsgQueueInsert
OnUpdate          : MsgQueueFlip → drain GetMsg → dispatch(msg)
OnRegularSchedule : stub log (sync/heartbeat/memory guard)
OnStop            : tutup DB + resource
dispatch          : route by msg.type; default = ECHO balik (sampai handler di-port)
```
Satu-satunya handler nyata sejauh ini: `AuthServer::dispatch` → `kMsgAuthCertReq` → `ProcessCertificationForAuth`.

---

## 3. Cara build, jalankan, verifikasi (PALING PENTING)

### Environment yang sudah ada di mesin ini
| Komponen | Detail |
| :--- | :--- |
| **SQL Server** | container `ranmssql` (`mcr.microsoft.com/mssql/server:2022-latest`), jalan, di network `rannet`, port 1433 |
| **Image build** | `ranlinux-dev` (`linux/amd64`) = `odbc-spike` + header boost 1.90 host via `docker cp`. **Punya boost + ODBC**, asio header-only (tak perlu link). **Pakai ini untuk semua build server.** |
| **Network** | `rannet` (bridge) — wajib `--network rannet` agar bisa resolve `ranmssql,1433` |
| **Password SA** | di `/tmp/ran_sa_pw.txt` (dev throwaway: lokal saja, **jangan commit**) |

### ⚠️ `make verify` vs jalur yang terbukti
`Makefile` target `verify` membangun `Dockerfile.dev` (apt-based) → **apt/GPG RUSAK di Docker Desktop environment ini** (masalah env, bukan kode). Itu juga hanya menjalankan `foundation_smoke`, bukan smoke server.

**Jalur verifikasi yang TERBUKTI** = compile manual `g++` di `ranlinux-dev`. Contoh menjalankan smoke satu server (live DB):
```bash
SA=$(cat /tmp/ran_sa_pw.txt)
REPO="<path>/ranserver-linux"
docker run --rm --platform linux/amd64 --network rannet \
  -v "$REPO":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanUser" \
  ranlinux-dev bash -lc '
    INC="-I/src/net -I/src/db"
    COMMON="/src/net/net_server.cpp /src/net/packet.cpp /src/net/session.cpp /src/db/odbc_db.cpp"
    g++ -std=c++17 -Wall -Wextra $COMMON \
      /src/servers/loginserver/login_server.cpp \
      /src/servers/loginserver/loginserver_smoke.cpp \
      $INC -I/src/servers/loginserver -lodbc -lpthread -o /tmp/smoke && /tmp/smoke'
```
- Ganti path server untuk smoke lain. **FieldServer** pakai `DB_NAME=RanGameS1` (lihat §4 — kritis).
- Tanpa env `SA_PASSWORD`/`DB_SERVER` → smoke jalan **lifecycle-only** (assertion DB di-skip) — berguna untuk uji cepat tanpa DB.
- AuthServer cert smoke butuh live DB (membuat SP stand-in lalu drop).

### Hasil terverifikasi terakhir
- `db_param_smoke`: 9/9 ; `packet_smoke`: 22/22 ; `authserver_smoke` (+cert): 19 assertion ; fan-out 5 server: **52/52** (lifecycle + live DB).

### Saran perbaikan (opsional, bila mau gate CMake penuh)
Buat target smoke server di CMake bisa di-build via `ranlinux-dev` (bukan `Dockerfile.dev`). Atau perbaiki apt/GPG di `Dockerfile.dev`. Sampai itu, jalur manual di atas adalah kanon.

---

## 4. ⚠️ GOTCHA KRITIS — nama database harus pakai suffix `S1`

DB hasil restore **WAJIB** bernama persis nama produksi asli: **`RanGameS1`** dan **`RanLogS1`** (dengan `S1`) — **JANGAN** dibersihkan jadi `RanGame`/`RanLog`.

**Alasan**: beberapa SP/view hardcode nama `...S1` lewat **three-part naming** cross-database. Rename memecahkan SP **diam-diam saat runtime** (bukan saat restore):
- `RanShop.dbo.sp_sumGameMoneyOfHour` → `RanGameS1.dbo.ChaInfo` / `.UserInven` / `.GuildInfo`
- `RanLogS1.dbo.job_RanGamePostBox` → `RanGameS1.dbo.PostInfo`
- `RanGameS1.dbo.sp_parseChaSkillsAndSendBank` → `RanLogS1.dbo.sp_purchase_insert_item`

8 DB hasil restore: **`RanGameS1`, `RanUser`, `RanLogS1`, `RanShop`, `RanMobileInterface`, `WBGame`, `WBLog`, `WBUser`** (hanya Game & Log yang ber-suffix S1). Sebelum rename DB apa pun, scan dulu:
```sql
SELECT o.name FROM <DB>.sys.sql_modules m JOIN <DB>.sys.objects o ON m.object_id=o.object_id
WHERE m.definition LIKE '%RanGameS1%' OR m.definition LIKE '%RanLogS1%';
```
Detail lengkap: `docs/runbooks/db-restore.md` §Langkah 4.

**Gap terpisah** (bukan akibat naming): `RanLogS1.dbo.sp_purchase_insert_item` direferensikan tapi **tidak ada di backup** — pola sama dengan `sp_CertificationUniqKey` yang tinggal di **Global Auth DB** terpisah (di luar 8 .bak). Jangan kaget bila SP tertentu hilang; cek apakah ia milik DB yang belum di-restore.

---

## 5. Aturan & guardrail WAJIB (jangan dilanggar)

1. **Tidak ada hosted CI** (keputusan final). GitHub Actions sudah dihapus total. Gate = build lokal. Kalau perlu CI, harus portable cloud-build, bukan brand-specific. (Pilar 4 + A2.)
2. **`.bak` & secret tidak di-commit.** File `.bak` (di `database/`) gitignored, lokal saja. Password SA dev di `/tmp` — jangan masuk kode. Produksi → secret manager.
3. **Human gate** untuk perubahan: integritas data/ekonomi, parity combat, ADR, schema/event, **keamanan/auth/kripto**. Jangan auto-merge hal ini — minta keputusan user.
4. **Aturan eksekusi**: *serial shared-layer dulu → baru fan-out 5 server*. Shared-layer (db/net/paket) + fan-out **sudah selesai**. Sekarang fase **irisan vertikal per fitur** (boleh paralel antar-server).
5. **Minimalkan vendor lock-in** (A2). Pilih lokal/portable di atas hosted. Justifikasi tooling infra terhadap pilar 4 + A2 + analisis biaya.
6. **Docs bahasa Indonesia**, gaya: diagram Mermaid, link `file:///` ke sumber Windows. Tiap chip → tulis runbook di `docs/runbooks/` + update `docs/runbooks/` index.
7. **Pola commit**: branch dari `main`, satu chip = satu branch + PR ke `main` (JANGAN stack PR ke branch antara — itu pernah bikin #13/#14 nyangkut, lihat §7). Sertakan hasil smoke di body PR.
8. **Verifikasi nyata, lapor apa adanya.** Tiap port diuji terhadap `ranmssql` nyata + smoke. Kalau gagal/di-skip, katakan.

---

## 6. Backlog / chip berikutnya (dengan rekomendasi model)

Pola rekomendasi model: tugas mekanik/replikasi → **Sonnet**; tugas correctness-critical/keamanan/protokol → **Opus** (xhigh). Rekomendasikan model tugas BERIKUTNYA saat menutup tugas (agar user sempat switch).

| # | Chip | Isi | Gate | Model |
| :-- | :--- | :--- | :--- | :--- |
| A | **Handler login Agent** | `IdnMsgLogIn` dll. — validasi kredensial via DB (irisan vertikal AgentServer pertama) | 🔒 manusia (auth) | Opus xhigh |
| B | **Cert nyata penuh** | `GlobalAuthManager` (kripto `szAuthData`→`SAuthData`) + struktur wire biner `NET_AUTH_CERTIFICATION_REQUEST/ANS` (sekarang pakai payload teks `a;b;c`) | 🔒 manusia (keamanan) | Opus xhigh |
| C | **Envelope LZO** | `NET_COMPRESS` + LZO (`SigmaCore/Compress/CMinLzo`, `CSendMsgBuffer`) — kompresi batch <1000B. (doc 04 salah sebut zlib; aslinya **LZO**) | — | Opus |
| D | **msgpack + CRC** | `NET_MSG_GCTRL` — `SendCountryAccessApprove` + pesan kontrol | — | Opus |
| E | **Sync Login↔Session** | `NET_MSG_REQ_GAME_SVR`/`SND_GAME_SVR` — daftar channel/beban | — | Sonnet |
| F | **Item DB deferred** | tipe `money`/`adCurrency` (skala 1/10000, CjADO kirim raw `__int64`), GUID, image/`adLongVarBinary`, `GetChunk` (BLOB), scrollable cursor, connection pooling per-worker | data-integrity | Opus utk money |
| G | **Config XML** | `AuthServerConfigXml` dst. — ganti argv/env dengan loader config nyata; di sinilah keputusan nama DB (`RanGameS1`) ketemu config produksi | — | Sonnet |

Saran urutan: **A atau B** (irisan vertikal bernilai tinggi, butuh keputusan keamanan user) → lalu C/D (protokol, untuk kompat klien penuh) → E/G (mekanik).

---

## 7. Pelajaran/jebakan yang sudah ketemu (jangan ulangi)

- **PR stacking**: PR #13/#14 sempat di-set base ke branch antara (`feat/db-write-path`, `feat/server-fanout-authserver`) dan merge ke sana, bukan `main` → `main` ketinggalan commit. Dikonsolidasi lewat #15. **Selalu base PR ke `main`.**
- **DB naming `S1`**: lihat §4. Pernah salah rename → SP cross-db pecah diam-diam. Sudah dikoreksi.
- **doc 04 (`04_network_protocol.md`) salah di 2 hal**: (1) header `NET_MSG_GENERIC` itu **8 byte** (DWORD dwSize TOTAL + EMNET_MSG nType uint32), bukan 4; (2) kompresi **LZO**, bukan zlib. Kode sudah benar; doc 04 belum tentu diperbaiki.
- **Cert request itu BINER** (`NET_AUTH_CERTIFICATION_REQUEST`), bukan msgpack. msgpack hanya di balasan country-access.
- **Image boost+ODBC**: apt/GPG rusak di Docker Desktop → `ranlinux-dev` dibuat dengan `docker cp` header brew boost ke `odbc-spike`. Saat `docker cp` brew (symlink) gagal, resolve dulu path asli: `python3 -c "import os;print(os.path.realpath('/opt/homebrew/include/boost'))"`.
- **ranmssql kadang Paused** → `docker unpause ranmssql`.

---

## 8. Referensi kunci (baca sesuai kebutuhan)

| File | Isi |
| :--- | :--- |
| `docs/00_design_pillars.md` | **baca pertama** — north-star, 5 pilar, ekonomi, monetisasi |
| `docs/08_source_tree_map.md` | **peta lengkap source (anti-kuncen)** — client+server+engine+tools+lib; apa di-port vs belum; batas git; implikasi codec/LZO |
| `docs/06_master_plan.md` | payung arsitektur EA→SA→TA + roadmap Fase 0–5 |
| `docs/07_ai_delivery_operating_model.md` | model operasi tim-agen AI: kapan "chip", mode eksekusi, autonomy ladder, aturan serial→fan-out |
| `docs/adr/ADR-001-*.md` | keputusan Hybrid (SQL Server di Linux + C++ port) |
| `docs/03_database_schema.md` | skema `ran_user`/`ran_game`/`ran_log`, akses ADO, mapping tipe |
| `docs/04_network_protocol.md` | protokol Winsock (⚠️ 2 koreksi di §7) |
| `docs/02_server_components/*` | peran tiap server (login/session/field/agent) |
| `docs/runbooks/db-restore.md` | restore 8 .bak ke SQL Server Linux + ⚠️ aturan naming S1 |
| `docs/runbooks/msodbcsql-spike.md` | Spike #1 — konektor ODBC tervalidasi |
| `docs/runbooks/asio-spike.md` | Spike #2 — asio loop tervalidasi |
| `docs/runbooks/build-foundation.md` | scaffold CMake + keputusan gate lokal |
| `docs/runbooks/db-layer-port.md` | port OdbcDb (baca + tulis) — gotcha ODBC param |
| `docs/runbooks/server-fanout-authserver.md` | fan-out #1 AuthServer (IOCP→asio mapping detail) |
| `docs/runbooks/net-packet-layer.md` | lapisan paket (framing, double-buffer) |
| `docs/runbooks/auth-cert-flow.md` | irisan vertikal cert AuthServer |
| `docs/runbooks/server-fanout-remaining.md` | fan-out #2–5 Login/Session/Agent/Field |
| `docs/runbooks/agent-login-handler.md` | Handler Login Agent (Chip A) |
| `docs/runbooks/auth-cert-crypto.md` | Kriptografi Sertifikasi Penuh (Chip B) |

---

## 9. Verifikasi cepat saat mulai (sanity check)
```bash
# 1. main bersih & lengkap?
git -C <repo> log --oneline -3 origin/main          # harus ada merge #17/#18
git -C <repo> ls-tree -d --name-only origin/main:ranserver-linux/servers  # 5 server

# 2. DB hidup & nama benar?
docker exec ranmssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa \
  -P "$(cat /tmp/ran_sa_pw.txt)" -No \
  -Q "SELECT name FROM sys.databases WHERE name LIKE 'Ran%' OR name LIKE 'WB%' ORDER BY name"
  # harus muncul RanGameS1 & RanLogS1 (BUKAN RanGame/RanLog)

# 3. smoke lifecycle-only cepat (tanpa DB) — lihat §3
```

Selamat melanjutkan. Pegang teguh §4 (naming S1) dan §5 (guardrail). Mulai dari chip A atau B di §6.
