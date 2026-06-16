# Runbook — Cert Flow AuthServer: `ProcessCertificationForAuth` end-to-end (paket → SP param → balas)

> **Tujuan**: Irisan **vertikal** pertama — fitur nyata AuthServer. Menyambung semua lapisan yang sudah di-port (paket → parse → **SP ber-parameter** via OdbcDb → baca result set → balas ber-frame) pada operasi DB sertifikasi `ProcessCertificationForAuth` (`AdoManager::ProcessCertificationForAuth`), di-port verbatim ke `OdbcDb`.

> ✅ **Sudah dieksekusi 2026-06-15** — `authserver_smoke` (cert slice) **hijau**: klien kirim cert-request ber-frame → `sp_CertificationUniqKey` jalan via jalur tulis param → result set dibaca → balasan `1;1;10101;GOODKEY-NEW`. Residu nol. Lihat [§Hasil](#hasil-eksekusi-2026-06-15).

---

## Alur asli (Windows)
1. `CAuthServer::MsgAuthCertificationRequest` (`AuthServerMsgEx.cpp`) — `reinterpret_cast` buffer ke `NET_AUTH_CERTIFICATION_REQUEST` (**struktur biner, bukan msgpack**), simpan `G_AUTH_INFO`, → `ResponseAuthCertification`.
2. `m_pAuthManager->CertificateAuthData(gsi.szAuthData)` — `GlobalAuthManager` **mendekripsi** blob `szAuthData` jadi field terstruktur (`SAuthData`: country, serverType, IP, port, uniqKey, isSessionSvr).
3. `CertificateAuthDataFromDB` → queue `db::CertificationAuthData` (job DB async) → `Execute` → `m_pDbManager->ProcessCertificationForAuth(...)`.
4. `AdoManager::ProcessCertificationForAuth` (`AdoManagerAuth.cpp`):
   ```cpp
   Ado.AppendIParamInteger("@country", ...);  AppendIParamInteger("@servertype", ...);
   Ado.AppendIParamVarchar("@serverip", ...);  AppendIParamInteger("@serverport", ...);
   Ado.AppendIParamVarchar("@uniqKey", ...);    AppendIParamInteger("@IsSessionSvr", ...);
   Ado.ExecuteStoredProcedure("dbo.sp_CertificationUniqKey");
   do { GetCollect("SessionSvrID"); GetCollect("Certification"); GetCollect("UniqKey"); } while (Next());
   ```
5. Balasan: `SendCountryAccessApprove` (**msgpack**) + `SendAuthCertificationToSessionServer` (biner `NET_AUTH_CERTIFICATION_ANS`).

## Temuan (dari sumber)
- Cert-request **biner** (bukan msgpack) → tak butuh port msgpack untuk jalur ini. (msgpack hanya di balasan country-access.)
- **`sp_CertificationUniqKey` TIDAK ada di 8 DB hasil restore** — ia tinggal di **Global Auth DB** (DB auth terpisah, di luar set .bak). Dicek: RanUser/WBUser/RanShop/… semua `-`.
- Field cert berasal dari **dekripsi `GlobalAuthManager`** atas `szAuthData` — modul kripto tersendiri.

## Yang di-port vs ditangguhkan
| Bagian | Status |
| :--- | :---: |
| `ProcessCertificationForAuth` (urutan param + `EXEC sp_CertificationUniqKey` + baca `SessionSvrID/Certification/UniqKey`) | ✅ verbatim → `OdbcDb` (jalur tulis param + jalur baca) |
| Routing dispatch `kMsgAuthCertReq` → handler (AuthPacketFunc) | ✅ |
| End-to-end via pipeline paket (terima → parse → SP → balas ber-frame) | ✅ |
| Dekripsi `GlobalAuthManager` (`szAuthData`→`SAuthData`) | ⏳ (modul kripto) |
| Struktur wire biner `NET_AUTH_CERTIFICATION_REQUEST/ANS` persis | ⏳ (slice pakai payload teks `a;b;c`) |
| Balasan `SendCountryAccessApprove` (msgpack) + relay ke Session Server | ⏳ |

## Berkas
- `ranserver-linux/servers/authserver/auth_server.{h,cpp}` — `CertRequest`/`CertResult`, `kMsgAuthCertReq/Ans`, `ProcessCertificationForAuth` (port verbatim), `dispatch` route-by-type.
- `authserver_smoke.cpp` — cert sub-test (buat SP stand-in → kirim cert frame → assert balasan → drop SP).

## Build & jalankan (`ranlinux-dev`, lihat [fan-out #1](server-fanout-authserver.md))
```bash
SA=$(cat /tmp/ran_sa_pw.txt)
docker run --rm --network rannet --platform linux/amd64 \
  -v "<repo>/ranserver-linux":/src \
  -e SA_PASSWORD="$SA" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanUser" \
  ranlinux-dev bash -lc '
    INC="-I/src/net -I/src/db -I/src/servers/authserver"
    g++ -std=c++17 -Wall -Wextra /src/net/net_server.cpp /src/net/packet.cpp /src/net/session.cpp /src/db/odbc_db.cpp \
      /src/servers/authserver/auth_server.cpp /src/servers/authserver/authserver_smoke.cpp $INC -o /tmp/authserver_smoke -lodbc -lpthread && /tmp/authserver_smoke'
```
**SP stand-in** (tanda-tangan nyata; isi badan stand-in karena SP asli ada di Global Auth DB yang belum di-restore):
```sql
CREATE OR ALTER PROCEDURE dbo.sp_CertificationUniqKey
  @country INT,@servertype INT,@serverip VARCHAR(64),@serverport INT,@uniqKey VARCHAR(64),@IsSessionSvr INT
AS BEGIN SET NOCOUNT ON;
  SELECT @serverport+1000 AS SessionSvrID,
         CASE WHEN @uniqKey='GOODKEY' THEN 1 ELSE 0 END AS Certification,
         @uniqKey+'-NEW' AS UniqKey; END
```

---

## Hasil Eksekusi (2026-06-15)
```
[AuthServer] cert request uniqKey=GOODKEY -> certification=1
  PASS stand-in sp_CertificationUniqKey created
  PASS cert response type = kMsgAuthCertAns
  PASS cert SP ran (ProcessCertificationForAuth) => 1;1;10101;GOODKEY-NEW
  PASS Certification=1 for GOODKEY
  PASS SessionSvrID = serverport+1000  (9101+1000)
  PASS new UniqKey from SP => GOODKEY-NEW
```
Klien kirim `kMsgAuthCertReq` payload `0;1;127.0.0.1;9101;GOODKEY;0` → dispatch parse → `ProcessCertificationForAuth` (6 param input) → `sp_CertificationUniqKey` → result set → balas ber-frame `1;1;10101;GOODKEY-NEW`. SP stand-in dibuat lalu **di-drop** (dicek: 0 sisa di RanUser).

## Catatan teknis
- **OdbcDb single-thread aman**: `ProcessCertificationForAuth` dipanggil dari update-tick yang terserialisasi (bukan beberapa worker bersamaan), jadi satu koneksi `m_db` tak berebut. Asli pakai antrean job DB async (`AddAuthJob`) → ditangguhkan (di sini sinkron di tick demi kesederhanaan slice).
- **Urutan kolom**: `sp_CertificationUniqKey` mengembalikan `SessionSvrID(1)/Certification(2)/UniqKey(3)`; dibaca naik sesuai batasan kursor forward-only `OdbcDb`.
- ⚠️ **Bukan autentikasi produksi**: kripto (`GlobalAuthManager`), struktur wire biner, dan SP asli **ditangguhkan**. Ini bukti **wiring** operasi cert DB end-to-end, bukan auth siap-produksi.

## Chip berikutnya
> **A. `GlobalAuthManager`** (kripto `szAuthData` encode/decode) + struktur wire `NET_AUTH_CERTIFICATION_REQUEST/ANS` → cert nyata penuh. → **Opus**, gate manusia (keamanan).
> **B. msgpack + CRC** (`NET_MSG_GCTRL`) → `SendCountryAccessApprove` + balasan kontrol lain.
> **C. Envelope LZO** (`NET_COMPRESS`/`CMinLzo`) → kompat klien terkompresi.
> **D. Fan-out 4 server lain** (Login/Agent/Session/Field) — mekanik. → **Sonnet**.

## Kesimpulan
Operasi DB sertifikasi `ProcessCertificationForAuth` **ter-port verbatim & berjalan end-to-end** lewat pipeline paket + jalur tulis param + jalur baca result-set — irisan vertikal pertama yang menyentuh semua lapisan yang di-port (paket, net, DB) sekaligus, pada SP nyata `sp_CertificationUniqKey`. Lapisan kripto/wire-struct/msgpack bersifat aditif di atas ini.
