#include <array>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <util/util.h>
#include "include/main.h"
#include "include/shaiya/SDatabase.h"
using namespace shaiya;

// ===========================================================================
// GM Security — IP-based login restriction for admin accounts
// ===========================================================================
//
// Every account with admin privileges MUST have an entry in
// Data/GMSecurity.ini to log in.  This is fail-closed:
//
//   - GM in INI + IP matches          → login proceeds
//   - GM in INI + IP mismatch         → login rejected
//   - GM NOT in INI                   → login rejected
//   - Normal account (not in INI)     → login proceeds
//
// ---- Two-stage verification ----
//
//   Stage 1 — INI pre-check (instant, no DB):
//     If the username exists in [IP_WHITELIST], the connecting IP must
//     match one of the listed addresses.
//
//   Stage 2 — DB admin check (lightweight SELECT via SDatabase):
//     If the username is NOT in the INI, query Users_Master for admin
//     indicators.  An account is admin if ANY of:
//       - Admin != 0       (bit:  master admin flag)
//       - AdminLevel > 0   (tinyint: GM permission tier)
//       - Status > 0       (smallint: account type, 0=normal, >0=GM)
//     If admin → login rejected.  An unlisted GM can never authenticate.
//
//     Uses the same SDatabase::PrepareSql/ExecuteSql path as the login
//     stored procedure (proven to work), then closes the cursor before
//     the real login query runs.
//
// ---- Configuration file ----
//
//   Data/GMSecurity.ini  (relative to ps_login.exe directory)
//
//   [SETTINGS]
//   Enabled=1          ; 0 = feature off, 1 = feature on
//
//   [IP_WHITELIST]
//   username=ip1,ip2,ip3
//
// ---- Security properties ----
//
//   - Fail-closed: unlisted GMs are blocked, not allowed.
//   - Pre-authentication: blocked GMs never reach the login SP.
//   - Parameterized DB query: immune to SQL injection.
//   - Invisible: blocked login = same error as wrong password.
//   - Stage 2 fail-open: if the DB pre-check fails, login proceeds
//     to the stored procedure (prevents broken config from DOS-ing
//     the server).  Stage 1 is always enforced.
//   - GMSecurity.ini MUST be protected by OS-level ACLs.
//
// ===========================================================================

namespace
{
    constexpr const char* kIniFileName = "Data\\GMSecurity.ini";
    constexpr const char* kIniSection  = "IP_WHITELIST";
    constexpr const char* kSettingsSection = "SETTINGS";
    constexpr int kMaxUsernameLen = 32;

    // --- Path resolution ---

    const char* get_ini_path()
    {
        static char path[MAX_PATH]{};
        static bool resolved = false;

        if (!resolved)
        {
            resolved = true;

            DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (len == 0 || len >= MAX_PATH)
            {
                path[0] = '\0';
                return path;
            }

            char* lastSlash = std::strrchr(path, '\\');
            if (!lastSlash)
            {
                path[0] = '\0';
                return path;
            }
            *(lastSlash + 1) = '\0';
            strcat_s(path, MAX_PATH, kIniFileName);
        }
        return path;
    }

    // --- Feature toggle ---

    // Reads [SETTINGS] Enabled from the INI.  Returns false if the key
    // is missing, the file is absent, or the value is 0.  Any non-zero
    // value (typically 1) enables GM Security.  Re-read on every login
    // so the admin can toggle without restarting ps_login.exe.
    bool is_feature_enabled()
    {
        const char* iniPath = get_ini_path();
        if (!iniPath[0])
            return false;

        return GetPrivateProfileIntA(kSettingsSection, "Enabled", 0, iniPath) != 0;
    }

    // --- Stage 1: INI whitelist check ---

    // Returns:
    //   +1 = username in INI, IP matches (allow, skip Stage 2)
    //   -1 = username in INI, IP mismatch (block)
    //    0 = username not in INI (need Stage 2)
    int check_whitelist_ip(const char* username, const char* ipv4)
    {
        if (!username || !username[0] || !ipv4 || !ipv4[0])
            return 0;

        const char* iniPath = get_ini_path();
        if (!iniPath[0])
            return 0;

        char safeUser[kMaxUsernameLen + 1]{};
        strncpy_s(safeUser, username, kMaxUsernameLen);

        constexpr const char* kNotFound = "\x01";

        char allowedIps[512]{};
        GetPrivateProfileStringA(
            kIniSection, safeUser, kNotFound,
            allowedIps, sizeof(allowedIps), iniPath);

        if (std::strcmp(allowedIps, kNotFound) == 0)
            return 0;   // Not in INI.

        if (allowedIps[0] == '\0')
            return -1;  // In INI but empty value — no IPs authorized.

        const char* p = allowedIps;
        auto ipLen = std::strlen(ipv4);

        while (*p)
        {
            while (*p == ',' || *p == ' ' || *p == '\t')
                ++p;
            if (*p == '\0')
                break;

            const char* tokenStart = p;
            while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t')
                ++p;
            auto tokenLen = static_cast<std::size_t>(p - tokenStart);

            if (tokenLen == ipLen &&
                std::strncmp(tokenStart, ipv4, tokenLen) == 0)
                return +1;  // IP matches.
        }

        return -1;  // In INI, no IP matched.
    }

    // --- Stage 2: DB admin check ---

    // Query Users_Master via SDatabase (same path as the login SP).
    // Returns true if the account has any admin flag set:
    //   - Admin  != 0   (bit:  master admin flag)
    //   - AdminLevel > 0 (tinyint: GM permission tier, 0 = normal)
    //   - Status > 0     (smallint: account type, 0 = normal)
    // Returns false if normal user, not found, or query fails (fail-open
    // for Stage 2 — broken config must not DOS the whole server).
    bool is_db_admin_account(SDatabase* db, const char* username)
    {
        if (!db || !username || !username[0])
            return false;

        const char* query =
            "SELECT TOP 1 [Admin], [AdminLevel], [Status] "
            "FROM [PS_UserData].[dbo].[Users_Master] "
            "WHERE [UserID] = ?";

        if (SDatabase::PrepareSql(db, query))
            return false;

        char user[kMaxUsernameLen + 1]{};
        strncpy_s(user, username, kMaxUsernameLen);

        if (SDatabase::BindParameter(db, 1, kMaxUsernameLen,
                SQL_C_CHAR, SQL_VARCHAR, user, nullptr, SQL_PARAM_INPUT))
        {
            SQLFreeStmt(db->stmt, SQL_CLOSE);
            SQLFreeStmt(db->stmt, SQL_RESET_PARAMS);
            return false;
        }

        if (SDatabase::ExecuteSql(db))
        {
            SQLFreeStmt(db->stmt, SQL_CLOSE);
            SQLFreeStmt(db->stmt, SQL_RESET_PARAMS);
            return false;
        }

        bool isAdmin = false;
        auto fetchRc = SQLFetch(db->stmt);

        if (fetchRc == SQL_SUCCESS)
        {
            // Col 1: Admin (bit → SQLCHAR: 0 or 1)
            unsigned char adminFlag = 0;
            SQLLEN ind1 = 0;
            SQLGetData(db->stmt, 1, SQL_C_UTINYINT, &adminFlag, sizeof(adminFlag), &ind1);

            // Col 2: AdminLevel (tinyint → SQLCHAR: 0–255)
            unsigned char adminLevel = 0;
            SQLLEN ind2 = 0;
            SQLGetData(db->stmt, 2, SQL_C_UTINYINT, &adminLevel, sizeof(adminLevel), &ind2);

            // Col 3: Status (smallint → SQLSMALLINT: 0–32767)
            short status = 0;
            SQLLEN ind3 = 0;
            SQLGetData(db->stmt, 3, SQL_C_SSHORT, &status, sizeof(status), &ind3);

            isAdmin = (adminFlag != 0) || (adminLevel > 0) || (status > 0);
        }

        SQLFreeStmt(db->stmt, SQL_CLOSE);
        SQLFreeStmt(db->stmt, SQL_RESET_PARAMS);

        return isAdmin;
    }
}

// ===========================================================================
// Login hook
// ===========================================================================

short hook_0x406A8C(SDatabase* db, char* username, char* password, unsigned lowPart, unsigned highPart, char* ipv4)
{
    // Feature toggle: [SETTINGS] Enabled=1 in GMSecurity.ini.
    // When disabled (0 or missing), skip all GM checks — every account
    // goes straight to the login stored procedure.
    if (is_feature_enabled())
    {
        // Stage 1: INI whitelist (instant, no DB)
        int whitelistResult = check_whitelist_ip(username, ipv4);

        if (whitelistResult == -1)
            return -1;  // Listed GM, wrong IP.

        if (whitelistResult == 0)
        {
            // Stage 2: not in INI — check DB for admin status.
            // If admin → block (unlisted GM, fail-closed).
            if (is_db_admin_account(db, username))
                return -1;
        }
    }

    // Authorized GM (+1) or normal user (0, not admin) — proceed.

    ULARGE_INTEGER sessionId{ lowPart, highPart };

    std::array<char, 1024> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "EXEC [PS_UserData].[dbo].[usp_Try_GameLogin_Taiwan] ?,?,%llu,'%s';",
        sessionId.QuadPart, ipv4);

    if (SDatabase::PrepareSql(db, buffer.data()))
        return -1;

    short result = 0;
    result = SDatabase::BindParameter(db, 1, 32, SQL_C_CHAR, SQL_VARCHAR, username, nullptr, SQL_PARAM_INPUT);
    result = SDatabase::BindParameter(db, 2, 32, SQL_C_CHAR, SQL_VARCHAR, password, nullptr, SQL_PARAM_INPUT);

    if (result)
        return -1;

    return SDatabase::ExecuteSql(db);
}

unsigned u0x406B24 = 0x406B24;
unsigned u0x406ADF = 0x406ADF;
void __declspec(naked) naked_0x406A8C()
{
    __asm
    {
        pushad

        mov ebp,[esp+0x44C]
        mov eax,[esp+0x448]
        mov ecx,[esp+0x444]
        mov esi,[esp+0x440]
        mov edi,[esp+0x43C]

        push ebp // ipv4

        // sessionId
        push eax
        push ecx

        push esi // password
        push edi // username
        push ebx // database
        call hook_0x406A8C
        add esp,0x18
        test ax,ax

        popad

        jne _0x406B24
        jmp u0x406ADF

        _0x406B24:
        jmp u0x406B24
    }
}

// Author: Cups
// Date: 31/10/2017

unsigned u0x404E89 = 0x404E89;
unsigned u0x404FAE = 0x404FAE;
void __declspec(naked) naked_0x404E84()
{
    __asm
    {
        // original
        push eax
        mov ebx,ecx
        mov eax,esi

        // rsa key length
        cmp ecx,0x80
        jne _0x404FAE
        jmp u0x404E89

        _0x404FAE:
        jmp u0x404FAE
    }
}

void Main()
{
    // GetUser (GM Security)
    util::detour((void*)0x406A8C, naked_0x406A8C, 7);
    // CUserCrypto::KeyInit
    util::detour((void*)0x404E84, naked_0x404E84, 5);
}
