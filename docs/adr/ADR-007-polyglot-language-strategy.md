# Architecture Decision Record (ADR)
## ADR-007: Strategi Bahasa Polyglot — Port C++ untuk Core, Bahasa Modern (mis. Go) untuk Servis Greenfield

* **Status**: **Accepted**
* **Date**: 2026-06-17
* **Author**: Lead Infrastructure Engineer & Claude (AI Coding Assistant)
* **Konteks pemicu**: pertanyaan "apakah server game wajib C++? bisa pakai Go?" — dijawab di sini agar jadi keputusan tetap, bukan diperdebatkan ulang tiap chip.

---

## 1. Context (Konteks)

Server Ran Online (*Smtm*) ditulis C++ (MSVC 2008) ~20 tahun. [ADR-001](ADR-001-cloud-native-vs-rejuvenation.md) memutuskan **Hybrid (port C++ ke Linux, bukan rewrite)**. Saat membangun sistem *baru* untuk reborn (engine indeks-pasar, arena, admin/GM console, BFF mobile), muncul pertanyaan wajar: **haruskah semua tetap C++, atau boleh pakai bahasa lain seperti Go/Rust/Elixir?**

Keyakinan umum "game server wajib C++" sudah usang: Go (gateway/chat/matchmaking), Erlang/Elixir (konkuren masif), JVM (banyak MMO), C# (Photon), Rust (server baru perf-tinggi) semua terbukti di produksi. Maka keputusannya bukan soal "boleh/tidak", melainkan **di mana batasnya**.

---

## 2. Decision Drivers (Faktor Pendorong Keputusan)

1. **Biaya migrasi vs nilai aset**: ~20 tahun logika game (Gaea: combat/AI/balance + glogic rules) adalah aset. Rewrite = membuang aset + kehilangan kemampuan verifikasi ke sumber asli.
2. **Profil beban tiap servis**: I/O-bound (routing+DB) vs compute-bound real-time sim (tick Gaea) menuntut karakteristik bahasa berbeda.
3. **A2 — cloud-exit / minim lock-in**: tidak terkunci ke satu vendor *maupun* satu bahasa; binary portabel & deploy sederhana.
4. **Keamanan**: kelas bug C++ (buffer/crypto/injection) — beberapa sudah ditemukan saat review — lebih jarang di bahasa memory-safe.
5. **Velocity & pembelajaran (EA)**: kecepatan iterasi servis baru + nilai belajar tim.

---

## 3. Options Considered (Opsi yang Dipertimbangkan)

### Opsi 1: Semua C++ (seragam dengan core)
* **+** Satu toolchain; reuse penuh `SigmaCore`/protokol; tak ada batas bahasa.
* **−** Servis greenfield (web/GM API, indeks pasar, BFF) jadi lambat dikembangkan & rawan bug di area yang tak butuh kontrol low-level. Tak memanfaatkan ekosistem modern (HTTP/gRPC, concurrency).

### Opsi 2: Semua Go (rewrite total termasuk Gaea)
* **+** Seragam, memory-safe, deploy mudah.
* **−** **Fatal**: rewrite Gaea = multi-tahun, *bug-for-bug mustahil*, membuang aset & verifiabilitas. Melanggar premis ADR-001. GC & layout kurang ideal untuk sim entity-padat per-tick. **Jebakan klasik yang menelan tim.**

### Opsi 3: Polyglot per batas-servis (port C++ core + Go untuk greenfield) — **DIPILIH**
* **+** Port yang terbukti (faithful, bisa diverifikasi); bangun-baru di bahasa paling pas. Arsitektur **sudah** terpecah jadi 5+ servis dengan batas pesan bersih → batas bahasa = batas servis. Selaras A2 (tak lock-in ke satu bahasa).
* **−** Lebih dari satu toolchain & skill; perlu kontrak antar-servis yang jelas (wire protocol / gRPC); duplikasi tipe lintas-bahasa.

---

## 4. Pemetaan: bahasa per servis

| Servis | Sifat beban | Bahasa | Alasan |
| :--- | :--- | :--- | :--- |
| **FieldServer / Gaea** | compute-bound, real-time tick, entity padat | **C++** (port) | logika legacy + kontrol memory/cache; rewrite = buang aset |
| **AgentServer** | gateway in-game + proxy, sebagian logika legacy | **C++** (port) | terikat protokol & logika existing |
| **Auth / Login / Session** | I/O-bound, routing + DB | **C++ (port) sekarang**; boleh dipertimbangkan ulang nanti | sudah di-port & terbukti; bukan prioritas rewrite |
| **Engine Indeks Pasar / Ekonomi** | greenfield, service | **Go** (kandidat) | tak ada legacy; concurrency + iterasi cepat (lihat ADR-005) |
| **Arena / Matchmaking baru** | greenfield, I/O + state | **Go** (kandidat) | goroutine pas untuk match state; scale-to-zero (ADR-006) |
| **Admin / GM API** (177 GM command → REST/gRPC) | greenfield, web | **Go** (kandidat) | console + audit log; memory-safe |
| **BFF client mobile / chat relay / analytics** | greenfield, I/O | **Go** (kandidat) | static binary, deploy sederhana, A2 |

> Aturan emas: **port yang terbukti, bangun-baru yang belum ada.** Batas servis adalah temannya.

---

## 5. Decision (Keputusan)

Adopsi **strategi polyglot berbasis batas-servis**:

1. **Core legacy tetap C++** (yang sedang/akan di-port): Gaea/Field, Agent, dan layer shared (`SigmaCore`/`RanLogic`). **Tidak ada rewrite Gaea.**
2. **Servis greenfield bebas memilih bahasa modern** — **Go sebagai default** untuk servis I/O-bound/web baru (memory-safe, deploy mudah, A2-friendly), kecuali ada alasan kuat lain (mis. Rust untuk perf-kritis, Elixir untuk konkurensi ekstrem).
3. **Kontrak antar-servis eksplisit**: komunikasi lintas-bahasa lewat protokol terdefinisi (wire `NET_MSG_*` existing untuk client legacy; **gRPC/protobuf** disarankan untuk servis baru). Hindari sharing struct biner lintas-bahasa secara implisit.
4. **Larangan keras**: jangan tergoda "Go untuk semua biar seragam" lalu menyeret rewrite core — itu membatalkan ADR-001.

---

## 6. Consequences (Konsekuensi)

* **Positif**: aset legacy terjaga & terverifikasi; servis baru cepat & aman dikembangkan; tak lock-in ke satu bahasa (A2); permukaan keamanan servis web mengecil (memory-safe).
* **Negatif / biaya**: dua+ toolchain & CI lokal; tim perlu C++ *dan* Go; perlu disiplin kontrak antar-servis (skema gRPC, versi-oning). Duplikasi definisi tipe lintas-bahasa harus dikelola (generator dari satu sumber kebenaran bila perlu).
* **Operasional**: `make verify` (gate lokal, ADR-001) tetap untuk C++; servis Go dapat gate build sendiri yang juga **portabel & bebas hosted-CI** (sejalan kebijakan no-GitHub-Actions di [doc 07 §10.5]).

---

## 7. Open items / tindak lanjut

* Pilihan default servis baru (**Go**) belum diuji via spike — buat *spike kecil* (mis. GM Admin API atau skeleton indeks-pasar di Go + gRPC ke satu servis C++) saat fase greenfield dimulai, untuk memvalidasi kontrak lintas-bahasa.
* Keputusan ini **tidak** mengubah urutan kerja sekarang (port C++ shared-layer → fan-out → fitur). Ia berlaku saat **sistem baru reborn** mulai dibangun.
* Bahasa untuk servis perf-kritis non-legacy (jika ada) = evaluasi Rust vs Go saat kasusnya muncul.

---

## 8. Kesimpulan

Server game **tidak wajib** C++. Untuk Ran Online: **C++ untuk core legacy karena rewrite-nya mahal & berisiko (bukan karena Go lemah)**, dan **bahasa modern (Go) untuk servis greenfield**. Polyglot per batas-servis memaksimalkan nilai aset lama sekaligus velocity & keamanan sistem baru — dan itu justru memperkuat A2 (tak terkunci pada satu vendor maupun satu bahasa). Lihat [[ran-online-modernization-adr]], [ADR-001](ADR-001-cloud-native-vs-rejuvenation.md), [doc 06 §8](../06_master_plan.md), [doc 08](../08_source_tree_map.md).
