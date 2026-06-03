#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include "include/main.h"
#include "include/config.h"
#include "include/game_data_archive.h"
#include "include/imgui_layer_internal.h"
#include "include/shaiya/CPlayerData.h"

namespace config
{
    const std::string& ini_path()
    {
        static std::string path = game_data::relative_path("CONFIG.ini");
        return path;
    }
}

namespace
{
    bool load_skip_updater_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPUPDATER", 0, iniPath.c_str()) != 0;
    }

    using GetCommandLineAProc = LPSTR(WINAPI*)();
    GetCommandLineAProc g_originalGetCommandLineA = nullptr;
    std::string g_skipUpdaterCommandLine;

    bool command_line_has_start_game(const char* commandLine)
    {
        if (!commandLine)
            return false;

        return game_data::lower_ascii(commandLine).find("start game") != std::string::npos;
    }

    LPSTR WINAPI hooked_get_command_line_a()
    {
        auto commandLine = g_originalGetCommandLineA
            ? g_originalGetCommandLineA()
            : GetCommandLineA();

        if (!load_skip_updater_setting() || command_line_has_start_game(commandLine))
            return commandLine;

        // The stock client uses the "start game" command-line token to know it
        // was launched by Updater.exe. Add it at runtime instead of editing the
        // executable or forcing updater-state flags that can change too late.
        g_skipUpdaterCommandLine = commandLine ? commandLine : "";
        g_skipUpdaterCommandLine += " start game";
        return g_skipUpdaterCommandLine.data();
    }

    void patch_get_command_line_for_skip_updater()
    {
        auto importSlot = reinterpret_cast<GetCommandLineAProc*>(0x746160);
        if (!g_originalGetCommandLineA)
            g_originalGetCommandLineA = *importSlot;

        auto hook = &hooked_get_command_line_a;
        util::write_memory(importSlot, &hook, sizeof(hook));
    }
}

void __declspec(naked) naked_0x4E6D76()
{
    using namespace imgui_layer;
    __asm
    {
        // Check live toggle — if disabled, return 0 immediately
        cmp byte ptr [g_idViewEnabled], 0
        je disabled

        mov al, byte ptr ds:[0x0090D1D4]
        cmp al, 1
        je originalcode
        cmp al, 2
        je originalcode
        cmp al, 3
        sete al
        ret

        originalcode:
        mov al, 1
        ret

        disabled:
        xor al, al
        ret
    }
}

namespace config
{
    void install_skip_updater()
    {
        patch_get_command_line_for_skip_updater();
    }

    void install_id_view()
    {
        util::detour((void*)0x4E5876, naked_0x4E6D76, 5);
    }

    static UiMode g_uiMode = UiMode::EP4;
    static bool g_uiModeLoaded = false;

    UiMode ui_mode()
    {
        if (!g_uiModeLoaded)
        {
            g_uiModeLoaded = true;
            char buf[16]{};
            GetPrivateProfileStringA("ADVANCED", "UI", "EP4",
                buf, sizeof(buf), ini_path().c_str());

            if (_stricmp(buf, "EP6") == 0)
                g_uiMode = UiMode::EP6;
            else if (_stricmp(buf, "EP7") == 0)
                g_uiMode = UiMode::EP7;
            else
                g_uiMode = UiMode::EP4;
        }
        return g_uiMode;
    }

    const char* ui_interface_path()
    {
        switch (ui_mode())
        {
        case UiMode::EP6: return "data/Intf_epi6";
        case UiMode::EP7: return "data/Intf_epi7";
        default:          return "data/interface";
        }
    }

    bool ui_needs_layout_patches()
    {
        return ui_mode() == UiMode::EP4;
    }

    static char g_windowTitleBase[128] = "Shaiya";
    static bool g_windowTitleBaseLoaded = false;

    static const char* window_title_base()
    {
        if (!g_windowTitleBaseLoaded)
        {
            g_windowTitleBaseLoaded = true;
            GetPrivateProfileStringA("ADVANCED", "WINDOWTITLE", "Shaiya",
                g_windowTitleBase, sizeof(g_windowTitleBase), ini_path().c_str());
        }
        return g_windowTitleBase;
    }

    void build_window_title(char* output, int outputSize)
    {
        if (!output || outputSize <= 0)
            return;

        const auto* base = window_title_base();
        auto* playerData = reinterpret_cast<shaiya::CPlayerData*>(0x90D1D0);

        if (playerData->charId != 0 && playerData->charName[0] != '\0')
            _snprintf_s(output, outputSize, _TRUNCATE, "%s - Playing as %s",
                base, playerData->charName.data());
        else
            strncpy_s(output, outputSize, base, _TRUNCATE);
    }
}
