#pragma once
#include <string>

namespace config
{
    const std::string& ini_path();
    void install_skip_updater();
    void install_id_view();

    // UI folder mode — determines which interface folder the client loads.
    //   EP4 (default): "data/interface"  + EP4 layout patches
    //   EP6:           "data/Intf_epi6"  (no layout patches)
    //   EP7:           "data/Intf_epi7"  (no layout patches)
    // Window title — reads WINDOWTITLE from INI and appends character name.
    void build_window_title(char* output, int outputSize);

    enum class UiMode { EP4, EP6, EP7 };
    UiMode ui_mode();
    const char* ui_interface_path();
    bool ui_needs_layout_patches();
}
