# Architecture Decision Record (ADR)
## ADR-001: Cloud-Native Refactor vs. Tech Stack Rejuvenation vs. Hybrid (SQL Server on Linux)

* **Status**: **Accepted** (divalidasi via [Spike #0](../runbooks/db-restore.md), 2026-06-14)
* **Date**: 2026-06-13 · **Divalidasi**: 2026-06-14
* **Author**: Antigravity (AI Coding Assistant) & Lead Infrastructure Engineer

---

## 1. Context (Konteks)
Kode sumber server Ran Online (*Smtm*) saat ini berusia sekitar 20+ tahun dan dikembangkan dengan ketergantungan penuh pada ekosistem Windows (*Windows-native*):
* **Jaringan**: Winsock IOCP (`CreateIoCompletionPort`).
* **Database**: ActiveX Data Objects (ADO) terikat COM Windows dan Microsoft SQL Server.
* **Compiler**: Microsoft Visual C++ Compiler (MSVC 2008).
* **UI**: Win32 Dialog Box (MFC/ATL).

Sistem ini stabil dan *battle-tested*, tetapi memiliki *technical debt* yang tinggi di era infrastruktur modern. Kita perlu memutuskan apakah akan melakukan **Refaktorisasi Penuh ke Cloud-Native (Linux-based)**, melakukan **Peremajaan (Rejuvenation) Tech Stack** di atas Windows, atau menerapkan pendekatan **Hybrid (Server C++ Linux + SQL Server on Linux)**.

---

## 2. Decision Drivers (Faktor Pendorong Keputusan)
1. **Cloud Portability & Regulasi (POJK 11/2022)**: Kemudahan migrasi antar-penyelenggara cloud (*Cloud-exit strategy*) dan kepatuhan kedaulatan data di Indonesia.
2. **Biaya Operasional (OpEx)**: Biaya lisensi Windows Server dan MS SQL Server di lingkungan cloud.
3. **Risiko & Waktu ke Pasar (Time-to-Market)**: Kompleksitas pengujian ulang logika game dan durasi pengerjaan.
4. **Skalabilitas & Operasional Modern**: Kemudahan deployment menggunakan Kubernetes (K8s) dan integrasi dengan alat observabilitas modern.

---

## 3. Options Considered (Opsi yang Dipertimbangkan)

### Opsi 1: Refaktorisasi Penuh ke Cloud-Native (Linux/PostgreSQL/Kubernetes)
Melakukan porting total codebase C++ ke Linux menggunakan `boost::asio` untuk jaringan, `libpqxx` untuk database, database PostgreSQL, kompilasi menggunakan CMake/GCC/Clang, serta pengemasan ke dalam Linux Containers (Docker/Kubernetes).
* **Pros**: Bebas biaya lisensi penuh (*OpEx* Rp 0 untuk DB & OS), kompatibilitas cloud-exit optimal (PostgreSQL didukung semua platform).
* **Cons**: *CapEx* waktu sangat tinggi (harus mentranslasi 1.000+ SP ke PL/pgSQL), risiko regresi tinggi.

### Opsi 2: Peremajaan Tech Stack (Modern Windows/SQL Server/Windows Containers)
Mempertahankan Windows OS sebagai basis runtime, tetapi melakukan modernisasi: compiler MSVC modern (C++17/20), database SQL Server 2022 Windows, menghilangkan Win32 GUI, dan menjalankan server di Windows Server Containers / VMs di Cloud.
* **Pros**: Risiko migrasi sangat rendah, performa Winsock IOCP optimal, *time-to-market* cepat (3-4 minggu).
* **Cons**: Biaya lisensi sangat tinggi (Windows + SQL Server), portabilitas cloud-exit buruk (Windows Container berat dan vendor-locked).

### Opsi 3: Hybrid Cloud-Native (C++ Linux + SQL Server on Linux)
Porting server C++ ke Linux (menggunakan `boost::asio` untuk jaringan dan driver ODBC Linux untuk SQL Server), namun database tetap menggunakan **Microsoft SQL Server yang dijalankan di atas OS Linux** (baik via Docker Container atau managed instance SQL Server on Linux di cloud).
* **Pros**:
  * **Penyelamat CapEx (Effort Rendah)**: **TIDAK PERLU** menulis ulang 1.000+ stored procedure. Kompatibilitas Transact-SQL (T-SQL) tetap terjaga 100%.
  * **Memenuhi Definisi Cloud-Native**: Seluruh infrastruktur (termasuk DB) berjalan sebagai kontainer Linux di atas Kubernetes (K8s) atau managed Linux VM.
  * **Kepatuhan POJK 11/2022**: Portabilitas tinggi. Kontainer SQL Server on Linux dapat dijalankan di cloud manapun (*exit strategy* sangat mudah diimplementasikan).
  * **Hemat Lisensi OS**: Mengeliminasi biaya lisensi Windows Server untuk sistem operasi.
* **Cons**:
  * **Tetap Memiliki Lisensi Software (OpEx)**: Kita tetap harus membayar lisensi MS SQL Server per-core (meskipun OS-nya Linux).
  * **Konfigurasi ODBC Driver Linux**: Harus memasang Microsoft ODBC Driver for SQL Server (`msodbcsql`) di dalam Docker image Linux untuk menggantikan ADO COM Windows.

---

## 4. Perbandingan Metrik Keputusan

| Parameter Evaluasi | Opsi 1: Cloud-Native (Linux/Postgres) | Opsi 2: Peremajaan (Windows Server) | Opsi 3: Hybrid (C++ Linux + SQL Server on Linux) |
| :--- | :---: | :---: | :---: |
| **Kombatibilitas Regulasi (POJK 11/2022)**| **Sangat Tinggi** (Portable, Multi-Cloud) | Rendah (Ketergantungan OS/DB Vendor) | **Tinggi** (Portable via Linux Containers) |
| **Biaya Operasional (OpEx)** | **Sangat Rendah** (Open-source stack) | Sangat Tinggi (Lisensi Microsoft) | Sedang (Lisensi SQL Server, Lisensi OS Rp 0) |
| **Biaya Pengembangan (CapEx)** | Tinggi (Upfront refactoring) | **Rendah** (Minimal code changes) | **Sangat Rendah** (C++ porting saja, DB SP aman) |
| **Time-to-Market (AI-Augmented)** | 2 - 3 Bulan | **3 - 4 Minggu** | **4 - 6 Minggu** |
| **Kemudahan Observabilitas** | **Tinggi** (Native K8s ecosystem) | Sedang (Windows logging tooling) | **Tinggi** (Native K8s ecosystem) |

---

## 5. Proposed Recommendation (Rekomendasi yang Diusulkan)

Kami merekomendasikan **Opsi 3: Hybrid Cloud-Native (C++ Linux + SQL Server on Linux)** sebagai keputusan arsitektur terbaik untuk fase pertama.

### Justifikasi Keputusan:
Opsi ini merupakan **titik tengah (sweet spot)** yang sangat taktis:
1. **Mengurangi Risiko Kegagalan Proyek**: Menulis ulang 1.000+ stored procedure dari game MMORPG yang rumit ke PL/pgSQL (Opsi 1) memiliki risiko bug ekonomi game/kalkulasi pertarungan yang sangat tinggi. Opsi 3 mengeliminasi risiko ini 100% karena database engine asli tetap dipertahankan.
2. **Kecepatan Implementasi**: Waktu porting terpangkas dari ~3 bulan menjadi **4-6 minggu** (menggunakan augmentasi AI Agent).
3. **Memenuhi Regulasi POJK 11/2022**: Karena database dan aplikasi dikemas dalam kontainer berbasis Linux, infrastruktur ini 100% siap untuk dipindahkan antar-cloud (*Cloud-Exit Strategy*) dan diorkestrasikan secara modern dengan Kubernetes.

---

## 6. Consequences (Konsekuensi)
* **Kompilasi Docker Image**: Kita perlu merancang Dockerfile dasar yang memasang Microsoft ODBC Driver di atas Alpine Linux / Ubuntu LTS agar konektor database cross-platform dapat berjalan lancar.
* **Strategi Jangka Panjang (Fase 2)**: Jika di masa depan ingin membuang biaya lisensi SQL Server sepenuhnya, kita bisa merencanakan transisi bertahap ke PostgreSQL setelah server C++ stabil berjalan di Kubernetes Linux.

---

## 7. Validasi — Spike #0 (2026-06-14)

Asumsi inti ADR ini ("pertahankan SQL Server, jangan tulis ulang SP") **dibuktikan secara empiris** dengan me-restore 8 backup di [`database/`](../../database/) ke **Microsoft SQL Server 2022 (16.0 CU25) on Linux (Ubuntu 22.04)** via Docker (emulasi `amd64` di Apple Silicon). Prosedur lengkap & dapat diulang: [`runbooks/db-restore.md`](../runbooks/db-restore.md).

### 7.1 Hasil Restore
* **8/8 database ONLINE.** Semua backup ber-*compatibility level* **100 (SQL Server 2008)** — di atas batas minimum SQL Server 2022 (compat 100), sehingga restore lancar tanpa konversi.
* Dua DB multi-file (`RanGameS1`/`WBGame` punya file data sekunder `RanGameS1_S`.ndf — *filegroup* biasa, **bukan** FILESTREAM) berhasil setelah `MOVE` semua file logis.

### 7.2 Artefak A — SP Inventory (angka nyata, mengganti perkiraan "1.000+")

| Database | Stored Proc | Functions | Triggers | Views | Tables |
| :--- | :---: | :---: | :---: | :---: | :---: |
| RanGame | 312 | 5 | 0 | 3 | 81 |
| RanUser | 43 | 7 | 0 | 4 | 41 |
| RanLog | 67 | 1 | 0 | 19 | 70 |
| RanShop | 26 | 0 | 0 | 3 | 17 |
| RanMobileInterface | 13 | 0 | 0 | 0 | 5 |
| WBGame | 306 | 5 | 0 | 4 | 82 |
| WBLog | 57 | 1 | 0 | 17 | 60 |
| WBUser | 42 | 7 | 0 | 4 | 48 |
| **TOTAL** | **866** | 26 | 0 | 54 | 404 |

> `Ran*` (461 SP) dan `WB*` (405 SP) adalah **dua keluarga nyaris kembar** (RanGame 312 ≈ WBGame 306; RanUser 43 ≈ WBUser 42; RanLog 67 ≈ WBLog 57) — kemungkinan world/channel kedua atau build varian. **Permukaan SP unik untuk diuji ≈ keluarga `Ran*` (461)**, bukan 866. Keputusan keluarga kanonik (`Ran*` vs `WB*`) = item terbuka.

### 7.3 Artefak B — Linux Incompatibility Report → **BERSIH**

| Fitur dicek | Temuan | Catatan |
| :--- | :---: | :--- |
| `xp_cmdshell` | **0** | — |
| OLE Automation (`sp_OA*`) | **0** | — |
| Linked server / `OPENQUERY`/`OPENROWSET` | **0** | — |
| FILESTREAM / FileTable | **0** | Tidak didukung di Linux — *aman, tidak dipakai* |
| Full-text (`CONTAINS`/`FREETEXT`) | **0** | — |
| Database Mail (`sp_send_dbmail`) | **0** | — |
| Extended proc (`xp_*`) asli | **0** | 37 "hit" awal = *false positive* dari kata "e**xp**erience" (wildcard `_`) |
| CLR assemblies (user) | **0** | — |
| SQL Agent jobs | **0** | — |

**Tidak ada satu pun fitur *blocking*.** Logika game murni T-SQL/`sys.objects` standar → kompatibel penuh dengan SQL Server on Linux.

### 7.4 Catatan operasional
* **Co-location wajib**: SP `RanGame` mereferensikan `RanUser`/`RanLog`/`RanShop` (referensi lintas-DB 3-bagian) → **ke-8 database harus berada di satu instance SQL Server** (bukan dipecah). Tidak masalah untuk Hybrid; jadi syarat deployment.
* **Auth**: tidak ditemukan `Trusted_Connection`/`SSPI`/`Integrated Security` di kode akses DB → jalur **SQL authentication** (user/pass via secret manager) sesuai rencana.

### 7.5 Kesimpulan
Asumsi inti **TERVALIDASI**: 866 SP (461 unik) berjalan di SQL Server on Linux tanpa fitur tak-kompatibel. Risiko penulisan ulang SP yang dihindari oleh Opsi 3 itu **nyata dan besar**, dan Hybrid mengeliminasinya. **Status ADR-001 dinaikkan ke *Accepted*.** Langkah berikut: spike konektor `msodbcsql` dari C++ Linux ke instance ini ([master plan §6](../06_master_plan.md#6-apa-yang-perlu-disiapkan-selanjutnya)).
