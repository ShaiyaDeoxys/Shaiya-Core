#pragma once
#include <cstdint>
#include <vector>

// ===========================================================================
//  EtainShield — Runtime configuration
// ===========================================================================
//
//  Loaded from Data/EtainShield.ini at server startup.
//  If the file is missing, all defaults below apply (everything enabled).
//
//  INI layout:
//
//    [General]
//    Enabled=1                   ; Master switch (0 disables ALL protections)
//
//    [AntiSpeedHack]
//    Enabled=1                   ; Toggle for speed-hack protections
//    MaxSpeedOnFoot=12.5         ; Max speed coefficient on foot  (server-measured ~12 + 0.5 margin)
//    MaxSpeedMounted=13.5        ; Max speed coefficient mounted  (server-measured ~13 + 0.5 margin)
//    ViolationLimit=5            ; Consecutive violations before correction (first 4 forgiven)
//
//    [AntiRangeHack]
//    Enabled=1                   ; Toggle for range-hack protections
//    Margin=4                    ; Extra tolerance added to every range check
//

struct EtainShieldConfig
{
    // [General]
    bool enabled = true;

    // [AntiSpeedHack]
    bool         speedHackEnabled        = true;
    float        speedMaxOnFoot          = 12.5f;  // server-measured ~12 + 0.5 margin
    float        speedMaxMounted         = 13.5f;  // server-measured ~13 + 0.5 margin
    uint8_t      speedViolationThreshold = 5;      // first 4 forgiven, 5th triggers correction

    // [AntiRangeHack]
    bool         rangeHackEnabled        = true;
    int          rangeMargin             = 4;
};

/// Global config instance — loaded once at startup, read at runtime.
extern EtainShieldConfig g_etainConfig;
