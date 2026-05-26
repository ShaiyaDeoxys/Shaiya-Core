#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <cctype>
#include <unordered_map>
#include <string>
#include "include/ecdsa_auth.h"
#include "include/shaiya/CUser.h"
#include "include/shaiya/SConnection.h"
#include "include/shaiya/SDatabase.h"

#pragma comment(lib, "bcrypt.lib")

using namespace shaiya;

namespace ecdsa_auth
{
    namespace
    {
        // ---------- Constants ----------

        constexpr int kChallengeLen = 32;
        constexpr int kPubKeyLen   = 64;   // raw X(32) + Y(32) for P-256
        constexpr int kSignatureLen = 64;  // r(32) + s(32) for P-256

        // Custom CUser::status value that prevents the periodic tick
        // from retrying send_login_success while we wait for the
        // challenge response.  Any value outside {0,1,2,3} works.
        constexpr auto kStatusEcdsaPending =
            static_cast<UserStatus>(static_cast<int32_t>(0x10));

        // Address of the original send_login_success function.
        // Expects ESI = CUser*, no stack params.
        unsigned u0x404950 = 0x404950;

        // ---------- CNG handles ----------

        BCRYPT_ALG_HANDLE g_algEcdsa = nullptr;
        BCRYPT_ALG_HANDLE g_algRng   = nullptr;

        // ---------- Per-user state ----------

        struct EcdsaState
        {
            uint8_t  challenge[kChallengeLen]{};
            uint8_t  publicKey[kPubKeyLen]{};
            bool     hasKey = false;
            std::string username;   // correct username for DB operations
        };

        CRITICAL_SECTION g_cs;

        // Keyed by CUser pointer so we never depend on CUser struct
        // field offsets for the username string.
        std::unordered_map<uintptr_t, EcdsaState> g_state;

        // Pending login: maps thread ID → username.
        // Set in cache_public_key, consumed in on_login_success.
        // Both run on the same I/O thread for a given login.
        std::unordered_map<DWORD, std::string> g_pendingUser;

        // Cached SQLHDBC from the server's connection pool.
        SQLHDBC g_cachedDbc = SQL_NULL_HANDLE;

        // ---------- Helpers ----------

        std::string to_lower(const char* s)
        {
            std::string r;
            if (!s) return r;
            while (*s)
            {
                r += static_cast<char>(std::tolower(
                    static_cast<unsigned char>(*s)));
                ++s;
            }
            return r;
        }

        // Call the original send_login_success (0x404950).
        void call_send_login_success(CUser* user)
        {
            auto* u = user;
            __asm
            {
                push esi
                mov  esi, u
                call u0x404950
                pop  esi
            }
        }

        // ---------- DB helpers ----------

        bool query_public_key_db(SQLHDBC dbc, const char* username,
                                 uint8_t* outKey, bool& outHasKey)
        {
            outHasKey = false;

            SQLHSTMT stmt = SQL_NULL_HANDLE;
            if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS)
                return false;

            const char* sql =
                "SELECT [AuthPublicKey] "
                "FROM [PS_UserData].[dbo].[Users_Master] "
                "WHERE [UserID] = ?";

            bool ok = false;

            if (SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(
                    const_cast<char*>(sql)), SQL_NTS) == SQL_SUCCESS)
            {
                char user[33]{};
                strncpy_s(user, username, 32);
                SQLLEN cbUser = SQL_NTS;

                if (SQLBindParameter(stmt, 1, SQL_PARAM_INPUT,
                        SQL_C_CHAR, SQL_VARCHAR, 32, 0,
                        user, sizeof(user), &cbUser) == SQL_SUCCESS
                    && SQLExecute(stmt) == SQL_SUCCESS)
                {
                    ok = true;

                    if (SQLFetch(stmt) == SQL_SUCCESS)
                    {
                        SQLLEN cbKey = 0;
                        SQLGetData(stmt, 1, SQL_C_BINARY,
                            outKey, kPubKeyLen, &cbKey);

                        if (cbKey == kPubKeyLen)
                            outHasKey = true;
                    }
                }
            }

            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            return ok;
        }

        bool store_public_key_db(SQLHDBC dbc, const char* username,
                                 const uint8_t* pubKey)
        {
            SQLHSTMT stmt = SQL_NULL_HANDLE;
            if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS)
                return false;

            const char* sql =
                "UPDATE [PS_UserData].[dbo].[Users_Master] "
                "SET [AuthPublicKey] = ? WHERE [UserID] = ?";

            bool ok = false;

            if (SQLPrepareA(stmt, reinterpret_cast<SQLCHAR*>(
                    const_cast<char*>(sql)), SQL_NTS) == SQL_SUCCESS)
            {
                SQLLEN cbKey  = kPubKeyLen;
                SQLLEN cbUser = SQL_NTS;
                char user[33]{};
                strncpy_s(user, username, 32);

                if (SQLBindParameter(stmt, 1, SQL_PARAM_INPUT,
                        SQL_C_BINARY, SQL_VARBINARY, kPubKeyLen, 0,
                        const_cast<uint8_t*>(pubKey), kPubKeyLen,
                        &cbKey) == SQL_SUCCESS
                    && SQLBindParameter(stmt, 2, SQL_PARAM_INPUT,
                        SQL_C_CHAR, SQL_VARCHAR, 32, 0,
                        user, sizeof(user), &cbUser) == SQL_SUCCESS
                    && SQLExecute(stmt) == SQL_SUCCESS)
                {
                    ok = true;
                }
            }

            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            return ok;
        }

        // ---------- ECDSA verification ----------

        bool verify_signature(const uint8_t* pubKeyRaw,
                              const uint8_t* challenge,
                              const uint8_t* signature)
        {
            if (!g_algEcdsa) return false;

            struct
            {
                BCRYPT_ECCKEY_BLOB header;
                uint8_t xy[kPubKeyLen];
            } blob{};

            blob.header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
            blob.header.cbKey   = 32;
            std::memcpy(blob.xy, pubKeyRaw, kPubKeyLen);

            BCRYPT_KEY_HANDLE hKey = nullptr;
            NTSTATUS st = BCryptImportKeyPair(
                g_algEcdsa, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey,
                reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0);

            if (!BCRYPT_SUCCESS(st) || !hKey)
                return false;

            st = BCryptVerifySignature(
                hKey, nullptr,
                const_cast<PUCHAR>(challenge), kChallengeLen,
                const_cast<PUCHAR>(signature), kSignatureLen, 0);

            BCryptDestroyKey(hKey);
            return BCRYPT_SUCCESS(st);
        }
    }

    // ================================================================
    // Public API
    // ================================================================

    void init()
    {
        InitializeCriticalSection(&g_cs);

        BCryptOpenAlgorithmProvider(
            &g_algEcdsa, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
        BCryptOpenAlgorithmProvider(
            &g_algRng, BCRYPT_RNG_ALGORITHM, nullptr, 0);

        OutputDebugStringA("[ECDSA-SRV] init() complete");
    }

    void cache_public_key(SDatabase* db, const char* username)
    {
        if (!db || !username || !username[0])
        {
            OutputDebugStringA("[ECDSA-SRV] cache_public_key: early return (null)");
            return;
        }

        // Cache the SQLHDBC for later use.
        if (g_cachedDbc == SQL_NULL_HANDLE)
            g_cachedDbc = db->dbc;

        // Store the correct username keyed by thread ID.
        // on_login_success runs on the same thread shortly after.
        DWORD tid = GetCurrentThreadId();

        char dbg[256];
        wsprintfA(dbg, "[ECDSA-SRV] cache_public_key user='%s' tid=%u",
            username, tid);
        OutputDebugStringA(dbg);

        EnterCriticalSection(&g_cs);
        g_pendingUser[tid] = std::string(username);
        LeaveCriticalSection(&g_cs);
    }

    void on_login_success(CUser* user)
    {
        OutputDebugStringA("[ECDSA-SRV] on_login_success ENTERED");

        if (!user)
        {
            OutputDebugStringA("[ECDSA-SRV] on_login_success: user is NULL");
            call_send_login_success(user);
            return;
        }

        auto uid = reinterpret_cast<uintptr_t>(user);
        DWORD tid = GetCurrentThreadId();

        // Retrieve the correct username stored by cache_public_key
        // (same thread, moments earlier).
        std::string username;

        EnterCriticalSection(&g_cs);
        auto pit = g_pendingUser.find(tid);
        if (pit != g_pendingUser.end())
        {
            username = pit->second;
            g_pendingUser.erase(pit);
        }
        LeaveCriticalSection(&g_cs);

        if (username.empty())
        {
            OutputDebugStringA("[ECDSA-SRV] on_login_success: no pending username, pass-through");
            call_send_login_success(user);
            return;
        }

        {
            char dbg[256];
            wsprintfA(dbg, "[ECDSA-SRV] on_login_success user='%s'",
                username.c_str());
            OutputDebugStringA(dbg);
        }

        // Query the public key now (the login SP cursor is consumed).
        EcdsaState st{};
        st.username = username;

        if (g_cachedDbc != SQL_NULL_HANDLE)
        {
            bool qok = query_public_key_db(
                g_cachedDbc, username.c_str(),
                st.publicKey, st.hasKey);

            char dbg[128];
            wsprintfA(dbg, "[ECDSA-SRV] on_login_success qok=%d hasKey=%d",
                qok ? 1 : 0, st.hasKey ? 1 : 0);
            OutputDebugStringA(dbg);
        }

        if (!st.hasKey)
        {
            // No key enrolled — normal login, then tell the client
            // to generate and enroll a keypair via 0xA106.
            OutputDebugStringA("[ECDSA-SRV] sending login_success + 0xA106 enroll");
            call_send_login_success(user);

            // Store the username so on_key_enroll can use it.
            EnterCriticalSection(&g_cs);
            g_state[uid] = st;
            LeaveCriticalSection(&g_cs);

            // Send 0xA106 enrollment trigger.
#pragma pack(push, 1)
            struct { uint16_t opcode; } enrollPkt{};
#pragma pack(pop)
            enrollPkt.opcode = 0xA106;
            SConnection::Send(
                reinterpret_cast<SConnection*>(user),
                &enrollPkt, sizeof(enrollPkt));
            return;
        }

        // Key enrolled — challenge the client.
        BCryptGenRandom(g_algRng, st.challenge, kChallengeLen, 0);

        EnterCriticalSection(&g_cs);
        g_state[uid] = st;
        LeaveCriticalSection(&g_cs);

        // Set a custom status so the periodic tick won't retry.
        user->status = kStatusEcdsaPending;

        // Send 0xA103 challenge packet.
#pragma pack(push, 1)
        struct
        {
            uint16_t opcode;
            uint8_t  data[kChallengeLen];
        } pkt{};
#pragma pack(pop)

        pkt.opcode = 0xA103;
        std::memcpy(pkt.data, st.challenge, kChallengeLen);

        OutputDebugStringA("[ECDSA-SRV] sending 0xA103 challenge");

        SConnection::Send(
            reinterpret_cast<SConnection*>(user),
            &pkt, sizeof(pkt));
    }

    void on_challenge_response(CUser* user, const void* packetData)
    {
        if (!user || !packetData)
            return;

        auto* raw = static_cast<const uint8_t*>(packetData);
        const uint8_t* signature = raw + 2;

        auto uid = reinterpret_cast<uintptr_t>(user);

        EnterCriticalSection(&g_cs);
        auto it = g_state.find(uid);

        if (it == g_state.end() || !it->second.hasKey)
        {
            LeaveCriticalSection(&g_cs);
            SConnection::Close(
                reinterpret_cast<SConnection*>(user), 0, 9);
            return;
        }

        EcdsaState st = it->second;
        g_state.erase(it);
        LeaveCriticalSection(&g_cs);

        if (!verify_signature(st.publicKey, st.challenge, signature))
        {
            SConnection::Close(
                reinterpret_cast<SConnection*>(user), 0, 9);
            return;
        }

        // Signature valid — restore status and complete the login.
        user->status = UserStatus::GetUserSuccess;
        call_send_login_success(user);
    }

    void on_key_enroll(CUser* user, const void* packetData)
    {
        OutputDebugStringA("[ECDSA-SRV] on_key_enroll ENTERED");

        if (!user || !packetData)
        {
            OutputDebugStringA("[ECDSA-SRV] on_key_enroll: null user or pkt");
            return;
        }

        // Only accept from fully authenticated users (status == 3).
        if (user->status != UserStatus::LoginSuccess)
        {
            char dbg[128];
            wsprintfA(dbg, "[ECDSA-SRV] on_key_enroll: wrong status=%d (need 3)",
                static_cast<int>(user->status));
            OutputDebugStringA(dbg);
            return;
        }

        auto* raw = static_cast<const uint8_t*>(packetData);
        const uint8_t* pubKey = raw + 2;

        // Reject an all-zero key (malformed enrollment).
        bool allZero = true;
        for (int i = 0; i < kPubKeyLen; ++i)
        {
            if (pubKey[i] != 0) { allZero = false; break; }
        }
        if (allZero)
        {
            OutputDebugStringA("[ECDSA-SRV] on_key_enroll: all-zero key, rejected");
            return;
        }

        // Retrieve the correct username from the state map.
        auto uid = reinterpret_cast<uintptr_t>(user);
        std::string username;

        EnterCriticalSection(&g_cs);
        auto it = g_state.find(uid);
        if (it != g_state.end())
        {
            username = it->second.username;
            g_state.erase(it);
        }
        LeaveCriticalSection(&g_cs);

        if (username.empty())
        {
            OutputDebugStringA("[ECDSA-SRV] on_key_enroll: no username in state");
            return;
        }

        // Store the public key.
        if (g_cachedDbc != SQL_NULL_HANDLE)
        {
            bool stored = store_public_key_db(
                g_cachedDbc, username.c_str(), pubKey);
            char dbg[128];
            wsprintfA(dbg, "[ECDSA-SRV] on_key_enroll: store result=%d user='%s'",
                stored ? 1 : 0, username.c_str());
            OutputDebugStringA(dbg);
        }
        else
        {
            OutputDebugStringA("[ECDSA-SRV] on_key_enroll: no cached DBC!");
        }
    }
}
