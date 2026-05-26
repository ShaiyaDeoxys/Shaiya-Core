#pragma once

// ===========================================================================
// ecdsa_auth — Client-side ECDSA P-256 key management
// ===========================================================================
//
// Transparent keypair lifecycle:
//
//   On first successful login (server sends 0xA106 enroll trigger):
//     - Generates an ECDSA P-256 keypair via Windows CNG (bcrypt.h).
//     - Saves the private key blob to auth.key (next to game.exe).
//     - Sends the 64-byte raw public key (X,Y) to the server (0xA104).
//
//   On subsequent logins (server sends 0xA103 challenge):
//     - Loads the private key from auth.key.
//     - Signs the 32-byte challenge.
//     - Sends the 64-byte signature back to the server (0xA103).
//
// The user never interacts with keys directly.
//
// ===========================================================================

#include <cstdint>

namespace ecdsa_auth
{
    // Initialise CNG handles.  Called once at DLL load.
    void init();

    // Called when the client receives a 0xA103 challenge packet from
    // the login server.  Signs the challenge and sends the response.
    void on_challenge_received(const void* packetData, int length);

    // Called when the client receives 0xA106 (enroll trigger) from
    // the server after a successful first login.  Generates a keypair
    // and sends the public key to the server (0xA104).
    void on_login_success();
}
