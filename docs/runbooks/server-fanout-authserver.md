# Runbook — Fan-out Server #1: AuthServer di Linux (`NetServer` IOCP→asio + `OdbcDb`)

> **Tujuan**: Membuktikan **port satu server utuh** berjalan di Linux di atas lapisan shared yang sudah di-port (tipe + `boost::asio` + `OdbcDb`). Server pertama = **AuthServer** (paling kecil/sederhana). Sekaligus mem-port **`NetServer`** — basis IOCP yang **diwarisi SEMUA server** (`class CAuthServer : public NetServer`) — ke `boost::asio`, sehingga jadi **template fan-out** untuk 4 server sisanya.

> ✅ **Sudah dieksekusi 2026-06-15** — `authserver_smoke` **9/9 assertion hijau** terhadap DB nyata (`RanUser`, probe row `name=AccountInfo`). Lihat [§Hasil](#hasil-eksekusi-2026-06-15).

---

## Konteks: kenapa sekarang fan-out
Lapisan shared sudah komplet & tervalidasi: **tipe** ([build-foundation](build-foundation.md)) + **jaringan** ([asio Spike #2](asio-spike.md)) + **DB baca & tulis** ([db-layer-port](db-layer-port.md)). Sesuai aturan proyek *"serial shared-layer dulu → baru fan-out"* ([doc 07 §5](../07_ai_delivery_operating_model.md)), ini langkah per-server pertama.

## Arsitektur asli (Windows)
- **EXE tipis** (`AuthServer/AuthServer.cpp`): `WinMain` + dialog MFC (tombol Start/Stop, tray icon) yang membungkus `CAuthServer* g_pServer`.
- **`CAuthServer : public NetServer`** (`RanLogicServer/Server/AuthServer.{h,cpp}`, 98h/291cpp). `NetServer` (`RanLogicServer/Server/NetServer.{h,cpp}`, 323h/637cpp) = **mesin IOCP**: worker thread `m_hWorkerThread[]`, accept thread, update thread, regular-schedule thread, `SOCKET m_sServer`, `HANDLE m_hIOServer`.
- **Urutan `CAuthServer::Start()`**: `Config->Load` → `StartCfg` → `StartIOCP` → `StartClientManager` → **`StartDbManager`** (buka User/Log DB via `COdbcManager` + set conn-string Auth DB) → `StartWorkThread` → `StartListenThread`.

> Catatan: kode asli **sudah** setengah migrasi ADO→ODBC (`COdbcManager`, `OdbcOpenUserDB`) sambil mempertahankan `AdoManager` untuk Auth DB. Port `OdbcDb` kita searah dengan arah itu.

## Pemetaan IOCP → `boost::asio` (inti port `NetServer`)
| Win32 `NetServer` | `sc::net::NetServer` (asio) |
| :--- | :--- |
| `CreateIoCompletionPort` (`m_hIOServer`) + `m_hWorkerThread[]` jalankan `WorkProc` | `boost::asio::io_context` + pool thread jalankan `io_context.run()` |
| accept thread (`m_hAcceptThread`) + `SOCKET m_sServer` + `ListenProc` | `tcp::acceptor` + loop `async_accept` → `OnAccept(socket)` |
| update thread (`m_hUpdateThread`) + `UpdateProc` @ FPS (`setUpdateFrame`) | `steady_timer` periodik → `OnUpdate()` |
| regular-schedule thread + `RegularScheduleProc` | `steady_timer` periodik → `OnRegularSchedule()` |
| `HANDLE` quit-event + `Stop*Thread`/`StopIOCP` | `acceptor.close` + `timer.cancel` + `work_guard.reset` + `io.stop` + `join` |
| console `HWND` EditBox | `Log()` → `stderr` (headless) |

## Pemetaan `WinMain` + dialog → `main` + signal
| Win32 | Linux |
| :--- | :--- |
| `WinMain` + `DialogBox`/`MainDlgProc` | `main()` headless |
| tombol Start/Stop dialog | `Start()` saat boot; `SIGINT`/`SIGTERM` → `Stop()` graceful |
| tray icon / `Shell_NotifyIcon` | — (proses daemon/console) |
| `checkCmdParameter(lpCmdLine)` | `argv` (`--port`, `--db`) + env (`AUTH_PORT`, `SA_PASSWORD`/`DB_SERVER`/`DB_NAME`) |

## Berkas (`ranserver-linux/`)
- `net/net_server.{h,cpp}` — `sc::net::NetServer`: basis lifecycle cross-platform (lib CMake `ran_net`). boost::asio header-only → hanya link `Threads`.
- `servers/authserver/auth_server.{h,cpp}` — `sc::servers::AuthServer : public sc::net::NetServer` (lib `authserver_lib`, naik di atas `ran_net` + `ran_db`).
- `servers/authserver/main.cpp` — entry headless (template fan-out; target `authserver`).
- `servers/authserver/authserver_smoke.cpp` — uji in-process end-to-end (target `authserver_smoke`).

## Build & jalankan
**CMake (gate `make verify`):** target `ran_net` + `authserver_lib` + `authserver` + `authserver_smoke` sudah ada di `CMakeLists.txt`.

**Sesi ini** — gate `make verify` (dev-container) masih terblokir gangguan **apt/GPG** Docker Desktop (lihat [build-foundation](build-foundation.md)). Chip ini butuh **boost::asio DAN ODBC dalam satu image**; `odbc-spike` punya ODBC tapi tak punya boost, dan `apt-get install libboost-dev` masih gagal GPG. Karena `boost::asio` **header-only**, dibuat image gabungan **`ranlinux-dev`** = `odbc-spike` + header boost host (via `docker cp`, **tanpa apt**):
```bash
# Bake ranlinux-dev (sekali) — salin header boost host ke dalam image (header-only).
docker run -d --name b --platform linux/amd64 odbc-spike sleep 600
docker cp "$(readlink -f /opt/homebrew/include/boost)" b:/usr/include/boost   # brew boost 1.90
docker commit b ranlinux-dev && docker rm -f b

# Build + run smoke terhadap ranmssql (User DB) di network rannet.
SA=$(cat /tmp/ran_sa_pw.txt)
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanUser" \
  ranlinux-dev bash -lc \
  'g++ -std=c++17 -Wall -Wextra \
     /src/net/net_server.cpp /src/db/odbc_db.cpp \
     /src/servers/authserver/auth_server.cpp /src/servers/authserver/authserver_smoke.cpp \
     -I/src/net -I/src/db -I/src/servers/authserver \
     -o /tmp/authserver_smoke -lodbc -lpthread && /tmp/authserver_smoke'
```
> `ranlinux-dev` adalah **kemudahan lokal**, bukan dependency ber-brand: gate kanonik tetap `make verify` (`Dockerfile.dev`) yang akan hijau begitu apt sehat. Header boost yang dipakai murni header-only (asio mendeteksi `__linux__` → epoll saat compile).

---

## Hasil Eksekusi (2026-06-15)
```
AuthServer fan-out smoke — NetServer(IOCP->asio) lifecycle + OdbcDb wiring.
[..] [AuthServer] StartDbManager: User DB connected via sc::db::OdbcDb (probe row name=AccountInfo)
[..] [AuthServer] started on port 18901 with 2 worker(s)
  PASS Start() returns 0
  PASS IsRunning() after Start
  PASS DB layer connected (OdbcDb)
[..] [AuthServer] client #1 connected from 127.0.0.1
[..] [AuthServer] auth manager + DB released
[..] [AuthServer] stopped
  PASS client connected to acceptor
  PASS OnAccept fired => accepted=1
  PASS client received banner => AUTHSERVER
  PASS OnUpdate ticked (timer) => updates=4
  PASS Stop() returns 0
  PASS IsRunning() false after Stop

AUTHSERVER FAN-OUT SLICE OK: lifecycle (start/accept/update/stop) on boost::asio +
per-server entry pattern + OdbcDb connect.
```
`name=AccountInfo` = tabel **nyata** di `RanUser` → wiring DB sungguhan, bukan mock.

## Yang di-stub / ditangguhkan (chip lanjutan)
- **Lapisan paket** (shared): `MsgManager` double-buffer + ZLIB + `PER_IO_OPERATION_DATA` pools (`GetFreeOverIO`/`ReleaseOperationData`). Saat ini `OnAccept` hanya *greet + close*.
- **Handler pesan AuthServer**: `MsgAuthCertificationRequest` → `CertificateAuthDataFromDB`, session/event/server-state. (Jalur tulis SP ber-parameter sudah tersedia dari [db-layer-port](db-layer-port.md) untuk ini.)
- **Multi-DB**: Log DB + Auth DB (skeleton baru buka User DB) + **pooling per-worker** (`OdbcDb` single-thread → DB hanya disentuh di `OnStart`).
- **Config XML** (`AuthServerConfigXml`) → sementara argv/env.

## Chip berikutnya
> **A. Fan-out 4 server sisanya** (Login/Agent/Session/Field) — kini **mekanik** mengikuti template ini → cocok **Sonnet 4.6**. Field/Agent lebih besar (game brain) → mungkin Opus untuk yang pertama.
> **B. Lapisan paket shared** (`MsgManager`/ZLIB/per-IO) — port bersama, lebih besar → **Opus 4.8 @ xhigh**.
> **C. Cert flow AuthServer** (pakai jalur tulis SP ber-parameter) — sentuh integritas → gate manusia → **Opus**.

## Kesimpulan
**Server utuh pertama (shell-nya) ter-port & berjalan di Linux** terhadap DB nyata: basis `NetServer` (IOCP→asio) + entry headless (`WinMain`→`main`+signal) + wiring `OdbcDb`. Ini **titik fan-out** — 4 server lain mengulang pola yang sama; lapisan paket & handler pesan menyusul sebagai chip shared/per-server. Bukti bahwa strategi "pertahankan kontrak kelas, ganti implementasi ke portabel" (ADR-001) berlaku **dari DB hingga server**.
