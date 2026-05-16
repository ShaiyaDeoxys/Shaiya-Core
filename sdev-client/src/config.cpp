#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <string>
#include <cstdint>
#include <cstdlib>
#include "include/main.h"
#include "include/config.h"

namespace
{
    std::string to_lower_copy(std::string value)
    {
        for (auto& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return value;
    }

    std::string get_client_config_ini_path()
    {
        char moduleFileName[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, moduleFileName, MAX_PATH))
            return ".\\CONFIG.ini";

        std::string path(moduleFileName);
        auto slashPos = path.find_last_of("\\/");
        if (slashPos != std::string::npos)
            path.resize(slashPos + 1);

        path += "CONFIG.ini";
        return path;
    }

    // ID view is always enabled — no CONFIG.ini dependency.
    std::uint8_t g_viewIdEnabled = 1;

    bool load_skip_updater_setting()
    {
        auto iniPath = get_client_config_ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPUPDATER", 0, iniPath.c_str()) != 0;
    }

    bool load_skip_server_selection_setting()
    {
        auto iniPath = get_client_config_ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPSERVERSELECTION", 1, iniPath.c_str()) != 0;
    }

    bool load_skip_mode_selection_setting()
    {
        auto iniPath = get_client_config_ini_path();
        return GetPrivateProfileIntA("ADVANCED", "SKIPMODESELECTION", 1, iniPath.c_str()) != 0;
    }

    int load_custom_ui_level()
    {
        auto iniPath = get_client_config_ini_path();
        char buffer[16]{};
        GetPrivateProfileStringA("ADVANCED", "UI", "", buffer, static_cast<DWORD>(sizeof(buffer)), iniPath.c_str());
        if (buffer[0] == '\0')
            GetPrivateProfileStringA("CONFIG", "UI", "0", buffer, static_cast<DWORD>(sizeof(buffer)), iniPath.c_str());

        return std::atoi(buffer);
    }

    bool load_imgui_overlay_setting()
    {
        auto iniPath = get_client_config_ini_path();
        return GetPrivateProfileIntA("ADVANCED", "IMGUIOVERLAY", 1, iniPath.c_str()) != 0;
    }

    using GetCommandLineAProc = LPSTR(WINAPI*)();
    GetCommandLineAProc g_originalGetCommandLineA = nullptr;
    std::string g_skipUpdaterCommandLine;

    bool command_line_has_start_game(const char* commandLine)
    {
        if (!commandLine)
            return false;

        return to_lower_copy(commandLine).find("start game") != std::string::npos;
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
    __asm
    {
        cmp byte ptr ds:[g_viewIdEnabled], 1
        jne disabled
        mov al, byte ptr ds:[0x0090D1D4]
        cmp al, 1
        je originalcode
        cmp al, 2
        je originalcode
        cmp al, 3
        sete al
        ret

        disabled:
        xor al, al
        ret

        originalcode:
        mov al, 1
        ret
    }
}

namespace config
{
    int load_custom_ui()
    {
        return load_custom_ui_level();
    }

    bool load_imgui_overlay()
    {
        return load_imgui_overlay_setting();
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
