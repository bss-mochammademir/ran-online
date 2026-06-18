// Golden-vector smoke for the self-contained MessagePack encoder.
//
// The golden bytes below were produced by the independent Python `msgpack`
// package (use_bin_type=True) — see commit message for the generator script.
// If sc::mp::Packer ever drifts from canonical MessagePack, this fails.
#include "msgpack_min.h"
#include "../servers/agentserver/char_lobby.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
static std::string hex(const std::vector<char>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (unsigned char c : b) { s += d[c >> 4]; s += d[c & 0xF]; }
    return s;
}
static void check(const char* what, const std::vector<char>& got, const std::string& want) {
    bool ok = (hex(got) == want);
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        std::printf("    got  %s\n    want %s\n", hex(got).c_str(), want.c_str());
        ++g_fail;
    }
}

int main() {
    using namespace sc::mp;
    using namespace sc::servers;
    std::printf("msgpack_min smoke — golden vectors vs Python `msgpack`.\n");

    // Scalar primitives.
    { std::vector<char> b; Packer p(b); p.UInt(7);            check("uint fixint", b, "07"); }
    { std::vector<char> b; Packer p(b); p.UInt(300);          check("uint16", b, "cd012c"); }
    { std::vector<char> b; Packer p(b); p.UInt(5000000000ULL);check("uint64", b, "cf000000012a05f200"); }
    { std::vector<char> b; Packer p(b); p.Int(-7);            check("neg fixint", b, "f9"); }
    { std::vector<char> b; Packer p(b); p.Int(-70000);        check("int32", b, "d2fffeee90"); }
    { std::vector<char> b; Packer p(b); p.Str("hero");        check("fixstr", b, "a46865726f"); }
    { std::vector<char> b; Packer p(b); p.Bool(true);         check("bool true", b, "c3"); }

    // Full NET_LOBBY_CHARINFO_AC structural golden (array[1]{ SCHARINFO_LOBBY }).
    CharInfoLobby c;
    c.m_dwCharID = 1001; c.m_ClubDbNum = 0; c.m_emClass = 2; c.m_wSchool = 1;
    c.m_wHair = 3; c.m_wFace = 4; c.m_wSex = 0; c.m_wHairColor = 5;
    c.m_sHP.dwData = 250;
    c.m_sExperience.lnData1 = 123456789; c.m_sExperience.lnData2 = 0;
    c.m_nBright = -10; c.m_wLevel = 50;
    c.m_sStats.wPow = 10; c.m_sStats.wStr = 11; c.m_sStats.wSpi = 12;
    c.m_sStats.wDex = 13; c.m_sStats.wInt = 14; c.m_sStats.wSta = 15;
    // m_PutOnItems default = SLOT_TSIZE(20) empties.
    c.m_sSaveMapID.dwID = 100; c.m_ClubRank = 0;
    c.m_ChaName = "TestHero"; c.m_ClubName = "";
    c.m_Lock = false; c.m_bRanMobile = false;

    std::vector<char> ac;
    { Packer p(ac); PackLobbyCharInfoAc(p, c); }
    check("NET_LOBBY_CHARINFO_AC full",
          ac,
          "91dc0014cd03e90002010304000591ccfa92ce075bcd1500f632960a0b0c0d0e0fdc0014969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000969100910000000000916400a8546573744865726fa0c2c2");

    std::printf(g_fail == 0 ? "\nMSGPACK_MIN SMOKE OK\n" : "\nMSGPACK_MIN SMOKE FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
