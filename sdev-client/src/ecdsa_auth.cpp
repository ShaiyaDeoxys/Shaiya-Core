#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "include/ecdsa_auth.h"
#include "include/shaiya/CNetwork.h"

#pragma comment(lib, "bcrypt.lib")

using namespace shaiya;

namespace ecdsa_auth
{
    namespace
    {
        // ---------- Constants ----------

        constexpr int kChallengeLen  = 32;
        constexpr int kPubKeyLen     = 64;   // raw X(32) + Y(32)
        constexpr int kPrivKeyLen    = 32;   // raw d
        constexpr int kSignatureLen  = 64;   // r(32) + s(32)

        // BCRYPT_ECCPRIVATE_BLOB: header(8) + X(32) + Y(32) + d(32) = 104
        constexpr int kPrivBlobLen = sizeof(BCRYPT_ECCKEY_BLOB) + kPubKeyLen + kPrivKeyLen;

        constexpr const char* kKeyFileName = "auth.key";

        // ---------- CNG handles ----------

        BCRYPT_ALG_HANDLE g_algEcdsa = nullptr;

        // ---------- Helpers ----------

        // Resolve full path to auth.key relative to the exe dir.
        bool get_key_path(char* out, int maxLen)
        {
            DWORD len = GetModuleFileNameA(nullptr, out, maxLen);
            if (len == 0 || static_cast<int>(len) >= maxLen)
                return false;

            char* slash = strrchr(out, '\\');
            if (!slash)
                return false;

            *(slash + 1) = '\0';

            if (strlen(out) + strlen(kKeyFileName) >= static_cast<size_t>(maxLen))
                return false;

            strcat_s(out, maxLen, kKeyFileName);
            return true;
        }

        bool file_exists(const char* path)
        {
            DWORD attr = GetFileAttributesA(path);
            return attr != INVALID_FILE_ATTRIBUTES
                && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        }

        // Load the private key blob from disk.
        // Returns a CNG key handle, or nullptr on failure.
        BCRYPT_KEY_HANDLE load_private_key()
        {
            if (!g_algEcdsa)
                return nullptr;

            char path[MAX_PATH]{};
            if (!get_key_path(path, MAX_PATH) || !file_exists(path))
                return nullptr;

            HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE)
                return nullptr;

            uint8_t blob[kPrivBlobLen]{};
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(hFile, blob, kPrivBlobLen, &bytesRead, nullptr);
            CloseHandle(hFile);

            if (!ok || bytesRead != kPrivBlobLen)
                return nullptr;

            // Validate the magic number.
            auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob);
            if (header->dwMagic != BCRYPT_ECDSA_PRIVATE_P256_MAGIC
                || header->cbKey != 32)
                return nullptr;

            BCRYPT_KEY_HANDLE hKey = nullptr;
            NTSTATUS st = BCryptImportKeyPair(
                g_algEcdsa, nullptr, BCRYPT_ECCPRIVATE_BLOB, &hKey,
                blob, kPrivBlobLen, 0);

            if (!BCRYPT_SUCCESS(st))
                return nullptr;

            return hKey;
        }

        // Generate a new ECDSA P-256 keypair.
        // Saves the private key blob to disk and returns the key handle.
        // Writes the raw public key (X,Y) to outPubKey.
        BCRYPT_KEY_HANDLE generate_and_save_keypair(uint8_t* outPubKey)
        {
            if (!g_algEcdsa)
                return nullptr;

            BCRYPT_KEY_HANDLE hKey = nullptr;
            NTSTATUS st = BCryptGenerateKeyPair(g_algEcdsa, &hKey, 256, 0);
            if (!BCRYPT_SUCCESS(st) || !hKey)
                return nullptr;

            st = BCryptFinalizeKeyPair(hKey, 0);
            if (!BCRYPT_SUCCESS(st))
            {
                BCryptDestroyKey(hKey);
                return nullptr;
            }

            // Export the private blob (contains public + private).
            uint8_t blob[kPrivBlobLen]{};
            ULONG cbResult = 0;
            st = BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB,
                blob, kPrivBlobLen, &cbResult, 0);
            if (!BCRYPT_SUCCESS(st) || cbResult != kPrivBlobLen)
            {
                BCryptDestroyKey(hKey);
                return nullptr;
            }

            // Copy raw public key (X,Y) — starts after the 8-byte header.
            std::memcpy(outPubKey, blob + sizeof(BCRYPT_ECCKEY_BLOB),
                kPubKeyLen);

            // Save to disk.
            char path[MAX_PATH]{};
            if (!get_key_path(path, MAX_PATH))
            {
                BCryptDestroyKey(hKey);
                return nullptr;
            }

            HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0,
                nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                BCryptDestroyKey(hKey);
                return nullptr;
            }

            DWORD written = 0;
            BOOL writeOk = WriteFile(hFile, blob, kPrivBlobLen, &written, nullptr);
            CloseHandle(hFile);

            // Scrub the private blob from the stack.
            SecureZeroMemory(blob, sizeof(blob));

            if (!writeOk || written != kPrivBlobLen)
            {
                // Delete partial file.
                DeleteFileA(path);
                BCryptDestroyKey(hKey);
                return nullptr;
            }

            return hKey;
        }
    }

    // ================================================================
    // Public API
    // ================================================================

    void init()
    {
        BCryptOpenAlgorithmProvider(
            &g_algEcdsa, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);

        OutputDebugStringA("[ECDSA-CLI] init() complete");
    }

    void on_challenge_received(const void* packetData, int length)
    {
        OutputDebugStringA("[ECDSA-CLI] on_challenge_received ENTERED");

        if (!packetData || length < kChallengeLen)
            return;

        auto* raw = static_cast<const uint8_t*>(packetData);
        const uint8_t* challenge;

        // Packet data may or may not include the 2-byte opcode prefix
        // depending on the caller's layout.
        if (length >= 2 + kChallengeLen
            && *reinterpret_cast<const uint16_t*>(raw) == 0xA103)
        {
            challenge = raw + 2;   // skip opcode prefix
        }
        else
        {
            challenge = raw;       // payload only
        }

        // Load private key.
        BCRYPT_KEY_HANDLE hKey = load_private_key();
        if (!hKey)
            return;   // No key — can't respond.  Server will time out.

        // Sign the challenge.
        uint8_t signature[kSignatureLen]{};
        ULONG cbSig = 0;
        NTSTATUS st = BCryptSignHash(
            hKey, nullptr,
            const_cast<PUCHAR>(challenge), kChallengeLen,
            signature, kSignatureLen, &cbSig, 0);

        BCryptDestroyKey(hKey);

        if (!BCRYPT_SUCCESS(st) || cbSig != kSignatureLen)
            return;

        // Send 0xA103 response.
#pragma pack(push, 1)
        struct
        {
            uint16_t opcode;
            uint8_t  sig[kSignatureLen];
        } pkt{};
#pragma pack(pop)

        pkt.opcode = 0xA103;
        std::memcpy(pkt.sig, signature, kSignatureLen);

        CNetwork::Send(&pkt, sizeof(pkt));
    }

    void on_login_success()
    {
        OutputDebugStringA("[ECDSA-CLI] on_login_success ENTERED (0xA106 received)");

        // If a private key already exists, nothing to do — enrollment
        // was completed on a previous login.
        char path[MAX_PATH]{};
        if (!get_key_path(path, MAX_PATH))
        {
            OutputDebugStringA("[ECDSA-CLI] on_login_success: get_key_path failed");
            return;
        }

        {
            char dbg[512];
            wsprintfA(dbg, "[ECDSA-CLI] on_login_success: path='%s' exists=%d",
                path, file_exists(path) ? 1 : 0);
            OutputDebugStringA(dbg);
        }

        if (file_exists(path))
            return;

        OutputDebugStringA("[ECDSA-CLI] generating new keypair...");

        // First login — generate keypair and enroll.
        uint8_t pubKey[kPubKeyLen]{};
        BCRYPT_KEY_HANDLE hKey = generate_and_save_keypair(pubKey);
        if (!hKey)
        {
            OutputDebugStringA("[ECDSA-CLI] generate_and_save_keypair FAILED");
            return;
        }

        OutputDebugStringA("[ECDSA-CLI] keypair generated, sending 0xA104");

        BCryptDestroyKey(hKey);

        // Send 0xA104 enrollment packet.
#pragma pack(push, 1)
        struct
        {
            uint16_t opcode;
            uint8_t  key[kPubKeyLen];
        } pkt{};
#pragma pack(pop)

        pkt.opcode = 0xA104;
        std::memcpy(pkt.key, pubKey, kPubKeyLen);

        CNetwork::Send(&pkt, sizeof(pkt));
    }
}
