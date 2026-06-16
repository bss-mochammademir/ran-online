# 09 — Katalog GM Command Ran Online

> **Tujuan**: mendokumentasikan sistem **GM Command** — "ruang kendali" game yang, bagi banyak operator private server, justru jadi **hadiah** dari seluruh kerja keras membangun server. Dokumen ini lebih dari sekadar referensi teknis: ia merekam *makna* — apa yang bisa dilakukan seorang GM, bagaimana otoritasnya dijaga, dan bagaimana satu perintah merambat ke seluruh topologi server.
>
> Disusun 2026-06-16 dari pembacaan langsung source (`RanLogic/s_NetGlobal.h`, `RanLogicServer/FieldServer/GLGaeaServerMsg.cpp`, `RanLogic/Network/NetLogicDefine.h`). Semua nama command & handler **diverifikasi ke kode**, bukan ingatan.

---

## 0. Ringkas

- **177** message bertipe `NET_MSG_GM_*` (enum `EMNET_MSG` di [`RanLogic/s_NetGlobal.h`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogic/s_NetGlobal.h)).
- Dieksekusi engine **Gaea** di FieldServer lewat tabel dispatch `m_MsgFunc[NET_MSG_...].Msgfunc = &GLGaeaServer::Handler` ([`GLGaeaServerMsg.cpp`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/FieldServer/GLGaeaServerMsg.cpp)).
- Gerbang otoritas = **`UserType`** akun (enum `EMUSERTYPE`), field yang **sama** dengan yang dibaca handler login AgentServer (`GSUserInfo.UserType`).

---

## 1. 🔐 Gerbang otoritas — siapa yang boleh

Hak GM ditentukan **grade akun** `UserType` ([`RanLogic/Network/NetLogicDefine.h:330`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogic/Network/NetLogicDefine.h), enum `EMUSERTYPE`):

| Konstanta | Nilai | Arti |
| :--- | :---: | :--- |
| `USER_COMMON` | 0 | pemain biasa |
| `USER_PREMIUM` | 1 | akun premium |
| `USER_SPECIAL` | 10 | spesial |
| `USER_USER_GM` | 11 | GM dalam-game |
| `USER_USER_GM_MASTER` | 12 | GM master |
| `USER_GM4` | 19 | GM tingkat 4 (Web GM) |
| `USER_GM3` | 20 | GM tingkat 3 |
| `USER_GM2` | 21 | GM tingkat 2 |
| `USER_GM1` | 22 | GM tingkat 1 (operasional tertinggi) |
| `USER_MASTER` | 30 | Master (tertinggi) |

> **Penyambung penting**: `UserType` ini **persis** field yang dibaca `AgentServer::ProcessUserLogin` dari `GSUserInfo` saat login ([runbook agent-login-handler](runbooks/agent-login-handler.md)). Artinya **login = sekaligus pintu masuk hak GM**: server tahu grade-mu saat masuk, dan grade itulah yang membuka (atau menutup) 177 command di bawah.

---

## 2. Konvensi routing (kenapa banyak yang "kembar")

Satu aksi GM sering merambat lintas server. Akhiran nama message = jalur perjalanannya di topologi:

| Sufiks | Arti jalur |
| :--- | :--- |
| `_CA` | Client → Agent |
| `_AS` / `_SS` | Agent → Session / di Session |
| `_FLD` | Agent → Field |
| `_AG` | → Agent |
| `_CF` / `_FC` | Client ↔ Field |
| `_FB` | Feedback (balasan) |
| `_BRD` | Broadcast ke semua client |
| `_CAF` | rantai Client-Agent-Field |
| `_END` | penanda akhir event |

Contoh: `KICK_USER` (asal) → `KICK_USER_PROC` (Session proses) → `KICK_USER_PROC_FLD` (diteruskan Agent ke Field yang menampung sesi). Inilah kenapa pekerjaan **fan-out 5 server** kita relevan: GM command adalah contoh sempurna pesan yang menyeberang Auth→Agent→Session→Field.

---

## 3. Katalog by tema (handler terverifikasi)

### 🪄 Spawn & dunia
| Command | Handler `GLGaeaServer::` | Fungsi |
| :--- | :--- | :--- |
| `MOB_GEN` / `MOB_GEN_EX` | `GMCtrolMobGen` / `…Ex` | spawn monster (+ varian) |
| `MOB_DEL` / `MOB_DEL_EX` | `GMCtrolMobDel` / `…Ex` | hapus monster |
| `MOB_LEVEL` / `…_LAYER` / `…_CLEAR` / `…_LIST` | `GMCtrolMobLevel…` | atur/lihat level monster per layer |
| `GENCHAR` (+`_AG`/`_FLD`/`_POS`/`_FB`) | (rantai gen char/NPC) | munculkan karakter/NPC |
| `EVENT_ITEM_GEN` / `…_END` | `MsgEventItemGen` / `…End` | **hujan item** (event) |
| `EVENT_MONEY_GEN` / `…_END` | `MsgEventMoneyGen` / `…End` | **hujan uang** (event) |
| `LAND` / `LAND_INFORMATION` | `MsgLand` | kontrol wilayah/land |
| `MAP_LAYER` (+notify) | — | kelola layer peta |

### 🚀 Teleport & lokasi
| Command | Handler | Fungsi |
| :--- | :--- | :--- |
| `MOVE2CHAR` (+`_POS`/`_AG`/`_FLD`/`_FB`) | `RequestMove2CharPos` / `MsgReqMove2CharFld` | teleport ke karakter tertentu |
| `MOVE2GATE` (+`_FLD`/`_FB`) | `GMCtrolMove2Gate` | teleport ke gate |
| `MOVE2MAPPOS` (+`_FLD`) | `GMCtrolMove2MapPos` | teleport ke peta+posisi |
| `WHERE_NPC` / `…_ALL` | `GMCtrlWhereNpc` / `…ALL` | cari lokasi NPC |
| `WHERE_PC_MAP` / `WHERE_PC_POS` | `GMCtrolWherePcPos` | cari lokasi pemain |
| `VIEWALLPLAYER` (+`_FLD`) | `RequestViewAllPlayer` | lihat semua pemain |

### ⚡ Karakter, EXP & event server
| Command | Handler | Fungsi |
| :--- | :--- | :--- |
| `ACQUIRE_EXP` (+`_ANS`) | `MsgGmAcquireEXP` | beri EXP / instant level |
| `AUTO_LEVEL` (+list/info/result) | — | auto-leveling tool |
| `EVENT_EXP_CAF` / `…_END` | `MsgEventExp` / `…End` | **boost EXP** server-wide |
| `EVENT_GRADE` / `…_END` | `MsgEventGrade` / `…End` | event grade/rate |
| `EVENT_EX` / `…_END` | `MsgEventEx` | event EX |
| `LIMIT_EVENT_*` | `MsgLimitEvent…` | event berbatas-waktu (begin/end/reset) |
| `CLASS_EVENT` | `MsgClassEvent` | event berbasis kelas |
| `CHAR_POINT_ADD_CF` | `MsgPointShopGmCharPointAddCF` | tambah point shop |

### 🛡️ Moderasi & administrasi
| Command | Handler | Fungsi |
| :--- | :--- | :--- |
| `KICK_USER` (+`_PROC`/`_PROC_FLD`) | `GMKicUser` | tendang pemain |
| `CHAT_BLOCK_*` (charid/name/uaccount) | `GMCtrolChatBlockFld` | mute chat |
| `WARNING_MSG` (+`_FLD`/`_BRD`) | `GMCtrolWarningMSG` | pesan peringatan/pengumuman |
| `COUNTDOWN_MSG_BRD` | — | hitung mundur broadcast |
| `DB_UNLOCK_USER` (+`_AF`) | `GmDbUnlockUserAF` | buka kunci akun di DB |
| `CHAR_INFO_*` | `GMCtrolCharInfoFld` | inspeksi data karakter |
| `WHISPER_STATE` / `GETWHISPERMSG` | — | pantau whisper |
| `NONCONFRONT_MODE` | `GMCtrlNonConfrontMode` | mode kebal / non-combat |
| `FREEPK` (+`_BRD`) | `GMCtrolFreePK` | toggle free-PK |

### 🎥 Sandbox & "easter egg"
| Command | Handler | Catatan |
| :--- | :--- | :--- |
| `SHOWMETHEMONEY` (+`_FLD`/`_EMULATOR_CF`) | `GMCtrolShowMeTheMoney` | cheat code klasik StarCraft → kasih uang 💰 |
| `WHYSOSERIOUS` (+`_FLD`/`_EMULATOR_CF`/`_FB_FAC`) | `GMCtrolWhySoSerious` | referensi Joker (Dark Knight) — command efek |
| `BIGHEAD` (+`_BRD`) | `GMCtrolBigHead` | mode kepala besar (cosmetic) |
| `BIGHAND` (+`_BRD`) | `GMCtrolBigHand` | mode tangan besar (cosmetic) |
| `FLYCAMERACONTROL_*` | `GMFlyCameraControl…` | kamera terbang bebas (spectate/sinematik) |

### 🛰️ Ops, jaringan & data
| Command | Handler | Fungsi |
| :--- | :--- | :--- |
| `PING` / `PING_FB` | `ProcessPingMsg` | ukur latency |
| `PINGTRACE_ON` / `OFF` | `ProcessPingTraceMsg` | trace jaringan |
| `NETWORKPROFILE_REQ` | — | profil jaringan |
| `MSG_DELAY_MODIFY` | `MsgGMNetMsgDelay` | atur delay pesan (debug) |
| `GAME_DATA_UPDATE_AS/CA/SS` | — | reload data game (glogic) tanpa restart |
| `LOG_ITEM_RELOAD_AS/CA/SS` | — | reload konfigurasi log item |
| `LOG_TO_DB_CAF` | `MsgLogToDB` | paksa flush log ke DB |
| `LOAD_IPEVENT` / `SET_IPEVENT` | `MsgGmLoadIPEvent` / `Set…` | event berbasis IP |
| `SHOP_INFO_REQ` / `_FB` | `RequestShopInfo` | inspeksi shop |

### 🎲 Random box / gacha (relevan strategis)
| Command | Fungsi |
| :--- | :--- |
| `RANDOMBOX_CHANCE_REGISTER_*` | daftarkan peluang random box |
| `RANDOMBOX_CHANCE_LIST_*` | lihat peluang aktif |
| `RANDOMBOX_CHANCE_DELETE_*` | hapus peluang |
| `RANDOMBOX_NOTIFY_ITEM_RELOAD_CAF` | reload item random box |

> ⚠️ **Bersinggungan langsung dengan north-star anti-judi** ([[ran-online-monetization]]): bahkan game lama punya knob *odds* gacha yang bisa diatur GM. Untuk reborn kita, sub-sistem inilah yang harus **transparan & ber-pagar** (atau dihapus) — bukan disembunyikan. Dokumen ini menandainya sebagai titik desain etis, bukan sekadar fitur.

### 🌐 Mobile / lintas-platform
| Command | Fungsi |
| :--- | :--- |
| `RANMOBILE_COMMAND_REQUEST` / `_RESPONSE` | jalur GM command untuk klien mobile (RanMobile) |

---

## 4. Makna untuk modernisasi

1. **GM tooling = pekerjaan ops kelas-satu**, bukan tempelan. 177 command ini sebenarnya spesifikasi sebuah **admin console**. Saat memodernkan, ini jadi cetak biru fitur panel admin (web) — dengan **audit log** (siapa GM mana jalankan apa, kapan) yang dulu minim.
2. **Otorisasi berbasis grade** sudah ada (`UserType`) — modernisasi tinggal menambah **RBAC** yang lebih halus + audit + 2FA untuk grade tinggi (`USER_GM1`/`USER_MASTER`).
3. **Routing lintas-server** (`_CA`/`_AS`/`_FLD`) memvalidasi arsitektur fan-out kita: GM command adalah uji-nyata pertama pesan yang menyeberang Agent↔Session↔Field saat handler-nya nanti di-port.
4. **`SHOWMETHEMONEY`/`WHYSOSERIOUS`/`BIGHEAD`** — sisi manusiawi dev. Layak dipertahankan sebagai easter egg di reborn: jiwa game bukan cuma rules, tapi juga keisengan pembuatnya. 🙂

---

## 5. Rujukan
- [`RanLogic/s_NetGlobal.h`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogic/s_NetGlobal.h) — enum `EMNET_MSG` (semua `NET_MSG_GM_*`)
- [`RanLogicServer/FieldServer/GLGaeaServerMsg.cpp`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/FieldServer/GLGaeaServerMsg.cpp) — tabel dispatch `m_MsgFunc` + handler
- [`RanLogic/Network/NetLogicDefine.h`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogic/Network/NetLogicDefine.h) — enum `EMUSERTYPE` (grade)
- [`docs/08_source_tree_map.md`](08_source_tree_map.md) — peta source (di mana semua ini hidup)
- [`docs/02_server_components/session_server.md`](02_server_components/session_server.md) — distribusi perintah GM lintas server
