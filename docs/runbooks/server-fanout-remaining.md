# Runbook — Fan-out Server #2–5: Login / Session / Agent / Field

> **Tujuan**: Replikasi template fan-out AuthServer ke 4 server sisanya — masing-masing mendapatkan skeleton `sc::net::NetServer` + `sc::db::OdbcDb` + `main()` headless + smoke. Setelah chip ini, **semua 5 server** berjalan di Linux dengan pipeline paket lengkap.

> ✅ **Sudah dieksekusi 2026-06-16** — 52/52 assertion hijau (lifecycle-only + live SQL Server). Lihat [§Hasil](#hasil-eksekusi-2026-06-16).

---

## Pemetaan: server asli → port Linux

| Server | Kelas asli | Port default | DB utama | Fitur stub (deferred) |
| :--- | :--- | :---: | :--- | :--- |
| **LoginServer** | `CLoginServer` | 9200 | RanUser | GeoIP filter, version check, channel-list sync dari SessionServer, `NET_MSG_REQ_GAME_SVR` handler |
| **SessionServer** | `CSessionServer` | 9401 | RanUser | Pelacakan karakter (MsgChaIncrease/Decrease), chat relay (MsgChatProcess/SndChatGlobal), heartbeat beban channel, distribusi perintah GM |
| **AgentServer** | `CAgentServer` | 9301 | RanUser | Login regional (IdnMsgLogIn dll.), koneksi Field Server pool (`FieldConnectAll`), routing paket (SendField/SendFieldSvr), anti-cheat nProtect |
| **FieldServer** | `s_CFieldServer` + `GLGaeaServer` | 9501 | **RanGame** | Gaea Engine (combat/AI/peta), CacheServer slot (tulis async DB), instance map, memory auto-restart guard |

Semua mengikuti **bentuk lifecycle** yang persis sama dengan AuthServer:
```
OnStart: buka DB + probe query → buka koneksi tambahan (stub)
OnAccept: buat Session → tambah ke m_sessions
OnUpdate: MsgQueueFlip → drain → dispatch (echo sementara)
OnRegularSchedule: stub log (sync / heartbeat / memory guard)
OnStop: tutup DB + resource lainnya
```

---

## Perbedaan arsitektur yang relevan

- **SessionServer** hanya menerima koneksi dari server lain (Login/Agent/Field), bukan dari client langsung — tapi pipeline paket sama.
- **AgentServer** menyambungkan diri *keluar* ke semua Field Server saat startup (`FieldConnectAll` → stub di `OnStart`).
- **FieldServer** hanya menerima koneksi dari AgentServer (bukan client langsung). DB utama = **RanGame** (data karakter: inventory, skill, quest) — berbeda dari 3 server lainnya (RanUser).
- **FieldServer** di produksi *menulis* lewat CacheServer (antrean async), bukan langsung ke DB — ditangguhkan.

---

## Berkas

```
ranserver-linux/servers/
├── loginserver/    login_server.{h,cpp}  main.cpp  loginserver_smoke.cpp
├── sessionserver/  session_server.{h,cpp} main.cpp sessionserver_smoke.cpp
├── agentserver/    agent_server.{h,cpp}  main.cpp  agentserver_smoke.cpp
└── fieldserver/    field_server.{h,cpp}  main.cpp  fieldserver_smoke.cpp
```

---

## Build & jalankan (`ranlinux-dev`)

```bash
SA=$(cat /tmp/ran_sa_pw.txt)
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" \
  ranlinux-dev bash -lc '
    INC="-I/src/net -I/src/db"
    COMMON="/src/net/net_server.cpp /src/net/packet.cpp /src/net/session.cpp /src/db/odbc_db.cpp"
    for s in loginserver sessionserver agentserver; do
      g++ -std=c++17 $COMMON /src/servers/$s/*_server.cpp /src/servers/$s/${s}_smoke.cpp \
          $INC -I/src/servers/$s -lodbc -lpthread -o /tmp/${s}_smoke
      DB_NAME=RanUser /tmp/${s}_smoke
    done
    g++ -std=c++17 $COMMON /src/servers/fieldserver/field_server.cpp \
        /src/servers/fieldserver/fieldserver_smoke.cpp \
        $INC -I/src/servers/fieldserver -lodbc -lpthread -o /tmp/fieldserver_smoke
    DB_NAME=RanGame /tmp/fieldserver_smoke'
```

---

## Hasil Eksekusi (2026-06-16)

**Lifecycle-only (tanpa DB) — 44/44:**
```
LOGINSERVER SMOKE OK: asio lifecycle + framed packet pipeline.
SESSIONSERVER SMOKE OK: asio lifecycle + framed packet pipeline.
AGENTSERVER SMOKE OK: asio lifecycle + framed packet pipeline.
FIELDSERVER SMOKE OK: asio lifecycle + framed packet pipeline.
```

**Live SQL Server (ranmssql) — 52/52:**
```
LoginServer:   probe row name=AccountInfo (RanUser)   → 13/13 PASS
SessionServer: probe row name=AccountInfo (RanUser)   → 13/13 PASS
AgentServer:   probe row name=AccountInfo (RanUser)   → 13/13 PASS
FieldServer:   probe row name=ActivityClosed (RanGame) → 13/13 PASS
```

Setiap smoke: Start → DB connect (OdbcDb) → terima koneksi TCP nyata → 3 frame ber-frame (termasuk split) → echo balik decode → Stop.

---

## Catatan teknis

- **RanGame** vs **RanUser**: hanya FieldServer yang membuka DB berbeda. `DB_NAME=RanGame` dibutuhkan saat jalankan smoke dengan env DB.
- **FieldServer probe**: `ActivityClosed` adalah tabel pertama dalam urutan alfabet di `RanGame`.
- **Env vars**: `LOGIN_PORT`, `SESSION_PORT`, `AGENT_PORT`, `FIELD_PORT` — masing-masing default ke port asli.
- **Semua dispatch echo**: hingga handler pesan di-port, semua tipe pesan di-echo kembali ke pengirim agar pipeline tetap terobservasi.

## Chip berikutnya

> **A. Handler login Agent** (`IdnMsgLogIn` — validasi kredensial via DB) → irisan vertikal Agent. **Opus**, gate manusia (autentikasi).
> **B. `GlobalAuthManager` + wire struct** cert nyata → lanjutan dari AuthServer cert flow. **Opus**.
> **C. Envelope LZO** (`NET_COMPRESS`/`CMinLzo`) → kompat klien. **Opus**.
> **D. Sinkronisasi beban Login↔Session** (`NET_MSG_REQ_GAME_SVR` / `NET_MSG_SND_GAME_SVR`) → irisan vertikal Login+Session. **Sonnet**.

## Kesimpulan

Semua **5 server** kini memiliki skeleton Linux yang identik: `sc::net::NetServer` (asio) + `sc::db::OdbcDb` + `main()` headless + smoke terverifikasi. Tujuan "*serial shared-layer dulu → lalu fan-out 5 server*" dari doc 07 **selesai** untuk lapisan dasar. Langkah berikutnya adalah mengisi masing-masing server dengan handler pesan nyata (irisan vertikal per fitur).
