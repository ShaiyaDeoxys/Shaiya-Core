#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <string>
#include <cstdint>
#include <cstdlib>
#include "include/main.h"
#include "include/config.h"
#include "include/game_data_archive.h"
#include "include/imgui_layer_internal.h"

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

    bool load_skip_server_selection_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPSERVERSELECTION", 1, iniPath.c_str()) != 0;
    }

    bool load_skip_mode_selection_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPMODESELECTION", 1, iniPath.c_str()) != 0;
    }

    bool load_imgui_overlay_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "IMGUIOVERLAY", 1, iniPath.c_str()) != 0;
    }

    bool load_custom_chat_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "CUSTOMCHAT", 1, iniPath.c_str()) != 0;
    }

    bool load_emojis_enabled_setting()
    {
        auto& iniPath = config::ini_path();
        return GetPrivateProfileIntA("ADVANCED", "EMOJIS", 1, iniPath.c_str()) != 0;
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
    bool load_imgui_overlay()
    {
        return load_imgui_overlay_setting();
    }

    bool load_custom_chat()
    {
        return load_custom_chat_setting();
    }

    bool load_emojis_enabled()
    {
        return load_emojis_enabled_setting();
    }


    bool load_skip_server_selection()
    {
        return load_skip_server_selection_setting();
    }

    bool load_skip_mode_selection()
    {
        return load_skip_mode_selection_setting();
    }

    void install_skip_updater()
    {
        patch_get_command_line_for_skip_updater();
    }

    void install_id_view()
    {
        util::detour((void*)0x4E5876, naked_0x4E6D76, 5);
    }
}
