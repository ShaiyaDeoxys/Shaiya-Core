# Login Service Module

`sdev-login` is the `ps_login.exe` server patch module. It patches a well-known login service vulnerability and adds GM Security.

## Environment

- Windows 10 or newer
- Visual Studio 2022
- C++23
- x86 build target
- Microsoft Visual C++ Redistributable: https://aka.ms/vs/17/release/vc_redist.x86.exe

## Installed Hooks

Active hooks are installed from `src/main.cpp`; GM Security policy lives in `src/gm_security.cpp`.

```cpp
// GetUser (GM Security)
util::detour((void*)0x406A8C, naked_0x406A8C, 7);
// CUserCrypto::KeyInit
util::detour((void*)0x404E84, naked_0x404E84, 5);
```

## GM Security

IP-based login restriction for admin accounts. Every account with admin privileges must have an entry in `Data/GMSecurity.ini` to log in. The system is fail-closed: unlisted GMs are blocked, not allowed.

### Two-Stage Verification

**Stage 1 (INI pre-check, instant, no DB):** if the username exists in `[IP_WHITELIST]`, the connecting IP must match one of the listed addresses. Mismatched IPs are rejected immediately.

**Stage 2 (DB admin check):** if the username is not in the INI, the system queries `Users_Master` for admin indicators (`Admin`, `AdminLevel`, `Status`). If any flag is set, login is rejected. This prevents unlisted GMs from authenticating even if they are not in the INI file.

### Configuration

`Data/GMSecurity.ini` (relative to `ps_login.exe` directory):

```ini
[SETTINGS]
; 0 = feature off, 1 = feature on
; Re-read on every login so the admin can toggle without restarting ps_login.exe.
Enabled=1

[IP_WHITELIST]
; username=ip1,ip2,ip3
admin=192.168.1.100,10.0.0.5
```

### Security Properties

- **Fail-closed**: unlisted GMs are blocked, not allowed.
- **Pre-authentication**: blocked GMs never reach the login stored procedure.
- **Parameterized DB query**: immune to SQL injection.
- **Invisible**: blocked login returns the same error as wrong password.
- **Stage 2 fail-open**: if the DB pre-check fails, login proceeds to the stored procedure (prevents broken config from blocking the entire server). Stage 1 is always enforced.
- **Hot-reloadable**: the INI is re-read on every login attempt.
- `GMSecurity.ini` should be protected by OS-level ACLs.

## CUserCrypto::KeyInit

Validates the RSA key length during the crypto initialization handshake.
