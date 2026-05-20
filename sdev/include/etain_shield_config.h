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
//    Const1=10.0                 ; Max time-delta threshold   (double @ 0x5740D8)
//    Const2=0.13                 ; Per-tick timing tolerance  (float  @ 0x5740E4)
//    Const3=3.0                  ; Timing multiplier          (float  @ 0x5740D0)
//    Const4=2.0                  ; Timing accumulator addend  (double @ 0x5740C8)
//    Tolerance=1.05              ; Speed multiplier headroom  (1.05 = 5%)
//    ViolationLimit=3            ; Consecutive drops before sending correction
//    MinTickDelta=50             ; (legacy, unused — hardcoded 50ms floor)
//    FreeDistance=0.0            ; (legacy, unused — removed from formula)
//    TeleportThreshold=300.0     ; Skip check if jump exceeds this distance
//
//    [AntiRangeHack]
//    Enabled=1                   ; Toggle for range-hack protections
//    Margin=4                    ; Extra tolerance added to every range check
//    MovingGrace=5               ; Additional tolerance for moving targets (~5m)
//
//    [AntiMoveAttack]
//    Enabled=1                   ; Toggle for move-while-attacking protection
//    MinLockMs=600               ; Minimum lock duration in ms (prevents jump bypass)
//    SkipSkillIds=56             ; Comma-separated skill IDs exempt from lock (dash skills)
//

struct EtainShieldConfig
{
    // [General]
    bool enabled = true;

    // [AntiSpeedHack]
    bool         speedHackEnabled        = true;
    double       speedConst1             = 10.0;
    float        speedConst2             = 0.13f;
    float        speedConst3             = 3.0f;
    double       speedConst4             = 2.0;
    float        speedTolerance          = 1.05f;
    uint8_t      speedViolationThreshold = 3;
    uint32_t     speedMinTickDelta       = 50;    // legacy (unused, hardcoded 50ms floor)
    float        speedFreeDistance        = 0.0f;  // legacy (unused, removed from formula)
    float        speedTeleportThreshold  = 300.0f;

    // [AntiRangeHack]
    bool         rangeHackEnabled        = true;
    int          rangeMargin             = 4;
    int          rangeMovingGrace        = 5;   // Extra range tolerance for moving targets

    // [AntiMoveAttack]
    bool              moveAttackEnabled       = true;
    uint32_t          moveAttackMinLockMs     = 600;  // Minimum lock duration (ms)
    std::vector<int>  moveAttackSkipSkills;           // Skill IDs exempt from lock (dash-type)
};

/// Global config instance — loaded once at startup, read at runtime.
extern EtainShieldConfig g_etainConfig;
