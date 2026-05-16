// ===========================================================================
// custom_game.cpp — Miscellaneous gameplay patches
//
// Small standalone patches that don't belong to a specific feature module.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include "include/main.h"

void hook::custom_game()
{
    // NPC-to-go bug workaround.
    // Prevents the client from locking up when an NPC dialog is opened
    // while the character is mid-movement.
    util::write_memory((void*)0x444129, 0xEB, 1);

    // Show dungeon maps (remove the conditional skip).
    util::write_memory((void*)0x4D9497, 0x90, 2);
}
