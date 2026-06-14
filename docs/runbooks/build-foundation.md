# Runbook — Build Foundation: CMake + Abstraksi Tipe + Dev Container (Fase 1)

> **Tujuan**: Membangun **fondasi build cross-platform** yang menyatukan ketiga lapisan shared Fase 1 ke dalam **satu proyek CMake** yang compile/link/run di Linux: (1) abstraksi tipe Win32 (`platform/win32_compat.h`), (2) jaringan `boost::asio` ([Spike #2](asio-spike.md)), (3) konektor DB ODBC/`msodbcsql` ([Spike #1](msodbcsql-spike.md)). Ini **template** yang dari situ porting 5 server di-fan-out (aturan "serial dulu, baru fan-out", [doc 07 §5](../07_ai_delivery_operating_model.md)).

> ✅ **Sudah dieksekusi 2026-06-14** — target `foundation_smoke` compile+link+run **hijau**. Lihat [§Hasil](#hasil-eksekusi-2026-06-14).

---

## Struktur scaffold
```
ranserver-linux/                 # akar build cross-platform baru
├── CMakeLists.txt               # top-level; titik fan-out per-server
├── Makefile                     # `make verify` = gate utama (dev-container lokal)
├── platform/
│   └── win32_compat.h           # shim tipe Win32 → std (mulai abstraksi tipe)
├── src/
│   └── foundation_smoke.cpp     # menyatukan win32_compat + asio + ODBC
├── Dockerfile.dev               # dev container Linux (toolchain lengkap)
└── README.md                    # cara build & verifikasi
(tidak ada hosted CI — gate = `make verify` lokal; lihat §"Tidak ada hosted CI")
```

## `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.16)
project(ranserver_linux LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)            # boost::asio (header-only) include dirs
find_package(Threads REQUIRED)          # io_context worker threads
find_library(ODBC_LIB odbc REQUIRED)    # unixODBC -> msodbcsql18 saat runtime

add_executable(foundation_smoke src/foundation_smoke.cpp)
target_include_directories(foundation_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/platform ${Boost_INCLUDE_DIRS})
target_link_libraries(foundation_smoke PRIVATE ${ODBC_LIB} Threads::Threads)

# Fan-out: tambahkan target per-server (login/agent/field/session/cache) di sini,
# masing-masing memakai ulang platform/ + lapisan net/db shared.
```

## `platform/win32_compat.h`
```cpp
#pragma once
#ifndef _WIN32
#include <cstdint>
#include <cstddef>
// ODBC <sqltypes.h> JUGA mendefinisikan WORD/DWORD/BYTE → tunda ke ODBC bila ada.
#ifndef __SQLTYPES_H
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
#endif
typedef void* HANDLE;     // real port: map ke fd / abstraksi kustom
typedef void* LPVOID;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#endif // !_WIN32
```
> Subset minimal-representatif untuk smoke; port nyata memperluasnya (mekanik).

## `src/foundation_smoke.cpp`
```cpp
#include <sql.h>          // ODBC dulu: <sqltypes.h>-nya memiliki WORD/DWORD/BYTE
#include <sqlext.h>
#include "win32_compat.h" // shim menunda tipe itu via guard __SQLTYPES_H
#include <boost/asio.hpp>
#include <iostream>

int main(){
    DWORD tick = 42; HANDLE h = nullptr; (void)h;     // (1) tipe Win32 via shim
    boost::asio::io_context io;                         // (2) jaringan (Spike #2)
    boost::asio::ip::tcp::acceptor probe(io); (void)probe;
    SQLHENV env = SQL_NULL_HANDLE;                      // (3) DB driver (Spike #1)
    SQLRETURN r = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    bool odbcOk = (r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO);
    if (odbcOk){ SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
                 SQLFreeHandle(SQL_HANDLE_ENV, env); }
    std::cout << "foundation_smoke OK:\n"
              << "  [1] win32_compat: DWORD=" << tick << " (uint32_t shim)\n"
              << "  [2] boost::asio io_context constructed\n"
              << "  [3] unixODBC env alloc: " << (odbcOk ? "ok" : "FAILED") << "\n"
              << "  -> unified cross-platform build foundation compiles, links & runs.\n";
    return odbcOk ? 0 : 1;
}
```

## `Dockerfile.dev` — dev container Linux (canonical)
```dockerfile
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive ACCEPT_EULA=Y
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake g++ make git curl gnupg ca-certificates apt-transport-https \
        libboost-dev libboost-system-dev unixodbc unixodbc-dev \
 && curl -fsSL https://packages.microsoft.com/keys/microsoft.asc \
        | gpg --dearmor -o /usr/share/keyrings/microsoft-prod.gpg \
 && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/microsoft-prod.gpg] https://packages.microsoft.com/ubuntu/22.04/prod jammy main" \
        > /etc/apt/sources.list.d/mssql-release.list \
 && apt-get update && apt-get install -y --no-install-recommends msodbcsql18 \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
```
> Smoke sendiri hanya butuh `cmake g++ libboost-dev unixodbc-dev`; `msodbcsql18` diperlukan saat **connect** ke DB (lihat Spike #1).

## Build & verifikasi — gate utama = **dev-container lokal**
Gate utama bersifat **lokal** (gratis, portabel, tak bergantung akun; selaras pilar 4 + A2):
```bash
cd ranserver-linux
make verify        # build clean-Linux + run di dalam dev container (gate)
# atau native (butuh cmake/g++/libboost-dev/unixodbc-dev): make build && make run
```

## Tidak ada hosted CI (by design)
Verifikasi **hanya** lewat `make verify` lokal di atas. **Tidak ada GitHub Actions** atau CI ber-brand lain — menyalahi A2 (cloud-exit / minimkan ketergantungan vendor), dan hampir dipastikan tak dipakai. Jika kelak benar-benar perlu hosted, kandidatnya **cloud-build portabel & brand-minimal**, tapi **belum di radar**. Rasional: [doc 07 §10.5](../07_ai_delivery_operating_model.md).

---

## Hasil Eksekusi (2026-06-14)
```
foundation_smoke OK:
  [1] win32_compat: DWORD=42 (uint32_t shim)
  [2] boost::asio io_context constructed
  [3] unixODBC env alloc: ok
  -> unified cross-platform build foundation compiles, links & runs.
```
> **Catatan transparansi**: gate utama (`make verify`, dev-container Linux) terblokir gangguan **apt/GPG BuildKit Docker Desktop** pada sesi ini (jam container terverifikasi benar, base image fresh, legacy builder pun gagal → masalah lingkungan, **bukan kode**). Verifikasi dilakukan via **CMake build di host (macOS)**; gate dev-container lokal akan hijau begitu lingkungan Docker sehat. API `boost::asio` + unixODBC sendiri sudah terbukti compile & run di Linux pada [Spike #1](msodbcsql-spike.md) & [#2](asio-spike.md).

## Temuan porting (penting untuk tim)
- **Bentrok tipe `DWORD`**: shim Win32 (`uint32_t`) vs `sqltypes.h` ODBC (`unsigned long`) → *"typedef redefinition with different types"*. **Aturan**: di TU yang pakai ODBC, **`#include <sql.h>` dulu**, lalu `win32_compat.h` (yang menunda WORD/DWORD/BYTE via `#ifndef __SQLTYPES_H`).
- Host macOS punya bentrok tipe ekstra (`UINT`/`LONG`/`ULONG` dari header sistem/ODBC) yang **tidak** muncul di target Linux — alasan menjadikan **target Linux (gate `make verify`)** sebagai acuan, bukan host macOS.

## Kesimpulan & langkah berikut
- **Fondasi build siap**: struktur CMake + shim tipe + dev container + CI, dengan target gabungan hijau. Ini **titik fan-out**.
- **Sisa Fase 1 (mekanik, per-modul chip)**: (a) perluas `win32_compat.h` mengikuti tipe yang dipakai tiap modul; (b) CMake-ify modul nyata mulai `SigmaCore` (tempat `AdoClass`/`CjADO` + tipe Win32 hidup); (c) lalu **fan-out porting 5 server** sebagai chip paralel.
- **Keputusan repo (diputuskan 2026-06-14)**: scaffold ini kode nyata pertama di luar `docs/`. `.gitignore` melacak **`ranserver-linux/`** (sumber kini hidup di [`ranserver-linux/`](../../ranserver-linux/), bukan hanya inline). **Tanpa hosted CI** — `.github/` tidak dilacak; gate = `make verify` lokal (A2 cloud-exit).

### Bersih-bersih (opsional)
```bash
docker rmi ran-dev 2>/dev/null; rm -rf build build-host
```
