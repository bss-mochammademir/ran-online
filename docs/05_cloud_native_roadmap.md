# Peta Jalan & Asesmen Kelayakan Cloud-Native

Dokumen ini menyajikan penilaian kelayakan (*feasibility assessment*) dan rencana migrasi arsitektur Ran Online server dari lingkungan *on-premise Windows-centric* ke arsitektur **Cloud-Native cross-platform** yang aman, fleksibel, dan mematuhi regulasi ketat (seperti prinsip tata kelola IT perbankan OJK - POJK 11/2022).

---

## Target Arsitektur Masa Depan (Target State)

```mermaid
graph TD
    Client[Client Game] -->|Ingress / Load Balancer| K8sCluster[Kubernetes Cluster: EKS / GKE / On-Premises]
    
    subgraph K8sCluster
        direction TB
        LoginSvc[Login Pods]
        AgentSvc[Agent Pods]
        SessionSvc[Session Pods]
        FieldSvc[Field Pods]
        CacheSvc[Cache Pods]
    end
    
    CacheSvc -->|Read/Write| DB[(Managed PostgreSQL: RDS / Cloud SQL)]
    LoginSvc & AgentSvc & SessionSvc -.->|Service Discovery / Mesh| K8sCluster
```

---

## Analisis Kelayakan Porting (Feasibility Assessment)

Porting server Ran Online langsung dari C++ Windows ke Linux C++ adalah tugas yang menantang namun sangat layak dengan pendekatan modular.

### 1. Jaringan & I/O (IOCP vs Epoll)
* **Kelayakan**: **Sedang**
* **Analisis**: Winsock IOCP (`CreateIoCompletionPort`) sangat berbeda secara arsitektur dengan Linux `epoll` atau macOS `kqueue`.
* **Rekomendasi**: Mengintegrasikan **`boost::asio`** atau **`libuv`** ke dalam [NetServer.cpp](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/Server/NetServer.cpp) untuk mengabstraksi lapisan jaringan secara cross-platform tanpa menulis ulang logika perutean paket.

### 2. Layer Database (ADO/OLE DB vs PostgreSQL)
* **Kelayakan**: **Rendah ke Sedang** (Memerlukan upaya re-engineering terbesar)
* **Analisis**: Dependensi COM OLE DB Windows di [AdoClass.cpp](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Database/Ado/AdoClass.cpp) tidak dapat dijalankan di Linux. Lebih dari 1.000 stored procedure T-SQL SQL Server harus dipindahkan.
* **Rekomendasi**:
  1. Ganti runtime OLE DB dengan driver C++ PostgreSQL native (**`libpqxx`**).
  2. Gunakan alat konversi otomatis (seperti `pgloader`) untuk tabel skema dasar.
  3. Konversi stored procedure kritis secara manual ke fungsi **PL/pgSQL**.

### 3. Kompilasi & Compiler (MSVC vs GCC/Clang)
* **Kelayakan**: **Tinggi**
* **Analisis**: Kode C++ di Ran Online menggunakan ekstensi compiler MSVC (seperti `#pragma once`, `#import`, tipe Windows `DWORD`, `HANDLE`).
* **Rekomendasi**:
  1. Ganti tipe data Win32 dengan pustaka standar C++ (misal: `DWORD` $\rightarrow$ `uint32_t`, `HANDLE` $\rightarrow$ file descriptor / abstraksi kustom).
  2. Buat sistem build menggunakan **CMake** untuk menggantikan file proyek Visual Studio (`.vcproj`).

---

## Peta Jalan Migrasi (Roadmap)

### Fase 1: Abstraksi OS & Database (Tahap Persiapan)
* **Tujuan**: Membuat kode server dapat berjalan di Linux terlebih dahulu (Cross-platform compilation).
* **Langkah**:
  - Konversi file solusi visual studio ke CMake.
  - Implementasi layer database baru menggunakan driver PostgreSQL.
  - Porting socket loop menggunakan `boost::asio`.

### Fase 2: Kontainerisasi (Docker & OCI Images)
* **Tujuan**: Mengemas setiap server ke dalam kontainer Linux yang ringan dan aman.
* **Langkah**:
  - Membuat `Dockerfile` multi-stage untuk kompilasi dan runtime server.
  - Memastikan base image menggunakan distro minimalis yang aman (seperti Alpine Linux atau Distroless) untuk memperkecil celah keamanan (*attack surface*).

### Fase 3: Orkestrasi & Cloud-Native Deployment (Kubernetes)
* **Tujuan**: Skalabilitas dinamis dan pemantauan kesehatan server otomatis.
* **Langkah**:
  - Mengonfigurasi Kubernetes Deployment untuk LoginServer, AgentServer, dan FieldServer.
  - Menggunakan Kubernetes *Headless Services* untuk menjaga rute komunikasi soket tetap presisi antara Agent dan Field Server.

---

## Kepatuhan POJK 11/2022 (Cloud-Exit & Keamanan Perbankan)

Mengingat pentingnya kepatuhan regulasi di sektor keuangan (OJK), refaktor cloud-native ini wajib menerapkan prinsip-prinsip berikut:

### 1. Strategi Keluar Cloud (Cloud-Exit Strategy)
* **Prinsip**: Server tidak boleh bergantung pada layanan proprietari cloud tertentu (seperti AWS Fargate eksklusif atau DB Serverless eksklusif).
* **Penerapan**: Menggunakan standar Kubernetes (K8s) murni dan database SQL standar (PostgreSQL) agar beban kerja server game dapat dipindahkan secara instan ke cloud provider lain (multi-cloud) atau kembali ke pusat data lokal (*on-premises hybrid*) jika terjadi kegagalan sistemik.

### 2. Kedaulatan & Perlindungan Data (Data Sovereignty)
* **Prinsip**: Data pemain dan akun harus disimpan di dalam wilayah hukum Indonesia.
* **Penerapan**: Deployment infrastruktur cloud wajib ditempatkan pada region lokal (misal: AWS Jakarta Region `ap-southeast-3` atau Google Cloud Jakarta `asia-southeast2`).

### 3. Keamanan dengan Prinsip Least Privilege
* **Prinsip**: Hak akses resource seminimal mungkin (Zero IAM Wildcard).
* **Penerapan**: Penggunaan Kubernetes Service Accounts yang terintegrasi dengan IAM Roles (IRSA) untuk membatasi akses Pod server game ke database PostgreSQL. Tidak boleh ada secrets (kredensial DB) yang disimpan keras (*hardcoded*) di dalam file kode sumber atau image Docker; gunakan rahasia Kubernetes atau Cloud Secret Manager.

### 4. Zero-Drift & High Auditability
* **Prinsip**: Seluruh infrastruktur dideklarasikan secara tertulis dan dapat diaudit.
* **Penerapan**: Semua sumber daya cloud (VPC, PostgreSQL Instance, Kubernetes Cluster) dideklarasikan menggunakan **Terraform**. Setiap perubahan infra wajib melalui pipa CI/CD GitOps (misal: Terraform Cloud / Atlantis) untuk mencegah *drift* (perbedaan konfigurasi manual) dan menjaga jejak audit yang bersih.
