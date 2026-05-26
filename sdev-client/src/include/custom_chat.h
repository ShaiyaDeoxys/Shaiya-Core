#pragma once

// ===========================================================================
// custom_chat — Parallel chat overlay with multi-layer security
// ===========================================================================
//
// Renders a custom ImGui-based chat on top of the native Shaiya chat area.
//
// ---- Message routing ----
//
//  Upper panel (combat/system, types 15–33, 50):
//    Server-originated messages — sanitized for display only (color codes,
//    control chars, URLs, dangerous Unicode).  NO security filtering.
//
//  Lower panel (social chat, types 34+):
//    Player-originated messages — full security pipeline applied.
//
// ---- Security pipeline (lower panel only) ----
//
//  Applied in push_msg(), called from record_chat_type() for types 34+.
//
//  1. TEXT SANITIZATION  (sanitize_message_text)
//     - Strips native color codes (<<XXXXXX).
//     - Removes ASCII control characters (bytes < 0x20, except space).
//     - Removes dangerous Unicode: RTL override (U+202E), zero-width
//       chars (U+200B-200F), bidi controls (U+202A-202E), bidi isolates
//       (U+2066-2069), and BOM (U+FEFF).  Prevents visual name spoofing.
//     - Replaces URLs (http://, https://, www.) with "[link]" — anti-phishing.
//     - Truncates to 256 display characters.
//
//  2. SYSTEM MESSAGE SPOOF PROTECTION  (is_spoofed_system_msg)
//     - Chat types 42 (Warning), 23-31 (Notices), 50 are server-only.
//       If one of these carries a "Player:" sender prefix, it's dropped
//       as a potential spoof attempt.
//     - Type 41 is Normal chat (confirmed in-game), NOT admin/system.
//
//  3. BLACKLIST / MUTE  (is_muted_by_content)
//     - Searches for any muted player name as a whole word anywhere in
//       the sanitized text.  Word-boundary matching prevents false
//       positives (muting "Jo" won't match "John").
//     - Works for all message formats — no parsing of sender prefix needed.
//     - Blocks both regular players and GMs indiscriminately.
//     - Persisted in CONFIG.ini section [MUTE], survives client restarts.
//     - Commands: /mute PlayerName, /unmute PlayerName.
//     - Max 200 muted players; case-insensitive.
//
//  4. RATE LIMITING  (check_rate_limit)
//     - Keyed on the full sanitized message text.
//     - If the same exact message appears 5+ times within 10 seconds in
//       a public channel, further copies are silently suppressed.
//     - Applies to chat types: 34, 38 (Trade), 39 (Shout), 40, 41
//       (Normal), 49 (Area).  Private channels (Party, Whisper, Guild)
//       are exempt.
//     - Does NOT add artificial cooldowns beyond what the server enforces
//       (e.g. the native 6-second Trade cooldown is respected as-is).
//     - Stale tracker entries are cleaned up every 30 seconds.
//
//  5. DUPLICATE DETECTION  (check_duplicate)
//     - Keyed on the full sanitized text.
//     - If the same text appears 3+ times consecutively, the previous
//       entry in the ring buffer is updated with a "(xN)" counter
//       instead of adding new lines.  Collapses chat flood into one
//       annotated message.
//     - Tracker map is pruned when it exceeds 500 entries.
//
//  6. RENDER CAP
//     - Hard limit of 1536 wrapped lines per panel per frame (512 stored
//       messages x 3 wrapped lines).  draw_line_stack() still draws only
//       the visible slice.
//
// ---- Ring buffer ----
//
//  Messages that pass all checks are stored in a circular ring of 512
//  entries (ParallelChatMsg).  Each entry carries per-message flags
//  (kMsgFlagRateLimited, kMsgFlagDuplicate) and a duplicate count for
//  render-time annotation.
//
// ---- Rendering ----
//
//  render_ingame_chat() reads native chat panel metrics (position, size,
//  scroll offsets) from the hooked 0x75E0-byte chat panel object and
//  draws wrapped text lines via ImGui::GetForegroundDrawList() directly
//  on top of the native chat area.  Upper panel = combat/system types
//  (15-33, 50); lower panel = social chat types (34+).
//
// ===========================================================================

namespace custom_chat
{
    // Entry point: called from prepare_chat_text_for_emojis() for every
    // incoming chat message.  Upper panel types (15–33, 50) are sanitized
    // and pushed directly; lower panel types (34+) go through the full
    // security pipeline before reaching the ring buffer.
    void record_chat_type(int chatType, const char* text);

    // Returns true when the custom chat overlay is active — the caller
    // uses this to hide the native chat text rendering.
    bool hide_native_chat_visuals();

    // Render the overlay.  Called once per ImGui frame from the render
    // thread.  Reads native chat metrics for positioning and draws the
    // upper + lower line stacks.  Text runs are queued for D3DX rendering.
    void render_ingame_chat();

    // Flush queued D3DX text runs using the game's native ID3DXFont.
    // Must be called AFTER ImGui_ImplDX9_RenderDrawData() so the D3DX
    // text renders on top of the ImGui layer (emoji quads).
    void flush_d3dx_text();

    // Draws the GM debug panel controls for custom chat options
    // (wrap widths, color customisation).
    void render_options();

    // /mute PlayerName — add to blacklist (persisted in CONFIG.ini).
    // Returns true if the command was handled.
    bool mute_player(const char* name);

    // /unmute PlayerName — remove from blacklist.
    // Returns true if the command was handled.
    bool unmute_player(const char* name);
}
