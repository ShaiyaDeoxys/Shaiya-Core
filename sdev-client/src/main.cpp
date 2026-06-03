#include "include/main.h"

void Main()
{
    hook::command();
    hook::imgui_layer();
    hook::item_icon();
    hook::name_color();
    hook::packet();
    hook::patch();
    hook::quick_slot();
    hook::raid();
    hook::resolutions();
    hook::select_screen();
    hook::title();
    hook::vehicle();
    hook::window();
    init_discord_rpc();
}
