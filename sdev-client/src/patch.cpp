#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unordered_map>
#include <util/util.h>
#include "include/main.h"
#include "include/shaiya/Static.h"
#include "include/config.h"
#include "include/interface_redirect.h"
#include "include/ep4_ui.h"
#include "include/stat_color.h"
#include "include/server_selection.h"
#include <cstdint>
#include <cstring>

namespace
{
    // -----------------------------------------------------------------------
    // Miscellaneous patch constants
    // -----------------------------------------------------------------------
    constexpr std::uint8_t kBuffSpacing = 0x24;
    constexpr std::uint8_t kBuffCountPerRow = 0x09;
    constexpr std::int32_t kFastTransitionDelay = 0x00000064;
    constexpr std::uint32_t kLoginSplashSkipSplashSleepDelay = 0x00000000;
    constexpr std::uint32_t kLoginSplashSkipPostInitDelay = 0x00000000;
    constexpr char kLoginSplashSkipHiddenCopyrightMessage[100] = "";
    constexpr std::int32_t kClientRessLeaderVisualSeconds = 5;
    float gLogoutGameOverVisualSeconds = 2.0f;
    constexpr std::int32_t kLoginToCharacterSelectionDelayMs = 1000;
    constexpr std::uint32_t kHiddenLevelUpMessageSize = 0x00000000;

    // -----------------------------------------------------------------------
    // Subaction message cooldown (sysmsg 5228-5237)
    // -----------------------------------------------------------------------
    constexpr bool kEnableSubactionMessageCooldown = true;
    constexpr std::uint32_t kSubactionMessageCooldownMs = 20000;
    constexpr std::int32_t kSubactionMessageFirst = 5228;
    constexpr std::int32_t kSubactionMessageLast = 5237;
}

int should_suppress_subaction_sysmsg(int messageNumber)
{
    if (!kEnableSubactionMessageCooldown)
        return false;

    if (messageNumber < kSubactionMessageFirst || messageNumber > kSubactionMessageLast)
        return false;

    const char* playerName = shaiya::g_var->sysmsg_p.data();
    if (!playerName || !playerName[0])
        playerName = "<unknown>";

    static std::unordered_map<std::string, std::uint32_t> lastShownByPlayer;
    auto now = GetTickCount();
    auto& lastShown = lastShownByPlayer[playerName];

    if (lastShown && now - lastShown < kSubactionMessageCooldownMs)
        return true;

    lastShown = now;
    return false;
}

unsigned u0x423155 = 0x423155;
void __declspec(naked) naked_sysmsg_to_chatbox_subaction_cooldown()
{
    __asm
    {
        pushad
        push dword ptr [esp+0x28] // messageNumber
        call should_suppress_subaction_sysmsg
        add esp,0x4
        test eax,eax
        popad

        jne suppress

        // original
        push esi
        mov esi,dword ptr [esp+0x10]
        jmp u0x423155

        suppress:
        ret
    }
}

// ===========================================================================
// hook::patch — main orchestrator
// ===========================================================================
void hook::patch()
{
    static constexpr unsigned char kPushZero[] = {0x6A, 0x00};
    static constexpr unsigned char kPushMinusOne[] = {0x68, 0xFF, 0xFF, 0xFF, 0xFF};
    static constexpr unsigned char kBypassCharacterCreateNameFormatCheck[] = {0xEB, 0x34, 0x90, 0x90};
    static constexpr unsigned char kBypassCharacterCreateNameSubstringChecks[] = {0xE9, 0x84, 0x00, 0x00, 0x00, 0x90};
    static constexpr unsigned char kBypassCharacterCreateNameVerifiedGate[] = {0xEB};
    static constexpr unsigned char kBypassCharacterCreateNameAvailabilityRequest[] = {0xE9, 0xC2, 0x04, 0x00, 0x00};
    static constexpr unsigned char kTreatCharacterCreateResultBusyAsSuccess[] = {0x74, 0x22};
    static constexpr unsigned char kForceUltimateModeOnCharacterCreate[] =
    {
        0xC7, 0x86, 0xC8, 0x24, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    static constexpr unsigned char kLoginSplashSkipRenderBlock[] = {0xE9, 0xD8, 0x02, 0x00, 0x00};
    static constexpr unsigned char kLoginSplashSkipAfterResourceInit[] = {0xE9, 0x96, 0x02, 0x00, 0x00};

    // -- CONFIG.ini driven features ----------------------------------------
    const auto customUiLevel = config::load_custom_ui();

    if (customUiLevel > 0)
    {
        // ADVANCED -> UI=1 redirects the stock data/interface folder to
        // data/intf_epi6.  UI=2 redirects to data/intf_epi8.
        // The folder names have the same length (9 chars), so this can be
        // patched in-place without relocating Game.exe strings.
        interface_redirect::patch_folder_to_custom(customUiLevel);
    }

    // PNG interface support.
    // Rewrites known interface texture references in Game.exe from
    // .tga/.jpg to .png so the client can load a PNG-based interface pack.
    interface_redirect::patch_texture_extensions_to_png();

    // Screenshot output format.
    // Rewrites the internal screenshot filename templates from .jpg/.JPG to .png.
    interface_redirect::patch_screenshot_extensions_to_png();

    // Cooldown for subaction messages (20s).
    // Instead of removing sysmsg 5228-5237 completely, rate-limit the whole
    // subaction message block to one visible message every 20 seconds per
    // player name (<p>). Comment this detour out to restore stock behavior.
    util::detour((void*)0x423150, naked_sysmsg_to_chatbox_subaction_cooldown, 5);

    // Skip updater check via CONFIG.ini.
    // ADVANCED -> SKIPUPDATER=1 makes the client see the same "start game"
    // command-line token normally supplied by Updater.exe.
    config::install_skip_updater();

    // Skip server selection screen.
    if (config::load_skip_server_selection())
    {
        server_selection::install_skip_hooks();
    }

    // -- EP4/EP5 UI layout hooks -------------------------------------------
    ep4_ui::install_hooks(customUiLevel > 0);
    stat_color::install_stats_color_hooks();

    // -- Miscellaneous inline patches --------------------------------------

    // remove ep6 vehicle section (auction board)
    util::write_memory((void*)0x463FE0, 0x07, 1);
    // speed recreation
    util::write_memory((void*)0x4C4D2F, 0x02, 1);
    // speed enchant
    util::write_memory((void*)0x501600, 0x02, 1);
    util::write_memory((void*)0x501602, 0x02, 1);
    util::write_memory((void*)0x501631, 0x02, 1);
    util::write_memory((void*)0x501633, 0x02, 1);
    util::write_memory((void*)0x501644, 0x03, 1);
    util::write_memory((void*)0x50164D, 0x03, 1);
    // Buff row position is now handled by the movable buff hook in ep6_repo_features.cpp.
    // Keep only spacing/count here so the runtime detours own the X/Y instructions.
    // 004D74E7  add edi,28
    // 004D7509  add dword ptr [esp+10],28
    util::write_memory((void*)0x4D74E9, &kBuffSpacing, sizeof(kBuffSpacing));
    util::write_memory((void*)0x4D750D, &kBuffSpacing, sizeof(kBuffSpacing));
    // 004D74EA  cmp eax,09
    util::write_memory((void*)0x4D74EC, &kBuffCountPerRow, sizeof(kBuffCountPerRow));
    // Fast transition delay.
    // 00436940  cmp eax,00000BB8
    util::write_memory((void*)0x436941, &kFastTransitionDelay, sizeof(kFastTransitionDelay));
    // 004D0340  push 00001388
    util::write_memory((void*)0x4D0341, &kFastTransitionDelay, sizeof(kFastTransitionDelay));
    // View ID visibility — always enabled, no CONFIG.ini dependency.
    config::install_id_view();
    // Login splash skip.
    // Hides the Nexon/copyright startup splash and skips its render/fade loops
    // while preserving the login resource setup that the account screen needs.
    // Do not replace this with a direct state-table jump to login: that looks
    // correct visually, but opens login with missing initialization and crashes
    // after the account login step.
    util::write_memory((void*)0x74A5A0, kLoginSplashSkipHiddenCopyrightMessage, sizeof(kLoginSplashSkipHiddenCopyrightMessage));
    util::write_memory((void*)0x4346B1, &kLoginSplashSkipSplashSleepDelay, sizeof(kLoginSplashSkipSplashSleepDelay));
    util::write_memory((void*)0x434ACD, &kLoginSplashSkipPostInitDelay, sizeof(kLoginSplashSkipPostInitDelay));
    util::write_memory((void*)0x4346BB, kLoginSplashSkipRenderBlock, sizeof(kLoginSplashSkipRenderBlock));
    // 00434A92 calls the login resource init/check on 007C48FC. Once that has
    // run, advance to the original state-1 transition block instead of drawing
    // the remaining splash frames.
    util::write_memory((void*)0x434A97, kLoginSplashSkipAfterResourceInit, sizeof(kLoginSplashSkipAfterResourceInit));
    // Ress leader visual timer.
    // The popup text comes from sysmsg 10068 and its countdown renders using
    // global 007AD434, initialized as 30 seconds at 004D5F41. This is separate
    // from the server-side 5000ms gameplay timer and must be patched in seconds.
    util::write_memory((void*)0x4D5F47, &kClientRessLeaderVisualSeconds, sizeof(kClientRessLeaderVisualSeconds));
    // Logout/game-over visual countdown.
    // The "x seconds left till game over" text uses sysmsg 10083/10084. The
    // countdown itself is a float copied into global 022B045C at 005223A9 and
    // then decremented every frame, so patch the initializer instead of the
    // later display clamp. This keeps the visible number ticking down normally.
    std::uint8_t logoutVisualInitPatch[] = { 0xD9, 0x05, 0x00, 0x00, 0x00, 0x00 };
    auto logoutVisualSecondsAddress = reinterpret_cast<std::uint32_t>(&gLogoutGameOverVisualSeconds);
    std::memcpy(&logoutVisualInitPatch[2], &logoutVisualSecondsAddress, sizeof(logoutVisualSecondsAddress));
    util::write_memory((void*)0x5223A9, logoutVisualInitPatch, sizeof(logoutVisualInitPatch));
    if (config::load_skip_server_selection())
    {
        // Login to character selection delay.
        // Keep the safe stock update/selection path, but shorten the hidden
        // single-server bridge timeout from the stock 30000ms to 1000ms.
        util::write_memory((void*)0x50C708, &kLoginToCharacterSelectionDelayMs, sizeof(kLoginToCharacterSelectionDelayMs));
        util::write_memory((void*)0x50C869, &kLoginToCharacterSelectionDelayMs, sizeof(kLoginToCharacterSelectionDelayMs));
    }
    // Remove level up messages.
    // Keep the stock levelup_skillup.tga/levelup_statusup.tga creation flow
    // intact, but force their 256x256 render size arguments to 0x0 so the
    // level-up splash textures are not visible.
    util::write_memory((void*)0x4AC538, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC53D, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC603, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC608, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    // Character creation name format check.
    // The creation UI calls the old single-byte name validator at 00564C40.
    // Keep empty/short-name checks and server-side validation intact, but skip
    // the local ASCII-only rejection so UTF-8/special names can reach the next
    // creation step.
    util::write_memory((void*)0x472214, kBypassCharacterCreateNameFormatCheck, sizeof(kBypassCharacterCreateNameFormatCheck));
    // The same flow checks several blocked substrings before sending the name
    // availability request. UTF-8 byte sequences can collide with these legacy
    // single-byte checks, so bypass only that local pattern filter.
    util::write_memory((void*)0x472280, kBypassCharacterCreateNameSubstringChecks, sizeof(kBypassCharacterCreateNameSubstringChecks));
    // The creation panel also has a separate "name already verified" flag
    // ([esi+2B8C]) that normally forces the user through the availability button
    // before the create action is allowed. Skip that UI gate entirely.
    util::write_memory((void*)0x472146, kBypassCharacterCreateNameVerifiedGate, sizeof(kBypassCharacterCreateNameVerifiedGate));
    // The stock client sends a 0x119 name-availability request here, then shows
    // a local confirmation dialog before it actually sends the 0x102 character
    // creation packet. This server build does not use that client-side gate, so
    // jump directly to the 0x102 creation path.
    util::write_memory((void*)0x47243A, kBypassCharacterCreateNameAvailabilityRequest, sizeof(kBypassCharacterCreateNameAvailabilityRequest));
    // Character creation result handling.
    // Some UTF-8/special names come back through the legacy "busy" result path
    // even after the local availability check was accepted.
    util::write_memory((void*)0x472A12, kTreatCharacterCreateResultBusyAsSuccess, sizeof(kTreatCharacterCreateResultBusyAsSuccess));
    if (config::load_skip_mode_selection())
    {
        // Skip mode selection screen and force Ultimate Mode by default.
        util::detour((void*)0x471D4E, (void*)0x471DF7, 169);
        util::write_memory((void*)0x472907, kForceUltimateModeOnCharacterCreate, sizeof(kForceUltimateModeOnCharacterCreate));
    }
    // Background rendering behavior.
    // These byte patches replace the original push arguments with:
    // - push 0
    // - push -1 (0xFFFFFFFF)
    util::write_memory((void*)0x434742, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x434880, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x434B42, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x4347FC, kPushMinusOne, sizeof(kPushMinusOne));
    util::write_memory((void*)0x43493A, kPushMinusOne, sizeof(kPushMinusOne));
    // Remove vanilla GM H-key HP viewer.
    util::write_memory((void*)0x534817, 0x1, 1);
    // costume lag workaround
    util::write_memory((void*)0x56F38D, 0x75, 1);
    util::write_memory((void*)0x583DED, 0x75, 1);
    // pet/wing lag workaround
    util::write_memory((void*)0x5881EE, 0x85, 1);
}

// ===========================================================================
// hook::select_screen
// ===========================================================================
void hook::select_screen()
{
    ep4_ui::install_select_screen_hooks();
}
