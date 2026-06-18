#ifndef SC_SERVERS_CHAR_LOBBY_H
#define SC_SERVERS_CHAR_LOBBY_H

#include <cstdint>
#include <string>
#include <vector>
#include "msgpack_min.h"

// Wire-faithful mirror of SCHARINFO_LOBBY and its msgpack sub-structs, used to
// build the NET_MSG_LOBBY_CHARINFO_AC character-detail response (char-select
// screen). Each Pack() reproduces the original MSGPACK_DEFINE field list IN
// ORDER — msgpack-c serializes a struct as a fixarray of its members, so the
// array shapes below must match the client's structs exactly.
//
// Source structs (cp1252 originals):
//   SCHARINFO_LOBBY  RanLogic/Msg/GLContrlBaseMsg.h:277
//   GLPADATA         enginelib/G-Logic/GLDefine.h:345   MSGPACK_DEFINE(dwData)
//   GLLLDATA         enginelib/G-Logic/GLDefine.h:695   MSGPACK_DEFINE(lnData1,lnData2)
//   SCHARSTATS       RanLogic/Character/GLCharDefine.h:303 (wPow,wStr,wSpi,wDex,wInt,wSta)
//   SNATIVEID        enginelib/G-Logic/TypeDefine.h:21  MSGPACK_DEFINE(dwID)
//   SITEM_LOBY       RanLogic/Item/GLItem.h:308
//   NET_LOBBY_CHARINFO_AC  RanLogic/Msg/GLContrlCharJoinMsg.h:260  MSGPACK_DEFINE(Data)
namespace sc { namespace servers {

// GLPADATA: union over {dwData} / {wNow,wMax}. MSGPACK_DEFINE(dwData).
struct GlPaData {
    uint32_t dwData = 0;   // wNow | (wMax << 16)
    void Pack(sc::mp::Packer& p) const { p.Array(1); p.UInt(dwData); }
};

// GLLLDATA: union over {lnData1,lnData2}. MSGPACK_DEFINE(lnData1, lnData2).
struct GlLlData {
    int64_t lnData1 = 0;   // lnNow (current exp)
    int64_t lnData2 = 0;   // lnMax (next-level exp; computed client-side via GLNEEDEXP)
    void Pack(sc::mp::Packer& p) const { p.Array(2); p.Int(lnData1); p.Int(lnData2); }
};

// SCHARSTATS. MSGPACK_DEFINE(wPow, wStr, wSpi, wDex, wInt, wSta).
struct CharStats {
    uint16_t wPow = 0, wStr = 0, wSpi = 0, wDex = 0, wInt = 0, wSta = 0;
    void Pack(sc::mp::Packer& p) const {
        p.Array(6);
        p.UInt(wPow); p.UInt(wStr); p.UInt(wSpi);
        p.UInt(wDex); p.UInt(wInt); p.UInt(wSta);
    }
};

// SNATIVEID: union over {dwID} / {wMainID,wSubID}. MSGPACK_DEFINE(dwID).
struct NativeId {
    uint32_t dwID = 0;
    void Pack(sc::mp::Packer& p) const { p.Array(1); p.UInt(dwID); }
};

// SITEM_LOBY. MSGPACK_DEFINE(sNativeID, nidDISGUISE, dwMainColor, dwSubColor, cDAMAGE, cDEFENSE).
struct ItemLoby {
    NativeId sNativeID;
    NativeId nidDISGUISE;
    uint32_t dwMainColor = 0;
    uint32_t dwSubColor  = 0;
    uint8_t  cDAMAGE     = 0;
    uint8_t  cDEFENSE    = 0;
    void Pack(sc::mp::Packer& p) const {
        p.Array(6);
        sNativeID.Pack(p);
        nidDISGUISE.Pack(p);
        p.UInt(dwMainColor);
        p.UInt(dwSubColor);
        p.UInt(cDAMAGE);
        p.UInt(cDEFENSE);
    }
};

// SLOT_TSIZE = 20 (RanLogic/Item/GLItemDef.h:595). Default-constructed
// SCHARINFO_LOBBY reserves this many empty SITEM_LOBY equip slots.
constexpr int SLOT_TSIZE = 20;

// SCHARINFO_LOBBY. 20-field MSGPACK_DEFINE — order must match exactly.
struct CharInfoLobby {
    uint32_t  m_dwCharID    = 0;
    uint32_t  m_ClubDbNum   = 0;
    uint32_t  m_emClass     = 0;   // EMCHARCLASS
    uint16_t  m_wSchool     = 0;
    uint16_t  m_wHair       = 0;
    uint16_t  m_wFace       = 0;
    uint16_t  m_wSex        = 0;
    uint16_t  m_wHairColor  = 0;
    GlPaData  m_sHP;
    GlLlData  m_sExperience;
    int32_t   m_nBright     = 0;
    uint16_t  m_wLevel      = 1;
    CharStats m_sStats;
    std::vector<ItemLoby> m_PutOnItems;   // default: SLOT_TSIZE empties
    NativeId  m_sSaveMapID;
    uint32_t  m_ClubRank    = 0;
    std::string m_ChaName;
    std::string m_ClubName;
    bool      m_Lock        = false;
    bool      m_bRanMobile  = false;

    CharInfoLobby() : m_PutOnItems(SLOT_TSIZE) {}

    void Pack(sc::mp::Packer& p) const {
        p.Array(20);
        p.UInt(m_dwCharID);
        p.UInt(m_ClubDbNum);
        p.UInt(m_emClass);
        p.UInt(m_wSchool);
        p.UInt(m_wHair);
        p.UInt(m_wFace);
        p.UInt(m_wSex);
        p.UInt(m_wHairColor);
        m_sHP.Pack(p);
        m_sExperience.Pack(p);
        p.Int(m_nBright);
        p.UInt(m_wLevel);
        m_sStats.Pack(p);
        p.Array(static_cast<uint32_t>(m_PutOnItems.size()));
        for (const auto& it : m_PutOnItems) it.Pack(p);
        m_sSaveMapID.Pack(p);
        p.UInt(m_ClubRank);
        p.Str(m_ChaName);
        p.Str(m_ClubName);
        p.Bool(m_Lock);
        p.Bool(m_bRanMobile);
    }
};

// NET_LOBBY_CHARINFO_AC: MSGPACK_DEFINE(Data) -> array[1]{ SCHARINFO_LOBBY }.
inline void PackLobbyCharInfoAc(sc::mp::Packer& p, const CharInfoLobby& data) {
    p.Array(1);
    data.Pack(p);
}

}} // namespace sc::servers

#endif // SC_SERVERS_CHAR_LOBBY_H
