# Runbook — Spike #2: Porting Jaringan `boost::asio` (IOCP → epoll)

> **Tujuan**: Membuktikan target porting jaringan [ADR-001](../adr/ADR-001-cloud-native-vs-rejuvenation.md) / [roadmap `05`](../05_cloud_native_roadmap.md) — bahwa loop jaringan **async (accept → read → write)** berjalan di **Linux** via **`boost::asio`**, sebagai pengganti model **Winsock IOCP** (`CreateIoCompletionPort`) di [`NetServer.cpp`](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/RanLogicServer/Server/NetServer.cpp), **tanpa menulis ulang logika perutean paket**.

> ✅ **Sudah dieksekusi 2026-06-14** — hasil di [§Hasil](#hasil-eksekusi-2026-06-14). Status: **LULUS**.

Berjalan **100% lokal** (Docker), tanpa cloud. Build native (tak perlu emulasi).

---

## Pemetaan konsep IOCP → `boost::asio`

| Winsock IOCP (Windows, sekarang) | `boost::asio` (cross-platform) |
| :--- | :--- |
| `CreateIoCompletionPort` | `io_context` (event loop / dispatcher) |
| `GetQueuedCompletionStatus` (worker thread) | `io_context.run()` — **epoll** di Linux, **IOCP** di Windows |
| Overlapped `WSARecv` | `async_read` / `async_read_until` |
| Overlapped `WSASend` | `async_write` |
| `AcceptEx` | `acceptor.async_accept` |

> Intinya: `boost::asio` memakai **IOCP di Windows** dan **epoll di Linux** di balik API yang sama — jadi logika routing paket tetap, hanya lapisan transport yang diabstraksi.

## Langkah 1 — Program C++ (`net_spike.cpp`)
Async echo server (acceptor → session read/write) + client loopback dalam satu proses:

```cpp
#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <memory>
#include <string>
using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket s) : sock_(std::move(s)) {}
    void start(){ read(); }
private:
    void read(){
        auto self = shared_from_this();
        boost::asio::async_read_until(sock_, buf_, '\n',
            [this, self](boost::system::error_code ec, std::size_t){
                if (ec) return;
                std::istream is(&buf_); std::string line; std::getline(is, line);
                auto out = std::make_shared<std::string>("ECHO: " + line + "\n");
                boost::asio::async_write(sock_, boost::asio::buffer(*out),
                    [self, out](boost::system::error_code, std::size_t){});
            });
    }
    tcp::socket sock_;
    boost::asio::streambuf buf_;
};

class Server {
public:
    Server(boost::asio::io_context& io, unsigned short port)
        : acc_(io, tcp::endpoint(tcp::v4(), port)) { accept(); }
private:
    void accept(){
        acc_.async_accept([this](boost::system::error_code ec, tcp::socket sock){
            if (!ec) std::make_shared<Session>(std::move(sock))->start();
            accept();
        });
    }
    tcp::acceptor acc_;
};

int main(){
    try {
        const unsigned short PORT = 9099;
        boost::asio::io_context io;
        Server srv(io, PORT);
        std::cout << "[server] boost::asio io_context up (epoll on Linux; IOCP on Windows)\n";
        std::thread worker([&]{ io.run(); });

        boost::asio::io_context cio; tcp::socket c(cio);
        c.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), PORT));
        std::string msg = "PING from boost::asio on Linux\n";
        boost::asio::write(c, boost::asio::buffer(msg));
        std::cout << "[client] sent: " << msg;
        boost::asio::streambuf in; boost::asio::read_until(c, in, '\n');
        std::istream is(&in); std::string reply; std::getline(is, reply);
        std::cout << "[client] recv: " << reply << "\n";

        io.stop(); worker.join();
        bool ok = (reply == "ECHO: PING from boost::asio on Linux");
        std::cout << (ok ? "\nSPIKE OK: async accept/read/write loop works on Linux via boost::asio.\n"
                         : "\nSPIKE FAILED\n");
        return ok ? 0 : 1;
    } catch (std::exception& e) { std::cerr << "EXC: " << e.what() << "\n"; return 2; }
}
```

## Langkah 2 — Dockerfile
```dockerfile
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ libboost-dev libboost-system-dev \
 && rm -rf /var/lib/apt/lists/*
COPY net_spike.cpp /spike/net_spike.cpp
RUN g++ -O2 -std=c++17 -Wall -o /spike/net_spike /spike/net_spike.cpp -lboost_system -lpthread
CMD ["/spike/net_spike"]
```

## Langkah 3 — Build & jalankan
```bash
docker build -t net-spike .
docker run --rm net-spike
```

---

## Hasil Eksekusi (2026-06-14)
```
[server] boost::asio io_context up (epoll on Linux; IOCP on Windows)
[client] sent: PING from boost::asio on Linux
[client] recv: ECHO: PING from boost::asio on Linux

SPIKE OK: async accept/read/write loop works on Linux via boost::asio.
```

## Kesimpulan & implikasi
- **LULUS.** `io_context` + `async_accept`/`async_read_until`/`async_write` berjalan di Linux (epoll) — pengganti langsung model IOCP.
- **Strategi porting nyata**: ganti lapisan transport di `NetServer.cpp` (CreateIoCompletionPort/overlapped I/O) dengan `io_context` + handler async; **logika MsgManager / routing paket** ([`04_network_protocol`](../04_network_protocol.md)) tetap. Pakai N worker thread `io.run()` untuk meniru pool IOCP.
- **Status shared-layer Fase 1**: dua abstraksi inti tervalidasi — **DB** ([Spike #1 `msodbcsql`](msodbcsql-spike.md)) & **jaringan** (spike ini). Sisa abstraksi = tipe Win32→`uint32_t` + CMake (mekanik, bukan unknown). → **Boleh fan-out porting 5 server** secara paralel (aturan "serial dulu, baru fan-out").

### Bersih-bersih (opsional)
```bash
docker rmi net-spike
```
