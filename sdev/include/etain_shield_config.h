#pragma once
#include <cstdint>

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
//    Tolerance=1.25              ; Speed multiplier headroom  (1.25 = 25%)
//    ViolationLimit=3            ; Consecutive violations before pullback
//    MinTickDelta=50             ; Minimum ms between checks
//    FreeDistance=5.0            ; Unconditional distance allowance (world units)
//    TeleportThreshold=300.0     ; Skip check if jump exceeds this distance
//
//    [AntiRangeHack]
//    Enabled=1                   ; Toggle for range-hack protections
//    Margin=4                    ; Extra tolerance added to every range check
//
//    [AntiMoveAttack]
//    Enabled=1                   ; Toggle for move-while-attacking protection
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
    float        speedTolerance          = 1.25f;
    uint8_t      speedViolationThreshold = 3;
    uint32_t     speedMinTickDelta       = 50;
    float        speedFreeDistance        = 5.0f;
    float        speedTeleportThreshold  = 300.0f;

    // [AntiRangeHack]
    bool         rangeHackEnabled        = true;
    int          rangeMargin             = 4;

    // [AntiMoveAttack]
    bool         moveAttackEnabled       = true;
};

/// Global config instance — loaded once at startup, read at runtime.
extern EtainShieldConfig g_etainConfig;
