#pragma once

// ===========================================================================
// custom_chat - Parallel chat overlay with layered client-side filtering
// ===========================================================================
//
// Renders a custom ImGui/D3DX chat overlay on top of the native Shaiya chat
// panels. The native chat still provides layout, scroll state, and message
// source data; this module owns sanitization, filtering, wrapping, and drawing.
//
// Message routing:
//   Upper panel (combat/system, types 15-33, 50)
//     Server-originated messages. These are sanitized for display but do not
//     pass through player-spam filtering.
//
//   Lower panel (social chat, types 34+)
//     Player-originated messages. These pass through the full filtering
//     pipeline before entering the local ring buffer.
//
// Lower-panel filtering:
//   1. Text sanitization
//      Removes native color codes, ASCII control characters, dangerous Unicode
//      controls, and replaces URLs with "[link]".
//
//   2. System-message spoof guard
//      Drops player-looking sender prefixes on chat types reserved for system
//      or notice messages.
//
//   3. Mute list
//      Matches muted names as whole words in sanitized text. The list is
//      persisted in CONFIG.ini under [MUTE].
//
//   4. Rate limit
//      Suppresses repeated public-channel messages after 5 copies in 10
//      seconds. Private channels are exempt.
//
//   5. Duplicate collapse
//      Consecutive identical messages are collapsed into one ring-buffer entry
//      with an "(xN)" render annotation.
//
//   6. Render cap
//      The overlay stores 512 messages and renders only the visible wrapped
//      slice for each native chat panel.
//
// Rendering:
//   render_ingame_chat() reads native chat metrics from the hooked chat panel
//   object and queues text runs. flush_d3dx_text() draws those runs with the
//   game's native ID3DXFont after ImGui has rendered, keeping text above emoji
//   quads while preserving the native font appearance.
// ===========================================================================

namespace custom_chat
{
    void record_chat_type(int chatType, const char* text);
    bool hide_native_chat_visuals();
    void render_ingame_chat();
    void flush_d3dx_text();
    void render_options();

    // Commands.
    bool mute_player(const char* name);
    bool unmute_player(const char* name);
}
