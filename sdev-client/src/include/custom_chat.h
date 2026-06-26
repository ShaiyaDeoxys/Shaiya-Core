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
//   5. Per-sender flood guard
//      Public channels only (Normal/Trade/Shout/Area). Catches one sender
//      flooding with DIFFERENT messages (which the text-keyed rate limit
//      misses). >6 messages in 5s is an offense; penalties escalate
//      15s -> 1m -> 5m -> permanent auto-mute (added to the [MUTE] list).
//      Escalation decays after 10 min of calm; the local player is exempt.
//
//   6. Duplicate collapse
//      Consecutive identical messages are collapsed into one ring-buffer entry
//      with an "(xN)" render annotation.
//
//   7. Render cap
//      The overlay stores 512 messages and renders only the visible wrapped
//      slice for each native chat panel.
//
// Rendering:
//   render_ingame_chat() reads native chat metrics from the hooked chat panel
//   object and queues each message's text as native ID3DXFont runs (drawn by
//   flush_d3dx_text after ImGui, so it matches the native chat exactly and
//   handles the game's codepage directly). Emoji/GIF tokens are drawn inline as
//   ImGui textured quads.
// ===========================================================================

namespace custom_chat
{
    void record_chat_type(int chatType, const char* text);
    void clear_messages();
    void load_chat_font();
    bool hide_native_chat_visuals();
    void render_ingame_chat();
    void render_options();

    // Draw the chat text runs queued by render_ingame_chat with the game's
    // native ID3DXFont. Call once per frame, after ImGui builds its draw data.
    void flush_d3dx_text();

    // Send a UTF-8 line as ordinary (white) player chat, mirroring the native
    // normal-chat send (game.exe FUN_0047A5F0 case 0). Byte-transparent, so
    // UTF-8 passes through intact.

    // Commands.
    bool mute_player(const char* name);
    bool unmute_player(const char* name);
}
