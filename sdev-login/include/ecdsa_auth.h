#pragma once

// ===========================================================================
// ecdsa_auth — ECDSA P-256 challenge-response authentication
// ===========================================================================
//
// Automatic, transparent ECDSA-based account protection.  Every user
// account can have an ECDSA P-256 public key stored in the database.
// When a key is present, the login flow adds a challenge-response step
// before granting access:
//
//   1. Client sends 0xA102 (username + password)  — standard login
//   2. Server verifies credentials via the login SP
//   3. Server looks up AuthPublicKey in Users_Master
//      a. Key is NULL → first login, proceed normally (client enrolls)
//      b. Key exists  → send 0xA103 challenge (32 random bytes)
//   4. Client signs the challenge with its stored private key
//   5. Client sends 0xA103 (64-byte ECDSA signature)
//   6. Server verifies the signature with the stored public key
//      a. Valid   → complete login (send normal success response)
//      b. Invalid → close connection
//
// Key enrollment (first login or new device):
//   After a successful login without an existing key, the client
//   auto-generates an ECDSA P-256 keypair, stores the private key
//   locally (Data/auth.key), and sends the public key to the server
//   via opcode 0xA104.  The server stores it in Users_Master.
//
// Threat model:
//   - Full database leak: attacker has all public keys but CANNOT
//     forge signatures (ECDSA private keys are never on the server).
//   - Credential reuse / brute force: password alone is insufficient
//     when a key is enrolled.
//   - Transparent: users and admins never interact with keys manually.
//
// Protocol:
//   0xA103 server→client: opcode(2) + challenge(32) = 34 bytes
//   0xA103 client→server: opcode(2) + signature(64)  = 66 bytes
//   0xA104 client→server: opcode(2) + publicKey(64)   = 66 bytes
//
// ===========================================================================

namespace shaiya { struct CUser; struct SDatabase; }

namespace ecdsa_auth
{
    // One-time initialisation (CNG handles, critical section).
    void init();

    // Called from hook_0x406A8C after the login SP succeeds.
    // Stores the correct username keyed by CUser pointer so that
    // on_login_success can find it without depending on CUser
    // struct field offsets.  The DB query is deferred.
    void cache_public_key(shaiya::SDatabase* db, const char* username);

    // Called from the hook that replaces the call to send_login_success
    // (0x404950).  If the user has a stored public key, sends a
    // challenge packet and defers the login.  Otherwise proceeds
    // normally.
    void on_login_success(shaiya::CUser* user);

    // Incoming 0xA103 — client's signed challenge response.
    void on_challenge_response(shaiya::CUser* user, const void* packetData);

    // Incoming 0xA104 — client's public key enrollment.
    void on_key_enroll(shaiya::CUser* user, const void* packetData);
}
