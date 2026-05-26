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
//    MovingGrace=5               ; Additional tolerance for moving targets (~5m)
//
//    [AntiCutting]
//    Enabled=1                   ; Toggle for anti-cutting protection
//    LockMs=500                  ; Movement freeze duration after each attack (ms)
//    SkipSkillIds=56             ; Comma-separated skill IDs exempt from lock (dash skills)
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
    int          rangeMovingGrace        = 5;   // Extra range tolerance for moving targets

    // [AntiCutting]
    bool              cuttingEnabled          = true;
    uint32_t          cuttingLockMs           = 500;  // Movement freeze duration after attack (ms)
    std::vector<int>  cuttingSkipSkills;              // Skill IDs exempt from lock (dash-type)
};

/// Global config instance — loaded once at startup, read at runtime.
extern EtainShieldConfig g_etainConfig;
