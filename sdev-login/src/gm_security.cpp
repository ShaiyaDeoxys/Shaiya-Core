#include "include/gm_security.h"

#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "include/shaiya/SDatabase.h"

namespace gm_security
{
    namespace
    {
        constexpr const char* kIniFileName = "Data\\GMSecurity.ini";
        constexpr const char* kIniSection = "IP_WHITELIST";
        constexpr const char* kSettingsSection = "SETTINGS";
        constexpr int kMaxUsernameLen = 32;

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

        bool is_feature_enabled()
        {
            const char* iniPath = get_ini_path();
            if (!iniPath[0])
                return false;

            return GetPrivateProfileIntA(kSettingsSection, "Enabled", 0, iniPath) != 0;
        }

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
                return 0;

            if (allowedIps[0] == '\0')
                return -1;

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

                if (tokenLen == ipLen && std::strncmp(tokenStart, ipv4, tokenLen) == 0)
                    return +1;
            }

            return -1;
        }

        bool is_db_admin_account(shaiya::SDatabase* db, const char* username)
        {
            if (!db || !username || !username[0])
                return false;

            const char* query =
                "SELECT TOP 1 [Admin], [AdminLevel], [Status] "
                "FROM [PS_UserData].[dbo].[Users_Master] "
                "WHERE [UserID] = ?";

            if (shaiya::SDatabase::PrepareSql(db, query))
                return false;

            char user[kMaxUsernameLen + 1]{};
            strncpy_s(user, username, kMaxUsernameLen);

            if (shaiya::SDatabase::BindParameter(db, 1, kMaxUsernameLen,
                    SQL_C_CHAR, SQL_VARCHAR, user, nullptr, SQL_PARAM_INPUT))
            {
                SQLFreeStmt(db->stmt, SQL_CLOSE);
                SQLFreeStmt(db->stmt, SQL_RESET_PARAMS);
                return false;
            }

            if (shaiya::SDatabase::ExecuteSql(db))
            {
                SQLFreeStmt(db->stmt, SQL_CLOSE);
                SQLFreeStmt(db->stmt, SQL_RESET_PARAMS);
                return false;
            }

            bool isAdmin = false;
            auto fetchRc = SQLFetch(db->stmt);

            if (fetchRc == SQL_SUCCESS)
            {
                unsigned char adminFlag = 0;
                SQLLEN ind1 = 0;
                SQLGetData(db->stmt, 1, SQL_C_UTINYINT, &adminFlag, sizeof(adminFlag), &ind1);

                unsigned char adminLevel = 0;
                SQLLEN ind2 = 0;
                SQLGetData(db->stmt, 2, SQL_C_UTINYINT, &adminLevel, sizeof(adminLevel), &ind2);

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

    bool is_login_allowed(shaiya::SDatabase* db, const char* username, const char* ipv4)
    {
        if (!is_feature_enabled())
            return true;

        int whitelistResult = check_whitelist_ip(username, ipv4);
        if (whitelistResult == -1)
            return false;

        if (whitelistResult == 0 && is_db_admin_account(db, username))
            return false;

        return true;
    }
}
