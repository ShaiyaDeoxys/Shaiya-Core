#pragma once

namespace shaiya
{
    struct SDatabase;
}

namespace gm_security
{
    bool is_login_allowed(shaiya::SDatabase* db, const char* username, const char* ipv4);
}
