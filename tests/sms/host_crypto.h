#pragma once
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

inline bool hostHmac(const uint8_t key[32], const char* data, size_t length, uint8_t digest[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
        return false;
    DWORD objectLength = 0, copied = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &copied, 0) >= 0;
    std::vector<uint8_t> object(objectLength);
    ok = ok && BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
        const_cast<PUCHAR>(key), 32, 0) >= 0;
    ok = ok && BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)),
        static_cast<ULONG>(length), 0) >= 0;
    ok = ok && BCryptFinishHash(hash, digest, 32, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0); return ok;
}
