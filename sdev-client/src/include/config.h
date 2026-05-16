#pragma once
#include <string>

namespace config
{
    int load_custom_ui();
    bool load_imgui_overlay();
    bool load_skip_server_selection();
    bool load_skip_mode_selection();
    void install_skip_updater();
    void install_id_view();
}
