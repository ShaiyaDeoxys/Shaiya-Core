#pragma once
#include <string>

namespace config
{
    const std::string& ini_path();
    bool load_imgui_overlay();
    bool load_custom_chat();
    bool load_emojis_enabled();
    bool load_skip_server_selection();
    bool load_skip_mode_selection();
    void install_skip_updater();
    void install_id_view();
}
