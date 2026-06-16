#include "auth_crypto.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace sc { namespace servers {

namespace {
    typedef uint32_t RC5_WORD;
    constexpr unsigned int r = 16; // 16 rounds
    constexpr RC5_WORD MAGIC_P = 0xb7e15163L;
    constexpr RC5_WORD MAGIC_Q = 0x9e3779b9L;

    inline RC5_WORD rotl(RC5_WORD val, int shift) {
        shift &= 31;
        return (val << shift) | (val >> (32 - shift));
    }

    inline RC5_WORD rotr(RC5_WORD val, int shift) {
        shift &= 31;
        return (val >> shift) | (val << (32 - shift));
    }

    void KeyExpand(const uint8_t* key, unsigned int keylen, std::vector<RC5_WORD>& sTable) {
        sTable.resize(2 * (r + 1));
        sTable[0] = MAGIC_P;
        for (unsigned int j = 1; j < sTable.size(); j++) {
            sTable[j] = sTable[j - 1] + MAGIC_Q;
        }

        unsigned int c = std::max((keylen + 3) / 4, 1U);
        std::vector<RC5_WORD> l(c, 0);
        for (unsigned int i = 0; i < keylen; i++) {
            l[i / 4] |= static_cast<RC5_WORD>(key[i]) << (8 * (i % 4));
        }

        RC5_WORD a = 0, b = 0;
        unsigned int n = 3 * std::max(static_cast<unsigned int>(sTable.size()), c);
        for (unsigned int h = 0, i = 0, j = 0; h < n; h++) {
            a = sTable[i] = rotl(sTable[i] + a + b, 3);
            b = l[j] = rotl(l[j] + a + b, static_cast<int>(a + b));
            i = (i + 1) % sTable.size();
            j = (j + 1) % c;
        }
    }

    void EncryptBlock(const uint8_t* inBlock, const std::vector<RC5_WORD>& sTable, uint8_t* outBlock) {
        RC5_WORD a = inBlock[0] | (inBlock[1] << 8) | (inBlock[2] << 16) | (inBlock[3] << 24);
        RC5_WORD b = inBlock[4] | (inBlock[5] << 8) | (inBlock[6] << 16) | (inBlock[7] << 24);

        a += sTable[0];
        b += sTable[1];

        for (unsigned int i = 0; i < r; i++) {
            a = rotl(a ^ b, b) + sTable[2 * i + 2];
            b = rotl(a ^ b, a) + sTable[2 * i + 3];
        }

        outBlock[0] = a & 0xFF; outBlock[1] = (a >> 8) & 0xFF; outBlock[2] = (a >> 16) & 0xFF; outBlock[3] = (a >> 24) & 0xFF;
        outBlock[4] = b & 0xFF; outBlock[5] = (b >> 8) & 0xFF; outBlock[6] = (b >> 16) & 0xFF; outBlock[7] = (b >> 24) & 0xFF;
    }

    void DecryptBlock(const uint8_t* inBlock, const std::vector<RC5_WORD>& sTable, uint8_t* outBlock) {
        RC5_WORD a = inBlock[0] | (inBlock[1] << 8) | (inBlock[2] << 16) | (inBlock[3] << 24);
        RC5_WORD b = inBlock[4] | (inBlock[5] << 8) | (inBlock[6] << 16) | (inBlock[7] << 24);

        for (unsigned int i = r; i > 0; i--) {
            b = rotr(b - sTable[2 * i + 1], a) ^ a;
            a = rotr(a - sTable[2 * i + 0], b) ^ b;
        }

        b -= sTable[1];
        a -= sTable[0];

        outBlock[0] = a & 0xFF; outBlock[1] = (a >> 8) & 0xFF; outBlock[2] = (a >> 16) & 0xFF; outBlock[3] = (a >> 24) & 0xFF;
        outBlock[4] = b & 0xFF; outBlock[5] = (b >> 8) & 0xFF; outBlock[6] = (b >> 16) & 0xFF; outBlock[7] = (b >> 24) & 0xFF;
    }

    std::string HexEncode(const std::vector<uint8_t>& bytes) {
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        return oss.str();
    }

    std::vector<uint8_t> HexDecode(const std::string& hex) {
        std::vector<uint8_t> bytes;
        if (hex.length() % 2 != 0) return bytes;
        bytes.reserve(hex.length() / 2);
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::strtol(byteString.c_str(), nullptr, 16));
            bytes.push_back(byte);
        }
        return bytes;
    }
} // namespace

std::string DecryptAuthData(const std::string& encryptedHex) {
    auto cipherBytes = HexDecode(encryptedHex);
    if (cipherBytes.empty() || cipherBytes.size() % 8 != 0) return "";

    const std::string keyStr = "3b59367098e946a4"; // MD5 of "mincoms"
    const uint8_t* key = reinterpret_cast<const uint8_t*>(keyStr.c_str());
    uint8_t iv[8];
    std::memset(iv, 0x01, 8);

    std::vector<RC5_WORD> sTable;
    KeyExpand(key, 16, sTable);

    std::vector<uint8_t> plaintext(cipherBytes.size());
    uint8_t block[8];
    uint8_t currentIv[8];
    std::memcpy(currentIv, iv, 8);

    for (size_t offset = 0; offset < cipherBytes.size(); offset += 8) {
        DecryptBlock(&cipherBytes[offset], sTable, block);
        for (int i = 0; i < 8; i++) {
            plaintext[offset + i] = block[i] ^ currentIv[i];
        }
        std::memcpy(currentIv, &cipherBytes[offset], 8);
    }

    // PKCS#7 unpadding
    if (plaintext.empty()) return "";
    uint8_t padLen = plaintext.back();
    if (padLen > 8 || padLen == 0) return "";
    for (size_t i = plaintext.size() - padLen; i < plaintext.size(); i++) {
        if (plaintext[i] != padLen) return "";
    }
    plaintext.resize(plaintext.size() - padLen);
    return std::string(plaintext.begin(), plaintext.end());
}

std::string EncryptAuthData(const std::string& plaintext) {
    const std::string keyStr = "3b59367098e946a4";
    const uint8_t* key = reinterpret_cast<const uint8_t*>(keyStr.c_str());
    uint8_t iv[8];
    std::memset(iv, 0x01, 8);

    std::vector<RC5_WORD> sTable;
    KeyExpand(key, 16, sTable);

    // PKCS#7 padding
    std::vector<uint8_t> padded(plaintext.begin(), plaintext.end());
    uint8_t padLen = 8 - (padded.size() % 8);
    padded.insert(padded.end(), padLen, padLen);

    std::vector<uint8_t> ciphertext(padded.size());
    uint8_t block[8];
    uint8_t currentIv[8];
    std::memcpy(currentIv, iv, 8);

    for (size_t offset = 0; offset < padded.size(); offset += 8) {
        for (int i = 0; i < 8; i++) {
            block[i] = padded[offset + i] ^ currentIv[i];
        }
        EncryptBlock(block, sTable, &ciphertext[offset]);
        std::memcpy(currentIv, &ciphertext[offset], 8);
    }
    return HexEncode(ciphertext);
}

}} // namespace sc::servers
