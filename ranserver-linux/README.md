# ranserver-linux — cross-platform build foundation

The Linux/cross-platform build root for the Ran Online server port (ADR-001 Hybrid).
This is the **template the 5 server ports fan out from** — it unifies the three
shared-layer pieces validated by the spikes into one CMake project:

| Piece | File | Validated by |
| :--- | :--- | :--- |
| Win32 type abstraction | `platform/win32_compat.h` | this foundation |
| Network (IOCP → epoll) | `boost::asio` | [Spike #2](../docs/runbooks/asio-spike.md) |
| DB connector (ADO/COM → ODBC) | `unixODBC` / `msodbcsql18` | [Spike #1](../docs/runbooks/msodbcsql-spike.md) |

`db/` — `sc::db::OdbcDb`: the first real `SigmaCore` module ported — the core
`CjADO` read path on ODBC (see [DB-layer port runbook](../docs/runbooks/db-layer-port.md)).
Targets: `ran_db` (static lib) + `db_smoke`.

## Build & verify

**Primary gate — local dev container** (free, portable, account-independent;
aligns with pilar 4 OpEx + A2 cloud-exit):

```bash
make verify        # clean-Linux build + run inside the dev container
```

Native local build (needs `cmake`, `g++`, `libboost-dev`, `unixodbc-dev`):

```bash
make build && make run     # or: cmake -B build -S . && cmake --build build && ./build/foundation_smoke
```

> **No hosted CI by design.** `make verify` is the sole, canonical build gate —
> local, portable, account-independent (A2 cloud-exit / minimize vendor lock-in).
> There is intentionally **no GitHub Actions** (or any brand-specific CI). If a
> hosted runner is ever needed it would be a portable cloud-build, but that is
> not on the radar. Rationale: [`docs/07_…` §10.5](../docs/07_ai_delivery_operating_model.md).

Background, the porting findings (e.g. the `DWORD` clash between the Win32 shim
and ODBC `sqltypes.h`), and next steps (expand the shim, CMake-ify `SigmaCore`,
then fan-out the servers) are in
[`docs/runbooks/build-foundation.md`](../docs/runbooks/build-foundation.md).
