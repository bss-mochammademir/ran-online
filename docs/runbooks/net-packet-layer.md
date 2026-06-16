# Runbook — Lapisan Paket Jaringan: framing `NET_MSG_GENERIC` + `MsgManager` double-buffer → Linux

> **Tujuan**: Mem-port **lapisan paket shared** yang membuat server benar-benar *bicara protokol* (bukan cuma accept+close): header biner `NET_MSG_GENERIC`, **framing** aliran TCP→pesan (logika "memotong paket" milik worker IOCP), dan antrean **double-buffer `MsgManager`** (`SigmaCore/Net/MsgList`). Lalu disambung ke `AuthServer` ([fan-out #1](server-fanout-authserver.md)) lewat `Session` (loop baca async) sehingga pipeline terima→proses→balas teruji end-to-end.

> ✅ **Sudah dieksekusi 2026-06-15** — `packet_smoke` **22/22** + `authserver_smoke` end-to-end **hijau** (kirim 3 paket ber-frame termasuk satu yang dipecah + satu header-only → di-echo balik & ter-decode benar). Lihat [§Hasil](#hasil-eksekusi-2026-06-15).

---

## Protokol asli (dari sumber, bukan ringkasan doc 04)
Pembacaan sumber mengoreksi penyederhanaan [doc 04](../04_network_protocol.md):

1. **Header pesan** `NET_MSG_GENERIC` (`RanLogic/s_NetGlobal.h`, **8 byte** — bukan 4):
   ```cpp
   struct NET_MSG_GENERIC { DWORD dwSize; EMNET_MSG nType; };   // dwSize = TOTAL termasuk header
   ```
   `dwSize` = ukuran **total** pesan termasuk 8 byte header (pesan kosong `dwSize==8`; `NET_MSG_CHARACTER`=12). `nType` = enum tipe (di sini diperlakukan `uint32` opaque). Little-endian di x86/x64 (Windows asli & target Linux sama → tanpa byte-swap).
2. **Envelope kirim** `CSendMsgBuffer` (`RanLogic/Network/SendMsgBuffer.cpp`) meng-agregasi pesan (sampai `COMPRESS_PACKET_SIZE`=1000) lalu membungkus dalam `NET_COMPRESS` (`Size|Type=NET_MSG_COMPRESS|bCompress|…`) dengan kompresi **LZO** (`SigmaCore/Compress/CMinLzo`) — **bukan ZLIB**. Flag `bCompress` memberitahu penerima untuk dekompres.
3. **msgpack + CRC**: pesan kontrol `NET_MSG_GCTRL` memakai `msgpack::sbuffer` + `m_Crc` opsional (`SetData(..., UseCrc)`).

## Yang di-port (chip ini) vs ditangguhkan
| Bagian | Status |
| :--- | :---: |
| Header `NET_MSG_GENERIC` 8-byte (serialize LE eksplisit) | ✅ `sc::net::EncodeMessage` |
| Framing stream→pesan (partial read, multi-per-read, tolak panjang invalid) | ✅ `sc::net::MessageFramer` |
| Antrean `MsgManager` double-buffer (Front/Back/Flip) | ✅ `sc::net::MsgManager` |
| Loop baca async per-koneksi (ganti recv-completion IOCP + `PER_IO_OPERATION_DATA`) | ✅ `sc::net::Session` |
| Integrasi `AuthServer` (accept→Session→MsgManager→UpdateProc dispatch→balas) | ✅ |
| Envelope `NET_COMPRESS` + kompresi **LZO** (`CMinLzo`) | ⏳ (modul kompresi sendiri, perlu fidelity test) |
| Payload **msgpack** + **CRC** (`NET_MSG_GCTRL`) | ⏳ |
| Agregasi kirim `CSendMsgBuffer` (batch < 1000B) | ⏳ |
| Pemetaan nilai `EMNET_MSG` nyata (`s_NetGlobal.h`) ke handler | ⏳ (saat porting handler) |

## Pemetaan double-buffer (`MsgManager`)
| Win32 `sc::net::MsgManager` (`MsgList`) | Port Linux |
| :--- | :--- |
| Front (`m_pMsgFront`) ditulis worker IOCP via `MsgQueueInsert` | `m_front`, `MsgQueueInsert` (lock) |
| Back (`m_pMsgBack`) dibaca thread logika via `GetMsg` | `m_back`, `GetMsg` |
| `MsgQueueFlip` tukar Front↔Back tiap frame tick | `MsgQueueFlip` pindah Front→Back |
| `GetMsg` lock-free (di-pin ke update-thread khusus) | semua op **dikunci** (tick jalan di pool io_context, bukan thread khusus) — perilaku sama, jelas-benar di pool |

> Asli memakai pool node `MSG_LIST` (`Buffer[2048]` tetap); port memakai `std::deque<Message>` dinamis. Batas `NET_DATA_BUFSIZE`=2048 ditegakkan framer sebagai panjang maksimum (cermin guard `addMsg`).

## Berkas (`ranserver-linux/net/` + `servers/authserver/`)
- `net/packet.{h,cpp}` — `NET_MSG_GENERIC` LE, `Message`, `EncodeMessage`, `MessageFramer`.
- `net/msg_manager.h` — `MsgManager` double-buffer (header-only).
- `net/session.{h,cpp}` — `Session` (loop `async_read_some`→framer→callback; `Send`).
- `servers/authserver/auth_server.{h,cpp}` — `OnAccept` buat `Session`; `OnUpdate` = `MsgQueueFlip` + drain `GetMsg` → `dispatch` (echo).
- CMake: `ran_net` kini mencakup `packet.cpp`+`session.cpp`; target uji `packet_smoke`.

## Build & jalankan (image `ranlinux-dev`, lihat [fan-out #1](server-fanout-authserver.md))
```bash
SA=$(cat /tmp/ran_sa_pw.txt)
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanUser" \
  ranlinux-dev bash -lc '
    INC="-I/src/net -I/src/db -I/src/servers/authserver"
    g++ -std=c++17 -Wall -Wextra /src/net/packet.cpp /src/net/packet_smoke.cpp $INC -o /tmp/packet_smoke -lpthread && /tmp/packet_smoke
    g++ -std=c++17 -Wall -Wextra /src/net/net_server.cpp /src/net/packet.cpp /src/net/session.cpp /src/db/odbc_db.cpp \
      /src/servers/authserver/auth_server.cpp /src/servers/authserver/authserver_smoke.cpp $INC -o /tmp/authserver_smoke -lodbc -lpthread && /tmp/authserver_smoke'
```

---

## Hasil Eksekusi (2026-06-15)
**`packet_smoke` (unit, tanpa socket) — 22/22:** encode LE (dwSize/nType/payload), dua-pesan-satu-feed, satu pesan dipecah lintas 3 feed (header split + payload split), tolak `dwSize` > 2048 dan < 8, dan urutan FIFO `MsgManager` (Insert→Front, Flip→Back, drain).

**`authserver_smoke` (end-to-end):**
```
[AuthServer] StartDbManager: User DB connected via sc::db::OdbcDb (probe row name=AccountInfo)
[AuthServer] started on port 18901 with 2 worker(s)
[AuthServer] client #1 connected from 127.0.0.1
  PASS 3 frames echoed back => echoes=3
  PASS echo#1 type+payload            (101/"alpha")
  PASS echo#2 type+payload (split msg)(202/"world", dikirim 2 write)
  PASS echo#3 header-only frame       (303, dwSize==8, payload kosong)
  PASS server dispatched >= 3 (MsgProcess) => processed=3
  PASS OnUpdate ticked (timer) => updates=1
AUTHSERVER FAN-OUT SLICE OK: asio lifecycle + framed recv pipeline
(Session -> framer -> MsgManager -> dispatch -> echo) + OdbcDb connect.
```

## Catatan teknis (penting untuk tim)
- **Framing**: baca 8-byte header → `dwSize` (LE) → validasi `8 ≤ dwSize ≤ 2048` → tunggu sampai `dwSize` byte tersedia → emit. Sisa byte (frame parsial) disimpan lintas `Feed`. `dwSize` invalid → error protokol → drop koneksi.
- **`dwSize` termasuk header** (bukan hanya payload) — sesuai ctor `NET_MSG_GENERIC`.
- **Endianness**: serialize eksplisit LE (tanpa `reinterpret_cast` struct) → bebas asumsi packing/alignment/aliasing.
- **Konkurensi**: `Session` (baca, beberapa worker) menulis Front via `MsgQueueInsert` (lock); `OnUpdate` (tick) `Flip`+drain. Semua op `MsgManager` dikunci karena tick jalan di pool, bukan thread khusus.

## Chip berikutnya
> **A. Envelope `NET_COMPRESS` + LZO** (port `SigmaCore/Compress/CMinLzo`) — agar kompatibel penuh dengan klien yang mengirim paket terkompresi. Fidelity test sendiri. → **Opus**.
> **B. msgpack + CRC** untuk `NET_MSG_GCTRL`. 
> **C. Handler cert AuthServer** (`MsgAuthCertificationRequest`→`CertificateAuthDataFromDB`) — kini pipeline terima siap; pakai jalur tulis SP ber-parameter. Gate manusia.
> **D. Fan-out 4 server lain** — mekanik (Sonnet).

## Kesimpulan
Lapisan paket inti (framing + double-buffer + loop baca) **ter-port & berjalan** di Linux, tersambung ke `AuthServer`: aliran TCP nyata kini di-frame jadi `NET_MSG_GENERIC`, diantrekan double-buffer, diproses di update-tick, dan dibalas — pipeline yang **semua server** butuhkan. Lapisan kompresi/msgpack/CRC bersifat aditif di atas ini (chip lanjutan).
