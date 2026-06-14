# ranserver-linux — cross-platform build foundation

The Linux/cross-platform build root for the Ran Online server port (ADR-001 Hybrid).
This is the **template the 5 server ports fan out from** — it unifies the three
shared-layer pieces validated by the spikes into one CMake project:

| Piece | File | Validated by |
| :--- | :--- | :--- |
| Win32 type abstraction | `platform/win32_compat.h` | this foundation |
| Network (IOCP → epoll) | `boost::asio` | [Spike #2](../docs/runbooks/asio-spike.md) |
| DB connector (ADO/COM → ODBC) | `unixODBC` / `msodbcsql18` | [Spike #1](../docs/runbooks/msodbcsql-spike.md) |

## Build & run

```bash
# Native (needs cmake, g++, libboost-dev, unixodbc-dev):
cmake -B build -S . && cmake --build build && ./build/foundation_smoke

# Or in the dev container (also installs msodbcsql18 for real DB connects):
docker build -f Dockerfile.dev -t ran-dev .
docker run --rm -v "$PWD":/work ran-dev \
  bash -lc "cmake -B build -S . && cmake --build build && ./build/foundation_smoke"
```

CI builds & runs `foundation_smoke` on `ubuntu-22.04` — see
[`.github/workflows/build-foundation.yml`](../.github/workflows/build-foundation.yml).

Background, the porting findings (e.g. the `DWORD` clash between the Win32 shim
and ODBC `sqltypes.h`), and next steps (expand the shim, CMake-ify `SigmaCore`,
then fan-out the servers) are in
[`docs/runbooks/build-foundation.md`](../docs/runbooks/build-foundation.md).
