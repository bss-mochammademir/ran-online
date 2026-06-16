#ifndef SC_SERVERS_AUTH_CRYPTO_H
#define SC_SERVERS_AUTH_CRYPTO_H

#include <string>

namespace sc { namespace servers {

// RC5-32/16/16 + CBC + PKCS#7, port of SigmaCore RC5EncryptA("mincoms").
// Key  = first 16 chars of lowercase hex MD5("mincoms") = "0e0aa086965047fb".
// IV   = 0x01 x 8 bytes. Cipher I/O is lowercase hex.
// NOTE: round-trip self-tests pass regardless of key correctness — final
// interop must be confirmed with a known-answer vector from a real client/server
// sample (see runbook auth-cert-crypto.md).

/** Decrypts a lowercase-hex RC5/CBC cipher text. Returns "" on failure. */
std::string DecryptAuthData(const std::string& encryptedHex);

/** Encrypts plain text with RC5/CBC and returns lowercase-hex cipher text. */
std::string EncryptAuthData(const std::string& plaintext);

}} // namespace sc::servers

#endif // SC_SERVERS_AUTH_CRYPTO_H
