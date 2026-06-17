# Runbook — AgentServer Login Handler (Chip A)

> **Tujuan**: Mengimplementasikan penanganan paket login regional Indonesia (`IDN_NET_MSG_LOGIN` → `NET_MSG_LOGIN_FB`) pada `AgentServer` menggunakan kursor basis data SQL Server lintas-platform.

---

## Alur Logika

1. **Penerimaan Paket**:
   * `AgentServer::dispatch` memotong aliran paket bertipe `IDN_NET_MSG_LOGIN` (233).
   * Melakukan validasi ukuran paket (`sizeof(IDN_NET_LOGIN_DATA) == 68` byte).

2. **Verifikasi Kredensial**:
   * Memanggil stored procedure `dbo.gs_user_verify` pada database `RanUser` menggunakan wrapper thread-safe `m_dbMx`.
   * Input stored procedure: `UserID`, `UserPass`, `UserIP`, `ServerGroup`, `ServerNumber`.
   * Stored procedure mengembalikan kode hasil (`nResult`), **dipetakan ke `EM_LOGIN_FB_SUB`** persis seperti `CAgentGsUserCheck::Execute` ([`RanLogicServer/Database/DBAction/DbActionUser.cpp`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/Database/DBAction/DbActionUser.cpp)) — sumber otoritatif jalur GS/IDN (`COdbcManager::GsUserCheck` mengembalikan return SP mentah, lalu DbAction inilah yang memetakannya):

   | `nResult` SP | `EM_LOGIN_FB_SUB` | Arti |
   | :---: | :--- | :--- |
   | -1 | `SYSTEM` | error DB |
   | 0 | `INCORRECT` | ID/PWD salah |
   | **1, 2, 3** | **`OK`** | **login sukses** |
   | **30** | `KR_OK_NEW_PASS` | sukses + perlu 2nd-pass baru *(sub-flow ditangguhkan)* |
   | **31** | `KR_OK_USE_PASS` | sukses + 2nd-pass ada *(sub-flow ditangguhkan)* |
   | 4 | `IP_BAN` | IP diblok |
   | 5 | `DUP` | login ganda |
   | 6 | `BLOCK` | akun diblok |
   | 7 | `RANDOM_PASS` | perlu random password |
   | 23 | `BETAKEY` | beta key (GS_PARAM) |
   | lainnya | `FAIL` | gagal umum |

   > ⚠️ **Koreksi 2026-06-16**: versi awal hanya menganggap `{2,3}` sukses → menolak login sah **kode 1 dan 30/31** (state 2nd-password) serta salah-memetakan 7/23. **Catatan penting**: kode "+10 Return of Hero" (`11/12/13/40/41`) itu **khusus jalur DAUM** (`daum_user_verify_new` di `s_COdbcUserCheck.cpp`), **bukan** `gs_user_verify` — sengaja TIDAK ada di mapping ini.

3. **Pengambilan Metrik Karakter**:
   * Jika verifikasi kredensial sukses, query dilakukan ke tabel `dbo.GSUserInfo` untuk membaca metrik pengguna seperti `IsUse2ndPass` (Secondary PIN check) dan jumlah karakter tersisa (`ChaRemain`).
   * Jika `IsUse2ndPass` bernilai `2`, login diarahkan ke pemeriksaan PIN sekunder.

4. **Penyusunan Feedback**:
   * Mengemas status login ke dalam biner `NET_LOGIN_FEEDBACK_DATA` (68 byte) dengan padding layout memori yang presisi.
   * Mengirimkan paket balasan `NET_MSG_LOGIN_FB` (223) ke client session.

---

## Definisi Paket Biner

Definisi paket dideklarasikan secara presisi pada [agent_server_msg.h](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/ranserver-linux/servers/agentserver/agent_server_msg.h):

```cpp
struct IDN_NET_LOGIN_DATA {
    uint32_t dwSize;
    uint32_t nType;
    int      m_nChannel;
    char     m_szUserid[21];
    char     m_szPassword[33];
};

struct NET_LOGIN_FEEDBACK_DATA {
    uint32_t dwSize;
    uint32_t nType;
    int      m_Result;
    int      m_Extreme;
    int      m_bActor;
    int      m_CheckFlag;
    int      m_LauncherVersion;
    int      m_GameVersion;
    uint16_t m_ChaRemain;
    char     m_szDaumGID[21];
    char     pad1[1];
    int      m_Country;
    uint32_t m_sCountryInfo;
    int      m_bJumping;
};
```

---

## Hubungan Sumber Kode Legacy

* Logika pemrosesan asli Windows: [AgentServerMsgLogin.cpp](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/Server/AgentServerMsgLogin.cpp#L12)
* Definisi tipe packet & sub-feedback: [s_NetGlobal.h](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogic/s_NetGlobal.h#L110)

---

## Langkah Verifikasi

Kompilasi dan jalankan smoke test pada container `ranlinux-dev`:

```bash
docker run --rm --platform linux/amd64 --network rannet \
  -v "$PWD":/src -e SA_PASSWORD="RanOnline@Spike0" -e DB_SERVER="ranmssql,1433" -e DB_NAME="RanUser" \
  ranlinux-dev bash -lc 'INC="-I/src/ranserver-linux/net -I/src/ranserver-linux/db -I/src/ranserver-linux/servers/agentserver -I/src/ranserver-linux/platform -I/src/XLib_lzo/include"; COMMON="/src/ranserver-linux/net/net_server.cpp /src/ranserver-linux/net/packet.cpp /src/ranserver-linux/net/session.cpp /src/ranserver-linux/net/minlzo.cpp /src/ranserver-linux/net/send_msg_buffer.cpp /src/ranserver-linux/db/odbc_db.cpp"; LZO_FILES="/src/XLib_lzo/src/lzo_init.c /src/XLib_lzo/src/lzo1x_1.c /src/XLib_lzo/src/lzo1x_d2.c"; g++ -std=c++17 -Wall -Wextra -fwrapv $COMMON $LZO_FILES /src/ranserver-linux/servers/agentserver/agent_server.cpp /src/ranserver-linux/servers/agentserver/agentserver_smoke.cpp $INC -lodbc -lpthread -o /tmp/agentserver_smoke && /tmp/agentserver_smoke'
```
Asssertion memvalidasi status sukses (`result=0`) dan kegagalan sandi (`result=5`).
