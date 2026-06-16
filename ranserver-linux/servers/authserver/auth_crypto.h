#ifndef SC_SERVERS_AUTH_CRYPTO_H
#define SC_SERVERS_AUTH_CRYPTO_H

#include <string>

namespace sc { namespace servers {

/**
 * Decrypts a hex-encoded cipher text string using RC5 CBC mode.
 * Key used: "3b59367098e946a4" (MD5 of "mincoms" first 16 bytes).
 * IV used: 0x01 (8 bytes).
 * Padding: PKCS#7.
 */
std::string DecryptAuthData(const std::string& encryptedHex);

/**
 * Encrypts a plain text string using RC5 CBC mode and returns a hex-encoded string.
 * Key used: "3b59367098e946a4".
 * IV used: 0x01 (8 bytes).
 * Padding: PKCS#7.
 */
std::string EncryptAuthData(const std::string& plaintext);

}} // namespace sc::servers

#endif // SC_SERVERS_AUTH_CRYPTO_H
