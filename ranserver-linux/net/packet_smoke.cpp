// packet_smoke — deterministic unit test of the packet codec + MsgManager (no
// sockets). Exercises the hard parts of stream framing: partial frames across
// feeds, multiple frames per feed, a header split mid-way, and malformed-length
// rejection — plus EncodeMessage round-trip and the MsgManager Front/Back queue.
#include "packet.h"
#include "msg_manager.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace sc::net;

static int g_fail = 0;
static void check(const char* what, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "  PASS " : "  FAIL ") << what;
    if (!detail.empty()) std::cout << " => " << detail;
    std::cout << "\n";
    if (!ok) ++g_fail;
}

int main() {
    std::cout << "packet codec smoke — NET_MSG_GENERIC framing + MsgManager.\n";

    // (1) EncodeMessage layout: dwSize little-endian = 8 + payload, type at +4.
    {
        const char* pl = "hello";
        auto f = EncodeMessage(0x1234, pl, 5);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(f.data());
        check("encode total size", f.size() == kHeaderSize + 5, std::to_string(f.size()));
        check("encode dwSize field LE", ReadLE32(p) == 13);
        check("encode nType field LE", ReadLE32(p + 4) == 0x1234);
        check("encode payload copied", std::memcmp(f.data() + kHeaderSize, pl, 5) == 0);
    }

    // (2) Two complete frames in one Feed.
    {
        auto a = EncodeMessage(11, "AA", 2);
        auto b = EncodeMessage(22, "BBBB", 4);
        std::vector<char> wire = a; wire.insert(wire.end(), b.begin(), b.end());
        MessageFramer fr; std::vector<Message> out;
        check("feed two-in-one ok", fr.Feed(wire.data(), wire.size(), out));
        check("two messages framed", out.size() == 2, std::to_string(out.size()));
        if (out.size() == 2) {
            check("msg#1 type", out[0].type == 11);
            check("msg#1 payload size", out[0].payloadSize() == 2);
            check("msg#2 type", out[1].type == 22);
            check("msg#2 payload size", out[1].payloadSize() == 4);
        }
        check("no leftover buffered", fr.Buffered() == 0);
    }

    // (3) One frame split across two feeds (payload split) + (4) header split.
    {
        auto m = EncodeMessage(99, "PAYLOAD!", 8);   // total = 16 bytes
        MessageFramer fr; std::vector<Message> out;
        // header split: 3 bytes, then the rest
        fr.Feed(m.data(), 3, out);
        check("partial header -> nothing yet", out.empty() && fr.Buffered() == 3);
        fr.Feed(m.data() + 3, 7, out);             // now 10 bytes (header done, partial payload)
        check("partial payload -> still nothing", out.empty(), "buffered=" + std::to_string(fr.Buffered()));
        fr.Feed(m.data() + 10, m.size() - 10, out);
        check("completed across 3 feeds", out.size() == 1);
        if (out.size() == 1) {
            check("reassembled type", out[0].type == 99);
            check("reassembled payload", std::string(out[0].payload(), out[0].payloadSize()) == "PAYLOAD!");
        }
    }

    // (5) malformed: dwSize > kMaxMessageSize -> error.
    {
        unsigned char bad[8]; WriteLE32(bad, static_cast<uint32_t>(kMaxMessageSize + 1)); WriteLE32(bad + 4, 5);
        MessageFramer fr; std::vector<Message> out;
        check("oversize dwSize rejected", fr.Feed(reinterpret_cast<char*>(bad), 8, out) == false);
    }
    // (6) malformed: dwSize < header -> error.
    {
        unsigned char bad[8]; WriteLE32(bad, 4); WriteLE32(bad + 4, 5);
        MessageFramer fr; std::vector<Message> out;
        check("undersize dwSize rejected", fr.Feed(reinterpret_cast<char*>(bad), 8, out) == false);
    }

    // (7) MsgManager Front/Back double-buffer ordering.
    {
        MsgManager mgr;
        Message m1; m1.type = 1; Message m2; m2.type = 2;
        mgr.MsgQueueInsert(std::move(m1));
        mgr.MsgQueueInsert(std::move(m2));
        check("inserts land in Front", mgr.FrontCount() == 2 && mgr.BackCount() == 0);
        Message tmp;
        check("Back empty before Flip", mgr.GetMsg(tmp) == false);
        mgr.MsgQueueFlip();
        check("Flip moves Front->Back", mgr.FrontCount() == 0 && mgr.BackCount() == 2);
        bool g1 = mgr.GetMsg(tmp); uint32_t t1 = tmp.type;
        bool g2 = mgr.GetMsg(tmp); uint32_t t2 = tmp.type;
        bool g3 = mgr.GetMsg(tmp);
        check("FIFO drain order", g1 && g2 && !g3 && t1 == 1 && t2 == 2);
    }

    std::cout << "\n";
    if (g_fail == 0) { std::cout << "PACKET CODEC SLICE OK: framing + MsgManager double-buffer.\n"; return 0; }
    std::cout << "PACKET CODEC SLICE FAILED: " << g_fail << " assertion(s).\n";
    return 1;
}
