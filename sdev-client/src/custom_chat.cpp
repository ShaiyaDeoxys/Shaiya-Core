#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <external/imgui/imgui.h>
#include "include/config.h"
#include "include/custom_chat.h"
#include "include/game_addresses.h"
#include "include/imgui_layer_internal.h"

#pragma comment(lib, "imm32.lib")

// ===========================================================================
// custom_chat.cpp — Parallel chat overlay with multi-layer security
//
// See custom_chat.h for the full security architecture overview.
//
// Security pipeline applies ONLY to lower-panel messages (social chat,
// types 34+).  Upper-panel messages (combat/system, types 15–33, 50) are
// server-originated and bypass filtering — they are sanitized for display
// only and pushed directly to the ring buffer.
//
// File layout:
//   1. Data structures and constants
//   2. Security layer 1: Blacklist / mute list       (CONFIG.ini [MUTE])
//   3. Security layer 2: Rate limiting               (per-text, public channels)
//   4. Security layer 3: Duplicate detection         (consecutive collapse)
//   5. Security layer 4: Text sanitization           (control chars, URLs, unicode)
//   6. Security layer 5: System message spoof guard
//   7. Security layer 6: Blacklist substring check
//   8. Security layer 7: Render cap
//   9. Ring buffer and security pipeline (push_msg)
//  10. Native chat metrics and rendering
//  11. Public API (record_chat_type, mute_player, unmute_player)
// ===========================================================================

namespace custom_chat
{
    using namespace imgui_layer;

    namespace
    {
        // ===================================================================
        // 1. Data structures and constants
        // ===================================================================

        // A single message in the ring buffer.
        struct ParallelChatMsg
        {
            int chatType = 0;    // Shaiya chat type (see ChatType.h)
            char text[512]{};    // Sanitized display text (post-pipeline)
        };

        // Snapshot of the native chat panel's geometry, read each frame
        // from the hooked 0x75E0-byte chat panel object at g_chatPanelPtr.
        struct NativeChatMetrics
        {
            bool valid = false;
            float baseX = 0.0f;           // Panel left edge (field [04])
            float baseY = 0.0f;           // Panel top (field [08])
            float textX = 0.0f;           // Text start X (baseX + offset)
            float textW = 0.0f;           // Text area width
            float upperH = 0.0f;          // Upper (combat) panel pixel height
            float lowerH = 0.0f;          // Lower (chat) panel pixel height
            float upperFirstLineY = 0.0f; // Y of the first visible upper line
            float lowerFirstLineY = 0.0f; // Y of the first visible lower line
            int sz0 = 0;                  // Native resize byte at +0x3B4
            int sz1 = 0;                  // Native resize byte at +0x3CC
            int upperVisibleLines = 0;
            int lowerVisibleLines = 0;
        };

        // A single wrapped display line (output of append_wrapped_lines).
        // 256 bytes: a max-width (120-column) line of 3-byte CJK glyphs can run
        // ~240 bytes, so the buffer must hold it without a mid-character clamp.
        struct NativeChatLine
        {
            int chatType = 0;
            char text[256]{};
        };

        // --- Ring buffer ---
        constexpr int kMaxParallelMsgs = 512;

        // --- Native chat layout constants (from reverse engineering) ---
        constexpr float kNativeChatLineH = 16.0f;
        constexpr float kNativeChatTextOffsetX = 0x19;        // text indent from panel left
        constexpr float kNativeChatTextWidth = 0x15e;          // text area width in pixels
        constexpr float kNativeUpperFirstLineYOffset = 0x35;   // upper panel first-line offset from baseY
        constexpr float kNativeLowerFirstLineYOffset = 0x5e;   // lower panel first-line offset from baseY
        constexpr int kNativeUpperExtraLines = 2;              // min visible lines when upper is maximized
        constexpr int kNativeLowerExtraLines = 4;              // min visible lines when lower is maximized
        constexpr int kDefaultNativeUpperCharsPerLine = 54;    // default wrap width (upper, combat)
        constexpr int kDefaultNativeLowerCharsPerLine = 47;    // default wrap width (lower, chat)
        constexpr int kNativeMaxLinesPerMsg = 3;               // max wrapped lines per message
        constexpr float kNativeParallelFontSize = 15.0f;       // ImGui font size matching native
        constexpr uintptr_t kNativeUpperScrollValueOffset = 0x16EC;  // scroll float in chat panel object
        constexpr uintptr_t kNativeLowerScrollValueOffset = 0x294C;

        bool g_stateLoaded = false;
        ParallelChatMsg g_ring[kMaxParallelMsgs]{};
        int g_head = 0;
        int g_count = 0;
        bool g_colorCustom[100]{};
        ImVec4 g_color[100]{};
        int g_upperCharsPerLine = kDefaultNativeUpperCharsPerLine;
        int g_lowerCharsPerLine = kDefaultNativeLowerCharsPerLine;

        // ===================================================================
        // 2. Security layer 1: Blacklist / mute list (CONFIG.ini [MUTE])
        // ===================================================================
        //
        // Persistent player blacklist with case-insensitive matching.
        // - Stored in CONFIG.ini under [MUTE] as "playername=1" entries.
        // - Loaded once on first access; writes are immediate (per-entry).
        // - Hard cap of 200 names to prevent abuse / memory bloat.
        // - Commands: /mute PlayerName, /unmute PlayerName (see public API).
        // - Actual filtering is done by is_muted_by_content() (section 7)
        //   which searches the sanitized message text for any muted name
        //   as a whole word — this data structure just holds the set.
        // ===================================================================

        constexpr int kMaxMutedPlayers = 200;       // hard cap on blacklist size
        constexpr const char* kMuteIniSection = "MUTE"; // CONFIG.ini section name

        struct MuteList
        {
            std::unordered_set<std::string> names;  // all names stored lowercase
            bool loaded = false;                    // lazy-load flag
        };

        MuteList g_muteList{};

        // --- Case-insensitive helpers ---

        std::string to_lower(const char* str)
        {
            std::string result;
            if (!str) return result;
            while (*str)
            {
                result.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(*str))));
                ++str;
            }
            return result;
        }

        std::string to_lower(const std::string& str)
        {
            return to_lower(str.c_str());
        }

        // Load the mute list from CONFIG.ini [MUTE] section.
        // Called once via load_state().  The INI section uses the format
        // "key=value\0key=value\0\0" returned by GetPrivateProfileSectionA.
        void load_mute_list()
        {
            if (g_muteList.loaded)
                return;
            g_muteList.loaded = true;

            auto& ini = config::ini_path();
            char buf[8192]{};
            auto len = GetPrivateProfileSectionA(
                kMuteIniSection, buf, sizeof(buf), ini.c_str());

            const char* p = buf;
            while (p < buf + len && *p)
            {
                std::string entry(p);
                auto eq = entry.find('=');
                if (eq != std::string::npos)
                    g_muteList.names.insert(to_lower(entry.substr(0, eq)));
                p += entry.size() + 1;
            }
        }

        // Persist a single mute/unmute change to CONFIG.ini.
        // Writing nullptr as the value deletes the key (Win32 API behavior).
        void save_mute_entry(const std::string& lowerName, bool muted)
        {
            auto& ini = config::ini_path();
            if (muted)
                WritePrivateProfileStringA(
                    kMuteIniSection, lowerName.c_str(), "1", ini.c_str());
            else
                WritePrivateProfileStringA(
                    kMuteIniSection, lowerName.c_str(), nullptr, ini.c_str());
        }

        // ===================================================================
        // 3. Security layer 2: Rate limiting (per-text, public channels)
        // ===================================================================
        //
        // Suppresses identical messages that appear too rapidly in public
        // channels.  Keyed on the full sanitized message text (not sender).
        //
        // Rules:
        //   - If the same exact text appears 5+ times within 10 seconds,
        //     further copies are silently suppressed.
        //   - Only applies to public player channels: 34 (ChatNormal),
        //     38 (Trade), 39 (Shout), 41 (Normal in-game), 49 (Area in-game).
        //   - Type 40 is yellow/system-like in this client and is left
        //     unthrottled so repeated server feedback (for example obstacle
        //     messages) still reaches the chat history.
        //   - Private channels (Party, Whisper, Guild) are exempt.
        //   - Does NOT add artificial cooldowns — the native 6-second Trade
        //     cooldown is respected as-is from the server.
        //   - Stale tracker entries are cleaned up every 30 seconds.
        //
        // Data structure: circular timestamp buffer per unique text string.
        // ===================================================================

        constexpr int kRateLimitMaxMsgs = 5;            // threshold: suppress on 5th
        constexpr DWORD kRateLimitWindowMs = 10000;      // 10-second sliding window
        constexpr DWORD kRateLimitCleanupInterval = 30000; // cleanup sweep every 30s

        // Circular buffer of timestamps for one unique message text.
        struct SenderRateInfo
        {
            DWORD timestamps[kRateLimitMaxMsgs]{};
            int head = 0;   // next write position
            int count = 0;  // entries used (up to kRateLimitMaxMsgs)
        };

        std::unordered_map<std::string, SenderRateInfo> g_rateLimits;
        DWORD g_lastRateLimitCleanup = 0;

        // Returns true if the message should be suppressed (rate exceeded).
        // `key` is the full sanitized message text — identical messages from
        // any source are rate-limited together, which catches both single-
        // sender spam and coordinated multi-sender spam.
        bool check_rate_limit(const std::string& key, int chatType)
        {
            // Only rate-limit public channels.
            // Private channels (Party=35, Whisper=36, Guild=37, Raid) are
            // exempt — players need unrestricted comms in groups.
            if (chatType != 34 && chatType != 38 && chatType != 39 &&
                chatType != 41 && chatType != 49)
                return false;

            auto now = GetTickCount();

            // Periodic cleanup: remove entries whose timestamps are all
            // outside the window.  Prevents unbounded map growth from
            // many unique messages over long sessions.
            if (now - g_lastRateLimitCleanup > kRateLimitCleanupInterval)
            {
                g_lastRateLimitCleanup = now;
                for (auto it = g_rateLimits.begin(); it != g_rateLimits.end();)
                {
                    bool stale = true;
                    for (int i = 0; i < it->second.count; ++i)
                    {
                        if (now - it->second.timestamps[i] < kRateLimitWindowMs)
                        {
                            stale = false;
                            break;
                        }
                    }
                    if (stale)
                        it = g_rateLimits.erase(it);
                    else
                        ++it;
                }
            }

            auto& info = g_rateLimits[key];

            // Count how many of the stored timestamps fall within the window
            int recentCount = 0;
            for (int i = 0; i < info.count; ++i)
            {
                if (now - info.timestamps[i] < kRateLimitWindowMs)
                    ++recentCount;
            }

            // Record this occurrence (overwrites oldest if buffer full)
            info.timestamps[info.head] = now;
            info.head = (info.head + 1) % kRateLimitMaxMsgs;
            if (info.count < kRateLimitMaxMsgs)
                ++info.count;

            return recentCount >= kRateLimitMaxMsgs;
        }

        // ===================================================================
        // 4. Security layer 3: Duplicate detection (consecutive collapse)
        // ===================================================================
        //
        // Collapses runs of identical messages into a single entry with an
        // "(xN)" annotation, keeping the chat area readable during floods.
        //
        // Rules:
        //   - Keyed on the full sanitized text (same key space as rate limit).
        //   - If the same text appears 3+ times consecutively, instead of
        //     adding a new ring entry, the PREVIOUS entry is updated with
        //     a "(xN)" counter and the new message is absorbed.
        //   - This is independent of rate limiting — a message can pass the
        //     rate check but still be collapsed as a duplicate.
        //   - The tracker map is pruned when it exceeds 500 entries to
        //     prevent unbounded growth during long sessions.
        //
        // Rendering: render_ingame_chat() reads kMsgFlagDuplicate and
        // g_msgDupeCount[] to append "(xN)" to the display text.
        // ===================================================================

        constexpr int kDuplicateThreshold = 3;  // collapse on 3rd consecutive

        struct DuplicateTracker
        {
            std::string lastText;       // last message text seen
            int consecutiveCount = 0;   // how many times in a row
        };

        // Returns the consecutive duplicate count for this text.
        // If >= kDuplicateThreshold, the caller should collapse the message
        // instead of adding a new ring buffer entry.
        int check_duplicate(const std::string& sanitizedText)
        {
            // Per-text tracker: counts consecutive runs of identical messages
            // across all chat types.  Using the sanitized text as the key
            // means "PlayerA: hello" and "PlayerB: hello" are counted as the
            // same message — intentional, to catch coordinated spam.
            static std::unordered_map<std::string, DuplicateTracker> textTrackers;

            auto& tracker = textTrackers[sanitizedText];

            if (sanitizedText == tracker.lastText)
            {
                ++tracker.consecutiveCount;
                return tracker.consecutiveCount;
            }

            tracker.lastText = sanitizedText;
            tracker.consecutiveCount = 1;

            // Prune tracker map when it grows too large — remove entries
            // that are no longer in a consecutive run (count <= 1).
            if (textTrackers.size() > 500)
            {
                for (auto it = textTrackers.begin(); it != textTrackers.end();)
                {
                    if (&it->second != &tracker && it->second.consecutiveCount <= 1)
                        it = textTrackers.erase(it);
                    else
                        ++it;
                }
            }

            return 1;
        }

        // ===================================================================
        // 5. Security layer 4: Text sanitization
        // ===================================================================
        //
        // All incoming messages pass through sanitize_message_text() before
        // any other security check.  This is the first line of defense and
        // normalizes the text for all downstream comparisons.
        //
        // Sanitization steps (applied in order during a single pass):
        //   a. Strip native color codes: <<XXXXXX (6-digit hex after "<<")
        //   b. Remove ASCII control characters (bytes < 0x20, except space)
        //   c. Remove dangerous Unicode (prevents visual name spoofing):
        //      - RTL override (U+202E), LTR override (U+202F)
        //      - Zero-width chars (U+200B–200F)
        //      - Bidi controls (U+202A–202E)
        //      - Bidi isolates (U+2066–2069)
        //      - BOM / zero-width no-break (U+FEFF)
        //   d. Replace URLs (http://, https://, www.) with "[link]" — anti-phishing
        //   e. Truncate to 256 display characters
        //
        // extract_sender_name() is a separate utility that parses the native
        // "PlayerName: message" format.  Used by the spoof guard (section 6)
        // to detect whether a system-only chat type carries a player prefix.
        // ===================================================================

        // Parse the sender name from the native message format.
        // Returns the name lowercased, or empty string if no valid name found.
        // Only used by is_spoofed_system_msg() — NOT for mute/rate-limit
        // (those use the full text as key to avoid dependence on format).
        std::string extract_sender_name(const char* text, bool lower = true)
        {
            if (!text || !text[0])
                return {};

            const char* p = text;

            // Skip leading color codes <<XXXXXX
            while (*p == '<' && *(p + 1) == '<')
            {
                p += 2;
                int hexCount = 0;
                while (hexCount < 6 &&
                       std::isxdigit(static_cast<unsigned char>(p[hexCount])))
                    ++hexCount;
                if (hexCount >= 5)
                    p += hexCount;
                else
                    break;
            }

            // Skip whitespace after color codes
            while (*p == ' ')
                ++p;

            // Find the colon separator
            const char* colon = std::strchr(p, ':');
            if (!colon || colon == p || (colon - p) > 24)
                return {};

            // Validate name: only alphanumeric chars (Shaiya names)
            bool validName = true;
            for (const char* c = p; c < colon; ++c)
            {
                auto ch = static_cast<unsigned char>(*c);
                if (!std::isalnum(ch))
                {
                    validName = false;
                    break;
                }
            }

            if (!validName)
                return {};

            std::string name(p, colon);
            return lower ? to_lower(name) : name;
        }

        // Core sanitization function — processes raw message text and writes
        // the cleaned result into `out`.  Single-pass, O(n) on input length.
        // The output is used as the key for rate limiting, duplicate detection,
        // and mute matching, so all those checks operate on normalized text.
        void sanitize_message_text(const char* text, std::string& out)
        {
            out.clear();
            if (!text)
                return;

            constexpr int kMaxDisplayLength = 256;
            int charCount = 0;

            for (std::size_t i = 0; text[i] != '\0' && charCount < kMaxDisplayLength;)
            {
                auto ch = static_cast<unsigned char>(text[i]);

                // Strip color codes <<XXXXXX
                if (text[i] == '<' && text[i + 1] == '<')
                {
                    int hexCount = 0;
                    while (hexCount < 6 &&
                           std::isxdigit(static_cast<unsigned char>(text[i + 2 + hexCount])))
                        ++hexCount;

                    if (hexCount >= 5)
                    {
                        i += 2 + hexCount;
                        continue;
                    }
                }

                // Strip ASCII control characters (0x00..0x1F)
                if (ch < 0x20)
                {
                    ++i;
                    continue;
                }

                // Strip Unicode RTL override (U+202E) and other
                // bidirectional manipulation bytes.
                // UTF-8 encoding: E2 80 AE (RTL override)
                //                 E2 80 AF (LTR override)
                //                 E2 80 AA-AD (bidi controls)
                //                 E2 81 A6-A9 (isolates)
                if (ch == 0xE2 && text[i + 1] != '\0' && text[i + 2] != '\0')
                {
                    auto b1 = static_cast<unsigned char>(text[i + 1]);
                    auto b2 = static_cast<unsigned char>(text[i + 2]);

                    // U+200B-200F (zero-width chars): E2 80 8B-8F
                    if (b1 == 0x80 && b2 >= 0x8B && b2 <= 0x8F)
                    {
                        i += 3;
                        continue;
                    }
                    // U+202A-202E (bidi controls): E2 80 AA-AE
                    if (b1 == 0x80 && b2 >= 0xAA && b2 <= 0xAF)
                    {
                        i += 3;
                        continue;
                    }
                    // U+2066-2069 (bidi isolates): E2 81 A6-A9
                    if (b1 == 0x81 && b2 >= 0xA6 && b2 <= 0xA9)
                    {
                        i += 3;
                        continue;
                    }
                    // U+FEFF (BOM/zero-width no-break): EF BB BF
                }
                if (ch == 0xEF)
                {
                    auto b1 = static_cast<unsigned char>(text[i + 1]);
                    auto b2 = static_cast<unsigned char>(text[i + 2]);
                    if (b1 == 0xBB && b2 == 0xBF)
                    {
                        i += 3;
                        continue;
                    }
                }

                // URL suppression: detect http://, https://, www.
                if ((text[i] == 'h' || text[i] == 'H') &&
                    (std::strncmp(text + i, "http://", 7) == 0 ||
                     std::strncmp(text + i, "https://", 8) == 0 ||
                     _strnicmp(text + i, "http://", 7) == 0 ||
                     _strnicmp(text + i, "https://", 8) == 0))
                {
                    out += "[link]";
                    charCount += 6;
                    // Skip until whitespace or end
                    while (text[i] != '\0' && text[i] != ' ' &&
                           text[i] != '\t' && text[i] != '\r' && text[i] != '\n')
                        ++i;
                    continue;
                }
                if ((text[i] == 'w' || text[i] == 'W') &&
                    _strnicmp(text + i, "www.", 4) == 0)
                {
                    out += "[link]";
                    charCount += 6;
                    while (text[i] != '\0' && text[i] != ' ' &&
                           text[i] != '\t' && text[i] != '\r' && text[i] != '\n')
                        ++i;
                    continue;
                }

                out.push_back(text[i]);
                ++charCount;
                ++i;
            }
        }

        // ===================================================================
        // 6. Security layer 5: System message spoof guard
        // ===================================================================
        //
        // Certain chat types are server-only — they should never carry a
        // player sender prefix.  If a message with one of these types has
        // a "PlayerName:" prefix, it's a potential spoof attempt and is
        // silently dropped.
        //
        // Protected types:
        //   42 = Warning (orange server warning)
        //   23–31 = Various server/system notices
        //   50 = Notice50 (grey system notice)
        //
        // NOT protected (confirmed in-game by testing):
        //   41 = Normal chat (player chat, despite ChatType.h naming it
        //        "ChatAdmin" — the enum name is misleading)
        // ===================================================================

        bool is_system_chat_type(int chatType)
        {
            return chatType == 42 ||
                   (chatType >= 23 && chatType <= 31) || chatType == 50;
        }

        // Returns true if a server-only chat type carries a player sender
        // prefix, indicating a spoof attempt.  Uses extract_sender_name()
        // on the RAW (pre-sanitization) text to check for the "Name:" pattern.
        bool is_spoofed_system_msg(int chatType, const char* text)
        {
            if (!is_system_chat_type(chatType))
                return false;

            auto sender = extract_sender_name(text);
            return !sender.empty();
        }

        // ===================================================================
        // 7. Security layer 6: Blacklist substring check
        // ===================================================================
        //
        // This is the actual filtering step for the mute list (section 2
        // holds the data, this section does the matching).
        //
        // How it works:
        //   - Lowercases the entire sanitized message text.
        //   - For each name in the mute list, searches for it as a
        //     whole-word substring (word-boundary matching).
        //   - Word boundary = start-of-string or non-alphanumeric char
        //     before the match, AND end-of-string or non-alphanumeric
        //     char after.
        //   - This prevents false positives: muting "Jo" won't match "John".
        //
        // Design decisions:
        //   - Works on the full sanitized text, not on a parsed sender name.
        //     This avoids dependence on message format (which varies by
        //     chat type and can be unreliable to parse).
        //   - Blocks both regular players and GMs indiscriminately —
        //     no special treatment for admin messages.
        //   - O(M × N) where M = muted names, N = text length.  Acceptable
        //     given the 200-name cap and 256-char text limit.
        // ===================================================================

        bool is_muted_by_content(const std::string& sanitized)
        {
            if (g_muteList.names.empty())
                return false;

            std::string lower = to_lower(sanitized);

            for (const auto& mutedName : g_muteList.names)
            {
                if (mutedName.empty())
                    continue;

                std::size_t pos = 0;
                while ((pos = lower.find(mutedName, pos)) != std::string::npos)
                {
                    // Check word boundary before the match
                    bool startOk = (pos == 0) ||
                        !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));

                    // Check word boundary after the match
                    auto endPos = pos + mutedName.size();
                    bool endOk = (endPos >= lower.size()) ||
                        !std::isalnum(static_cast<unsigned char>(lower[endPos]));

                    if (startOk && endOk)
                        return true;

                    ++pos;
                }
            }
            return false;
        }

        // ===================================================================
        // 8. Security layer 7: Render cap
        // ===================================================================
        //
        // Hard limit of wrapped lines per panel per frame.  This must be
        // high enough to include recent messages after long sessions.
        // Previously, a 64-line cap filled with historical upper-chat lines
        // and made new messages disappear once that threshold was reached.
        // draw_line_stack() still draws only the visible slice.
        // ===================================================================

        constexpr int kMaxRenderLinesPerPanel = kMaxParallelMsgs * kNativeMaxLinesPerMsg;

        // ===================================================================
        // Initialization
        // ===================================================================

        void load_state()
        {
            if (g_stateLoaded)
                return;

            g_stateLoaded = true;
            load_mute_list();
        }

        ImFont* get_parallel_font()
        {
            return g_parallelFont;
        }

        // Returns true if the chat type belongs in the upper (combat/system)
        // panel.  Everything else goes to the lower (social) panel.
        bool is_upper_chat_type(int chatType)
        {
            return (chatType >= 15 && chatType <= 33) || chatType == 50;
        }

        // ===================================================================
        // 9. Ring buffer and security pipeline (push_msg)
        // ===================================================================
        //
        // The ring buffer is a fixed-size circular array of 512 messages.
        // Each slot has parallel arrays for flags and duplicate counts,
        // used at render time for annotation.
        //
        // push_msg_raw() is the low-level insertion point (no filtering).
        // push_msg()     is the high-level entry point that runs the full
        //                security pipeline before inserting.
        //
        // Pipeline order in push_msg():
        //   1. sanitize_message_text()   → clean text, strip URLs/unicode
        //   2. is_spoofed_system_msg()   → drop fake system messages
        //   3. is_muted_by_content()     → drop messages from muted players
        //   4. check_rate_limit()        → suppress spam floods
        //   5. check_duplicate()         → collapse consecutive duplicates
        //   6. push_msg_raw()            → insert into ring buffer
        //
        // A message that fails ANY check is silently dropped — no error
        // feedback is shown to the user (by design, to not reward spammers).
        // ===================================================================

        // Per-message flags — stored in g_msgFlags[], parallel to g_ring[].
        constexpr unsigned char kMsgFlagNone = 0;
        constexpr unsigned char kMsgFlagDuplicate = 2;    // collapsed consecutive dup

        unsigned char g_msgFlags[kMaxParallelMsgs]{};     // flags parallel to g_ring
        int g_msgDupeCount[kMaxParallelMsgs]{};           // "(xN)" count for duplicates

        // Low-level ring insertion.  No security checks — caller is responsible.
        // Used by push_msg() after pipeline passes, and by mute_player()/
        // unmute_player() to inject "[System]" feedback directly.
        void push_msg_raw(int chatType, const char* text, unsigned char flags, int dupeCount)
        {
            auto& msg = g_ring[g_head];
            msg.chatType = chatType;
            g_msgFlags[g_head] = flags;
            g_msgDupeCount[g_head] = dupeCount;

            if (text && text[0])
                strncpy_s(msg.text, text, _TRUNCATE);
            else
                msg.text[0] = '\0';

            g_head = (g_head + 1) % kMaxParallelMsgs;
            if (g_count < kMaxParallelMsgs)
                ++g_count;
        }

        // ===================================================================
        // Security layer 8: Per-sender flood guard (public channels, anti-bot)
        // ===================================================================
        //
        // The rate-limit/duplicate layers key on message TEXT, so they only
        // catch a player repeating the SAME line.  This layer catches a single
        // sender flooding the public channels with DIFFERENT messages.
        //
        // Public channels only (Normal 34/41, Trade 38, Shout 39, Area 49) —
        // group channels (party/whisper/guild) are exempt by design, and the
        // local player is never throttled against themselves.
        //
        // Detection: a sliding window — more than kFloodThreshold messages in
        // kFloodWindowMs is one "offense".  Penalties escalate per offense so a
        // bot cannot just spam in waves between fixed cooldowns:
        //   1st offense -> 15 s silence,  2nd -> 1 min,  3rd -> 5 min,
        //   4th -> permanent auto-mute (added to the persistent CONFIG.ini
        //   blacklist, after which the normal mute layer drops everything).
        // Escalation decays: if a sender goes kFloodOffenseDecayMs without a
        // new offense, the counter resets, so an occasional fast-typing human
        // never climbs the ladder — only sustained bot behavior does.
        // Suppression is silent (no feedback to the spammer); only the
        // permanent auto-mute emits one notice so the user knows why.

        constexpr DWORD kFloodWindowMs = 5000;            // sliding window
        constexpr int   kFloodThreshold = 6;              // > this in window = offense
        constexpr DWORD kFloodOffenseDecayMs = 600000;    // 10 min calm resets escalation
        constexpr int   kFloodAutoMuteOffense = 4;        // 4th offense -> permanent mute
        constexpr DWORD kFloodCooldownsMs[] = { 15000, 60000, 300000 }; // 15 s, 1 min, 5 min

        struct SenderFloodInfo
        {
            DWORD timestamps[kFloodThreshold + 1]{}; // ring of recent message ticks
            int head = 0;
            int count = 0;
            int offenseCount = 0;       // escalation level
            DWORD mutedUntil = 0;       // active temporary-suppression expiry
            DWORD lastOffenseTick = 0;  // for escalation decay
            DWORD lastSeenTick = 0;     // for stale-entry cleanup
        };

        std::unordered_map<std::string, SenderFloodInfo> g_senderFlood;
        DWORD g_lastSenderFloodCleanup = 0;

        bool is_public_flood_channel(int chatType)
        {
            // Normal (34 typed / 41 in-game), Trade (38), Shout (39),
            // Area (49 in-game).  See ChatType.h note: enum names mislead.
            return chatType == 34 || chatType == 38 || chatType == 39
                || chatType == 41 || chatType == 49;
        }

        bool is_local_player(const std::string& lowerName)
        {
            if (!g_pPlayerData || g_pPlayerData->charId == 0)
                return false;
            auto* myName = g_pPlayerData->charName.data();
            if (!myName || myName[0] == '\0')
                return false;
            return lowerName == to_lower(myName);
        }

        void prune_sender_flood(DWORD now)
        {
            if (now - g_lastSenderFloodCleanup < kRateLimitCleanupInterval)
                return;
            g_lastSenderFloodCleanup = now;

            for (auto it = g_senderFlood.begin(); it != g_senderFlood.end();)
            {
                bool muted = it->second.mutedUntil != 0
                    && static_cast<int32_t>(now - it->second.mutedUntil) < 0;
                if (!muted && now - it->second.lastSeenTick > kFloodOffenseDecayMs)
                    it = g_senderFlood.erase(it);
                else
                    ++it;
            }
        }

        void auto_mute_flooder(const std::string& lowerName, const std::string& displayName)
        {
            if (g_muteList.names.count(lowerName))
                return;
            if (g_muteList.names.size() >= kMaxMutedPlayers)
                return;

            g_muteList.names.insert(lowerName);
            save_mute_entry(lowerName, true);

            char buf[128]{};
            std::snprintf(buf, sizeof(buf), "[Anti-spam] %s has been muted for flooding.",
                          displayName.empty() ? lowerName.c_str() : displayName.c_str());
            push_msg_raw(24, buf, kMsgFlagNone, 0);
        }

        // Returns true if this message should be suppressed (sender is flooding
        // or currently serving an escalation cooldown).
        bool check_sender_flood(const char* text, int chatType)
        {
            if (!is_public_flood_channel(chatType))
                return false;

            auto sender = extract_sender_name(text);
            if (sender.empty() || is_local_player(sender))
                return false;

            auto now = GetTickCount();
            prune_sender_flood(now);

            auto& info = g_senderFlood[sender];
            info.lastSeenTick = now;

            // Still inside an active suppression window — drop silently.
            if (info.mutedUntil != 0 && static_cast<int32_t>(now - info.mutedUntil) < 0)
                return true;

            // Record this message in the sliding-window ring.
            info.timestamps[info.head] = now;
            info.head = (info.head + 1) % (kFloodThreshold + 1);
            if (info.count < kFloodThreshold + 1)
                ++info.count;

            int recent = 0;
            for (int i = 0; i < info.count; ++i)
                if (now - info.timestamps[i] < kFloodWindowMs)
                    ++recent;

            if (recent <= kFloodThreshold)
                return false;   // within the allowed burst

            // Flood detected.  Forgive prior offenses after a long calm gap so
            // escalation only stacks for sustained (bot-like) flooding.
            if (info.lastOffenseTick != 0 && now - info.lastOffenseTick > kFloodOffenseDecayMs)
                info.offenseCount = 0;
            info.lastOffenseTick = now;
            ++info.offenseCount;

            // Reset the window; the cooldown / mute is the active gate now.
            info.count = 0;
            info.head = 0;

            if (info.offenseCount >= kFloodAutoMuteOffense)
            {
                auto_mute_flooder(sender, extract_sender_name(text, false));
                return true;
            }

            auto tier = std::min(info.offenseCount,
                                 static_cast<int>(std::size(kFloodCooldownsMs))) - 1;
            info.mutedUntil = now + kFloodCooldownsMs[tier];
            return true;
        }

        // ---------------------------------------------------------------
        // push_msg — Full security pipeline entry point
        // ---------------------------------------------------------------
        // Called for lower-panel (social/player) chat messages only.
        // Upper-panel messages (combat/system) bypass this entirely — see
        // record_chat_type().  Runs all security layers in sequence.  If
        // the message survives all checks, it's pushed to the ring buffer.
        void push_msg(int chatType, const char* text)
        {
            // Layer 4: Sanitize text (control chars, URLs, unicode, color codes)
            std::string sanitized;
            sanitize_message_text(text, sanitized);
            if (sanitized.empty())
                return;

            // Layer 5: System message spoof protection — drop system-type
            // messages that carry a player sender prefix.
            if (is_spoofed_system_msg(chatType, text))
                return;

            // Layer 6: Blacklist check — searches for any muted player name
            // anywhere in the sanitized text as a whole word.  Blocks both
            // regular players and GMs indiscriminately.
            if (is_muted_by_content(sanitized))
                return;

            // The local player's own messages are never throttled — you should
            // always see exactly what you said (and we must never auto-mute
            // yourself).  Skips the flood guard, rate limit and duplicate
            // collapse for self; other senders still pass through all of them.
            bool self = is_local_player(extract_sender_name(text));

            // Layer 8: Per-sender flood guard — public channels only.  Catches
            // a single sender flooding with DIFFERENT messages; escalates to a
            // permanent auto-mute for sustained bot-like flooding.
            if (!self && check_sender_flood(text, chatType))
                return;

            // Layer 2: Rate limit — keyed on sanitized text, public channels only.
            // Suppresses the same text appearing 5+ times in 10 seconds.
            if (!self && check_rate_limit(sanitized, chatType))
                return;

            // Layer 3: Duplicate detection — keyed on sanitized text.
            // If ≥3 consecutive, update the existing ring entry's "(xN)"
            // counter instead of adding a new line.
            int dupeCount = self ? 1 : check_duplicate(sanitized);
            if (dupeCount >= kDuplicateThreshold)
            {
                // Scan backwards in the ring to find the previous matching
                // entry and update its duplicate count in-place.
                int prev = (g_head - 1 + kMaxParallelMsgs) % kMaxParallelMsgs;
                for (int scan = 0; scan < std::min(g_count, 20); ++scan)
                {
                    int idx = (prev - scan + kMaxParallelMsgs) % kMaxParallelMsgs;
                    if (sanitized == g_ring[idx].text)
                    {
                        g_msgFlags[idx] = kMsgFlagDuplicate;
                        g_msgDupeCount[idx] = dupeCount;
                        return;
                    }
                }
                // Previous entry not found (wrapped out) — push with flag.
                push_msg_raw(chatType, sanitized.c_str(),
                             kMsgFlagDuplicate, dupeCount);
                return;
            }

            // All checks passed — push clean message.
            push_msg_raw(chatType, sanitized.c_str(), kMsgFlagNone, 0);
        }

        // ===================================================================
        // 10. Native chat metrics, rendering, and color mapping
        // ===================================================================
        //
        // The overlay renders via ImGui::GetForegroundDrawList(), drawing
        // directly on top of the native Shaiya chat area.  To align
        // perfectly, we read the native chat panel object's geometry each
        // frame (position, size, scroll offsets, resize bytes).
        //
        // Panel layout:
        //   - Upper panel: combat/system types (15–33, 50) — damage, XP, etc.
        //   - Lower panel: social chat types (34+) — Normal, Trade, Whisper, etc.
        //
        // Text is rendered bottom-up: the most recent message appears at
        // the bottom of each panel, scrolling older messages upward.
        // Native scroll offsets (float values at known panel offsets) are
        // read to keep the overlay in sync with the player's scroll state.
        //
        // Each message is wrapped to fit the panel width using
        // append_wrapped_lines(), which counts visual columns (including
        // emoji tokens as 2 columns) and splits at the configured
        // chars-per-line limit.  Max 3 wrapped lines per message.
        //
        // Text rendering uses draw_text_line() which handles:
        //   - Inline emoji rendering (texture quads from the emoji atlas)
        //   - A light native-style text shadow
        //   - Native or custom per-chat-type color mapping
        // ===================================================================

        // --- Color mapping: chatType → RGBA ---

        ImVec4 native_rgb(unsigned char r, unsigned char g, unsigned char b)
        {
            constexpr float inv255 = 1.0f / 255.0f;
            return ImVec4(r * inv255, g * inv255, b * inv255, 1.0f);
        }

        ImVec4 default_color(int chatType)
        {
            // Exact native RGB table from game.exe:004207F0.
            if (chatType == 15) return native_rgb(0xFF, 0x9B, 0x32);
            if (chatType == 16 || chatType == 17 || chatType == 42 || chatType == 45)
                return native_rgb(0xFF, 0x00, 0x00);
            if (chatType == 18) return native_rgb(0xFF, 0xCC, 0x00);
            if (chatType == 19) return native_rgb(0x33, 0xFF, 0x33);
            if (chatType == 20 || chatType == 47) return native_rgb(0xFF, 0x00, 0xFF);
            if (chatType == 21) return native_rgb(0x99, 0xFF, 0xFF);
            if (chatType == 22) return native_rgb(0xCC, 0xFF, 0x33);
            if (chatType == 24) return native_rgb(0xFF, 0xFF, 0xFF);
            if (chatType == 40) return native_rgb(0xFF, 0xFF, 0x00);
            if (chatType == 31) return native_rgb(0xCC, 0xCC, 0xCC);
            if (chatType == 34) return native_rgb(0xD5, 0xD4, 0xD3);
            if (chatType == 35) return native_rgb(0xA8, 0xF3, 0xA0);
            if (chatType == 36) return native_rgb(0xF3, 0xA0, 0xA0);
            if (chatType == 37) return native_rgb(0xEE, 0xA4, 0xED);
            if (chatType == 38) return native_rgb(0xF3, 0xDE, 0xA0);
            if (chatType == 39) return native_rgb(0xFF, 0x30, 0x00);
            if (chatType == 48) return native_rgb(0xFF, 0x80, 0xFF);
            if (chatType == 49) return native_rgb(0x78, 0x75, 0xE6);
            if (chatType == 50) return native_rgb(0xFF, 0x03, 0xDA);

            return native_rgb(0xFF, 0xFF, 0xFF);
        }

        ImVec4 color_for_type(int chatType)
        {
            if (chatType >= 0 && chatType < static_cast<int>(std::size(g_colorCustom)) && g_colorCustom[chatType])
                return g_color[chatType];

            return default_color(chatType);
        }

        // Read the native chat panel's geometry from the hooked 0x75E0-byte
        // object.  This is called every frame to stay in sync with panel
        // resizes and repositions.  The panel pointer is captured at runtime
        // by the ASM hook on function 0x47D1F0 (see imgui_layer_asm.cpp).
        bool read_native_chat_metrics(NativeChatMetrics& out)
        {
            out = {};
            auto panelPtr = g_chatPanelPtr;
            if (!panelPtr)
                return false;

            auto fields = reinterpret_cast<const unsigned*>(panelPtr);
            out.baseX = static_cast<float>(fields[1]);
            out.baseY = static_cast<float>(fields[2]);
            out.sz0 = *reinterpret_cast<const unsigned char*>(panelPtr + 0x3B4);
            out.sz1 = *reinterpret_cast<const unsigned char*>(panelPtr + 0x3CC);

            out.textX = out.baseX + kNativeChatTextOffsetX;
            out.textW = kNativeChatTextWidth;
            out.upperVisibleLines = out.sz0 + kNativeUpperExtraLines;
            out.lowerVisibleLines = out.sz1 + kNativeLowerExtraLines;
            out.upperH = static_cast<float>(out.upperVisibleLines) * kNativeChatLineH;
            out.lowerH = static_cast<float>(out.lowerVisibleLines) * kNativeChatLineH;
            out.upperFirstLineY = out.baseY + kNativeUpperFirstLineYOffset
                + static_cast<float>(out.sz0) * kNativeChatLineH;
            out.lowerFirstLineY = out.baseY + kNativeLowerFirstLineYOffset
                + out.upperH + static_cast<float>(out.sz1) * kNativeChatLineH;
            out.valid = true;
            return true;
        }

        // Native CChat dropdown-open flags, reversed from game.exe:
        //   CChat::Render  @0x47DB70 gates the channel list on byte +0x75B4
        //     (draws 6 channel rows over the bottom-left of the lower chat),
        //     and a second item dropdown on byte +0x75B5.
        //   CChat::InputProc @0x487590 toggles those bytes on click.
        // Our chat overlay draws after all native UI (in the Present hook), so
        // it would otherwise sit on top of these popups.  While either is open
        // we skip the lower panel so the native dropdown stays readable.
        constexpr uintptr_t kNativeChannelDropdownOpenOffset = 0x75B4;
        constexpr uintptr_t kNativeItemDropdownOpenOffset = 0x75B5;

        bool native_chat_dropdown_open()
        {
            auto panelPtr = g_chatPanelPtr;
            if (!panelPtr)
                return false;

            return *reinterpret_cast<const unsigned char*>(panelPtr + kNativeChannelDropdownOpenOffset) != 0
                || *reinterpret_cast<const unsigned char*>(panelPtr + kNativeItemDropdownOpenOffset) != 0;
        }

        // Native chat "Filter Option" window state.  Reversed from game.exe
        // CChat::Render @0x47DB70, which calls the filter predicate
        // FUN_00420750(chatType) per line and skips the line when it returns 0.
        // The predicate reads 8 int flags (1 = show, 0 = hide) from the chat
        // config object pointed to by *(void**)0x7C06F8:
        //   +0x20 Damage   +0x24 Acquire  +0x28 Death   +0x2C Notice
        //   +0x30 Common   +0x34 Party    +0x38 Guild   +0x3C Trade
        // We read the same live object so toggling a checkbox immediately
        // shows/hides messages in our overlay, matching the native behavior.
        // Types outside these categories are always shown (native default = 1).
        constexpr uintptr_t kNativeChatFilterPtrAddr = 0x7C06F8;

        bool chat_type_visible(int chatType)
        {
            auto base = *reinterpret_cast<const uintptr_t*>(kNativeChatFilterPtrAddr);
            if (!base)
                return true;

            uintptr_t flagOffset;
            switch (chatType)
            {
            case 15: case 16: case 20:                       flagOffset = 0x20; break; // Damage
            case 18: case 19: case 21: case 22: case 31:     flagOffset = 0x24; break; // Acquire
            case 17:                                         flagOffset = 0x28; break; // Death
            case 23: case 24: case 25: case 26:
            case 27: case 28: case 29: case 30:              flagOffset = 0x2C; break; // Notice
            case 34: case 48:                                flagOffset = 0x30; break; // Common
            case 35:                                         flagOffset = 0x34; break; // Party
            case 37:                                         flagOffset = 0x38; break; // Guild
            case 38: case 49:                                flagOffset = 0x3C; break; // Trade
            default:                                         return true;
            }

            return *reinterpret_cast<const int*>(base + flagOffset) != 0;
        }

        // Wrap a single message into 1–3 display lines, appending them to
        // the `lines` array.  Emoji tokens (":name:") count as 2 visual
        // columns.  Returns the new lineCount after appending.
        int append_wrapped_lines(
            NativeChatLine* lines,
            int lineCount,
            int maxLines,
            int chatType,
            const char* text,
            int charsPerLine)
        {
            if (!lines || lineCount >= maxLines)
                return lineCount;

            const char* msg = text ? text : "";
            int len = static_cast<int>(std::strlen(msg));
            charsPerLine = std::clamp(charsPerLine, 20, 120);

            int pos = 0;
            for (int line = 0; line < kNativeMaxLinesPerMsg && lineCount < maxLines; ++line)
            {
                int end = pos;
                int visualColumns = 0;

                while (end < len)
                {
                    int advanceBytes = 1;
                    int advanceColumns = 1;

                    auto* emoji = (msg[end] == ':') ? find_emoji_by_token(msg + end) : nullptr;
                    if (emoji)
                    {
                        advanceBytes = static_cast<int>(emoji->token.size());
                        advanceColumns = is_visual_token_enabled(emoji->kind) ? 2 : 0;
                    }
                    else
                    {
                        // Advance by a whole UTF-8 codepoint so a wrap boundary
                        // never splits a multibyte character (a split sequence
                        // renders as a broken '?'/box glyph).  3-4 byte
                        // sequences (CJK, kana, hangul) are double-width.
                        unsigned char lead = static_cast<unsigned char>(msg[end]);
                        if (lead >= 0xF0)      advanceBytes = 4;
                        else if (lead >= 0xE0) advanceBytes = 3;
                        else if (lead >= 0xC0) advanceBytes = 2;
                        else                   advanceBytes = 1;
                        if (end + advanceBytes > len)
                            advanceBytes = len - end;
                        advanceColumns = (advanceBytes >= 3) ? 2 : 1;
                    }

                    if (visualColumns > 0 && visualColumns + advanceColumns > charsPerLine)
                        break;

                    end += advanceBytes;
                    visualColumns += advanceColumns;

                    if (visualColumns >= charsPerLine)
                        break;
                }

                if (end <= pos)
                    end = std::min(pos + 1, len);

                int n = end - pos;
                if (n >= static_cast<int>(sizeof(lines[lineCount].text)))
                    n = sizeof(lines[lineCount].text) - 1;
                if (n > 0)
                    std::memcpy(lines[lineCount].text, msg + pos, n);
                lines[lineCount].text[n] = '\0';
                lines[lineCount].chatType = chatType;
                ++lineCount;
                pos = end;

                if (pos >= len)
                    break;
            }

            return lineCount;
        }

        // Read the native scroll position (float) from the chat panel object.
        // Upper panel offset: +0x16EC, lower panel offset: +0x294C.
        int read_native_chat_scroll_offset(bool upper)
        {
            auto panelPtr = g_chatPanelPtr;
            if (!panelPtr)
                return 0;

            auto offset = upper ? kNativeUpperScrollValueOffset : kNativeLowerScrollValueOffset;
            float value = *reinterpret_cast<const float*>(panelPtr + offset);
            if (!std::isfinite(value) || value <= 0.0f)
                return 0;

            return std::clamp(static_cast<int>(value), 0, 512);
        }

        // Draw a single text line with inline emoji support and a native-style
        // shadow.  Splits the text into runs of plain text and emoji tokens,
        // rendering plain text via D3DX and emojis as ImGui texture quads.
        // outline=true  → 4-direction outline (lower/social panel)
        // outline=false → single drop shadow at (x+1, y+1) (upper/combat panel)
        // Chat text is queued here as D3DX runs and drawn after ImGui by
        // flush_d3dx_text() with the game's own ID3DXFont (GDI rasterizer), so it
        // looks pixel-identical to native chat and renders the game's codepage
        // directly (ñ etc. need no transcode). Emoji/GIF tokens are drawn inline
        // as ImGui image quads. Width is measured with the ImGui font only.
        struct D3DXTextRun
        {
            float x, y;
            D3DCOLOR color;
            char text[256];
        };
        constexpr int kMaxD3DXTextRuns = 2048;
        D3DXTextRun g_d3dxRuns[kMaxD3DXTextRuns]{};
        int g_d3dxRunCount = 0;

        void draw_text_line(
            ImDrawList* drawList,
            ImFont* font,
            float fontSize,
            float x,
            float y,
            ImU32 color,
            ImU32 shadow,
            const char* text,
            bool outline = true)
        {
            if (!drawList || !text || text[0] == '\0')
                return;

            float cursorX = x;
            float emojiSize = fontSize;
            auto len = std::strlen(text);
            std::size_t idx = 0;

            while (idx < len)
            {
                auto* emoji = (text[idx] == ':') ? find_emoji_by_token(text + idx) : nullptr;
                if (emoji)
                {
                    if (is_visual_token_enabled(emoji->kind))
                    {
                        auto tex = get_emoji_texture(*emoji);
                        if (tex)
                        {
                            drawList->AddImage(
                                reinterpret_cast<ImTextureID>(tex),
                                ImVec2(cursorX, y),
                                ImVec2(cursorX + emojiSize, y + emojiSize));
                            cursorX += emojiSize;
                        }
                    }
                    idx += emoji->token.size();
                    continue;
                }

                auto runStart = idx;
                while (idx < len
                    && !(text[idx] == ':' && find_emoji_by_token(text + idx)))
                    ++idx;

                const char* p = text + runStart;
                const char* pEnd = text + idx;
                auto runLen = static_cast<std::size_t>(pEnd - p);

                // Measure with the ImGui font for cursor advance / emoji spacing.
                float runW = font
                    ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, pEnd).x
                    : ImGui::CalcTextSize(p, pEnd).x;

                // Queue the text run for D3DX rendering after the ImGui flush.
                // outline=true:  4-direction outline (N/S/E/W) + centre colour.
                // outline=false: single drop shadow at (+1,+1) + centre colour.
                int shadowDraws = outline ? 4 : 1;
                if (runLen > 0 && runLen < sizeof(D3DXTextRun::text) &&
                    g_d3dxRunCount + shadowDraws + 1 <= kMaxD3DXTextRuns)
                {
                    auto enqueue = [&](float ox, float oy, D3DCOLOR c) {
                        auto& run = g_d3dxRuns[g_d3dxRunCount++];
                        run.x = cursorX + ox;
                        run.y = oy;
                        run.color = c;
                        std::memcpy(run.text, p, runLen);
                        run.text[runLen] = '\0';
                    };

                    D3DCOLOR shadowD3D = (shadow & 0xFF00FF00) |
                        ((shadow >> 16) & 0xFF) |
                        ((shadow & 0xFF) << 16);
                    D3DCOLOR colorD3D = (color & 0xFF00FF00) |
                        ((color >> 16) & 0xFF) |
                        ((color & 0xFF) << 16);

                    if (outline)
                    {
                        enqueue(0.0f, y - 1.0f, shadowD3D);
                        enqueue(0.0f, y + 1.0f, shadowD3D);
                        enqueue(-1.0f, y, shadowD3D);
                        enqueue(1.0f, y, shadowD3D);
                    }
                    else
                    {
                        enqueue(1.0f, y + 1.0f, shadowD3D);
                    }
                    enqueue(0.0f, y, colorD3D);
                }

                cursorX += runW;
            }
        }

        // Draw a stack of wrapped lines for one panel (upper or lower).
        // Lines are drawn bottom-up from firstLineY, respecting the native
        // scroll offset and visible line count.
        // outline=true  → 4-direction outline shadow (lower/social panel)
        // outline=false → single drop shadow (upper/combat panel)
        void draw_line_stack(
            ImDrawList* drawList,
            float x,
            float firstLineY,
            int visibleLines,
            const NativeChatLine* lines,
            int lineCount,
            int scrollOffset,
            bool outline = true)
        {
            if (!drawList || !lines || lineCount <= 0 || visibleLines <= 0)
                return;

            auto* font = get_parallel_font();
            int maxScroll = std::max(0, lineCount - 1);
            scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
            int end = lineCount - scrollOffset;
            int first = std::max(0, end - visibleLines);
            int shown = end - first;
            ImU32 shadow = IM_COL32(0, 0, 0, 255);

            for (int i = 0; i < shown; ++i)
            {
                int selected = first + (shown - 1 - i);
                float y = firstLineY - static_cast<float>(i) * kNativeChatLineH;
                auto color = color_for_type(lines[selected].chatType);
                draw_text_line(
                    drawList,
                    font,
                    kNativeParallelFontSize,
                    x,
                    y,
                    ImGui::ColorConvertFloat4ToU32(color),
                    shadow,
                    lines[selected].text,
                    outline);
            }
        }

        // GM debug panel widget: per-chat-type color editor with quick-select
        // buttons.  Allows overriding the default color for any chat type.
        void draw_color_controls()
        {
            static int selectedType = 34;
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputInt("Chat Type", &selectedType, 1, 5);
            selectedType = std::clamp(selectedType, 0, 99);

            auto defaultColorValue = default_color(selectedType);
            if (!g_colorCustom[selectedType])
                g_color[selectedType] = defaultColorValue;

            ImGui::SameLine();
            if (ImGui::SmallButton("Use current##parallel_color"))
                g_colorCustom[selectedType] = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##parallel_color"))
            {
                g_colorCustom[selectedType] = false;
                g_color[selectedType] = defaultColorValue;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset all##parallel_colors"))
            {
                for (auto& custom : g_colorCustom)
                    custom = false;
            }

            bool custom = g_colorCustom[selectedType];
            if (ImGui::Checkbox("Custom color", &custom))
            {
                g_colorCustom[selectedType] = custom;
                if (custom)
                    g_color[selectedType] = defaultColorValue;
            }

            ImGui::SameLine();
            ImGui::ColorButton("Default##parallel_color_default", defaultColorValue);

            ImGui::BeginDisabled(!g_colorCustom[selectedType]);
            ImGui::SetNextItemWidth(240.0f);
            ImGui::ColorEdit4(
                "Color",
                reinterpret_cast<float*>(&g_color[selectedType]),
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::EndDisabled();

            ImGui::TextDisabled("Quick select:");
            const int commonTypes[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50 };
            for (int i = 0; i < static_cast<int>(std::size(commonTypes)); ++i)
            {
                if (i > 0)
                    ImGui::SameLine();

                int chatType = commonTypes[i];
                auto col = color_for_type(chatType);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x, col.y, col.z, 0.35f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x, col.y, col.z, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x, col.y, col.z, 0.75f));
                char label[16]{};
                std::snprintf(label, sizeof(label), "%d##pct", chatType);
                if (ImGui::SmallButton(label))
                    selectedType = chatType;
                ImGui::PopStyleColor(3);

                if ((i + 1) % 9 == 0)
                    ImGui::NewLine();
            }
        }
    }

    // ===================================================================
    // 11. Public API
    // ===================================================================
    //
    // record_chat_type() — entry point from the message hook.  Called by
    //   prepare_chat_text_for_emojis() for every incoming chat message.
    //   Skips screen-notice types (handled natively).  Upper panel messages
    //   (combat/system, types 15–33, 50) are sanitized but bypass the
    //   security pipeline — they come from the server, not from players.
    //   Only lower panel messages (social chat, types 34+) go through
    //   the full pipeline (spoof guard, blacklist, rate limit, duplicates).
    //
    // hide_native_chat_visuals() — returns true when the overlay is active.
    //   The caller (native chat render hook) uses this to suppress the
    //   native text drawing, leaving only our ImGui overlay visible.
    //
    // render_ingame_chat() — called once per ImGui frame.  Reads native
    //   panel metrics, builds wrapped line arrays for upper/lower panels,
    //   and draws them via the foreground draw list.
    //
    // render_options() — GM debug panel controls for wrap widths and colors.
    //
    // mute_player() / unmute_player() — called from command.cpp when the
    //   user types /mute or /unmute.  Modifies the blacklist and persists
    //   the change to CONFIG.ini.  Feedback is shown as a yellow "[System]"
    //   message injected directly into the ring buffer (type 24).
    // ===================================================================

    void record_chat_type(int chatType, const char* text)
    {
        load_state();
        // Text stays in the game's native codepage — flush_d3dx_text() renders
        // it with the native font via DrawTextA, which handles the codepage.
        // Upper panel messages (combat/system, types 15–33, 50) are from the
        // server, not from players — skip the security pipeline entirely.
        // Only lower panel messages (social chat, types 34+) are filtered.
        if (is_upper_chat_type(chatType))
        {
            std::string sanitized;
            sanitize_message_text(text, sanitized);
            if (!sanitized.empty())
                push_msg_raw(chatType, sanitized.c_str(), kMsgFlagNone, 0);
        }
        else
        {
            push_msg(chatType, text);
        }
    }

    // Load the ImGui chat font into the atlas. Must run at init, BEFORE the
    // first frame builds the atlas — a font added lazily afterwards never gets
    // a glyph texture. Merges Arial (Latin + Cyrillic + Vietnamese) with a CJK
    // face (YaHei/SimSun). g_parallelFont is now used for the chat BUBBLES and
    // for text-width MEASUREMENT in draw_text_line; the lower chat box itself
    // renders with the native ID3DXFont (see flush_d3dx_text).
    void load_chat_font()
    {
        if (g_parallelFontLoaded)
            return;
        g_parallelFontLoaded = true;

        auto& io = ImGui::GetIO();
        io.Fonts->AddFontDefault();

        char windowsDir[MAX_PATH]{};
        if (!GetWindowsDirectoryA(windowsDir, MAX_PATH))
            return;
        std::string fonts(windowsDir);
        if (!fonts.empty() && fonts.back() != '\\')
            fonts += "\\";
        fonts += "Fonts\\";

        // ImGui keeps the glyph-ranges pointer until the atlas is built, so it
        // must outlive this function — keep it static.
        static ImVector<ImWchar> s_ranges;
        if (s_ranges.empty())
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
            builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
            builder.BuildRanges(&s_ranges);
        }

        constexpr float kSize = 16.0f;
        std::string arial = fonts + "arial.ttf";
        if (GetFileAttributesA(arial.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            ImFontConfig cfg{};
            std::snprintf(cfg.Name, sizeof(cfg.Name), "ChatUnicode");
            g_parallelFont = io.Fonts->AddFontFromFileTTF(
                arial.c_str(), kSize, &cfg, s_ranges.Data);

            // Merge the first available face for each CJK script onto the base
            // font, so Chinese / Japanese / Korean all render. Each list is a
            // fallback chain of standard Windows font files. Korean carries the
            // full Hangul block (~11k glyphs) — the bulk of the atlas.
            auto mergeFace = [&](std::initializer_list<const char*> files, const ImWchar* ranges) {
                for (auto* file : files)
                {
                    std::string path = fonts + file;
                    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                        continue;
                    ImFontConfig mergeCfg{};
                    mergeCfg.MergeMode = true;
                    // 1x oversampling for CJK: the default 3x triples the atlas
                    // width per glyph, which with thousands of CJK + Hangul
                    // glyphs overflows the atlas and silently drops glyphs (they
                    // then render as the '?' fallback). 1x is the recommended
                    // setting for large CJK fonts and keeps every glyph.
                    mergeCfg.OversampleH = 1;
                    mergeCfg.OversampleV = 1;
                    mergeCfg.PixelSnapH = true;
                    io.Fonts->AddFontFromFileTTF(path.c_str(), kSize, &mergeCfg, ranges);
                    return true;
                }
                return false;
            };

            // Chinese (Simplified, common ~2.5k — covers everyday chat).
            mergeFace({ "msyh.ttc", "msyhl.ttc", "simsun.ttc", "simhei.ttf" },
                      io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            // Japanese (kana + common kanji).
            mergeFace({ "meiryo.ttc", "YuGothM.ttc", "yugothic.ttf", "msgothic.ttc" },
                      io.Fonts->GetGlyphRangesJapanese());
            // Korean (full Hangul syllables).
            mergeFace({ "malgun.ttf", "malgunsl.ttf", "gulim.ttc", "batang.ttc" },
                      io.Fonts->GetGlyphRangesKorean());
        }

        // Fall back to the default font so the chat path never sees null.
        if (!g_parallelFont && !io.Fonts->Fonts.empty())
            g_parallelFont = io.Fonts->Fonts[0];
    }

    bool hide_native_chat_visuals()
    {
        load_state();
        return true;
    }

    // Wipe the entire parallel chat history.  Called on relog (return to
    // character select) so a new session never inherits the previous
    // character's messages.  Resetting g_head/g_count is enough to stop the
    // old entries from rendering; we also clear the parallel arrays and the
    // rate-limit tracker so no stale flags or spam counters carry over.
    void clear_messages()
    {
        for (auto& msg : g_ring)
        {
            msg.chatType = 0;
            msg.text[0] = '\0';
        }
        for (auto& flag : g_msgFlags)
            flag = kMsgFlagNone;
        for (auto& dupe : g_msgDupeCount)
            dupe = 0;

        g_head = 0;
        g_count = 0;
        g_rateLimits.clear();
        g_senderFlood.clear();
    }

    // Draw the queued chat text runs with the game's own native chat font
    // (D3DX / GDI rasterizer) for pixel-identical native appearance. DrawTextA
    // renders the game's codepage directly, so non-ASCII (ñ, etc.) needs no
    // transcode. Called once per frame, after ImGui builds its draw data.
    void flush_d3dx_text()
    {
        if (g_d3dxRunCount <= 0)
            return;

        auto* font = g_var->camera.d3dxFont0;
        if (!font) font = g_var->camera.d3dxFont1;
        if (!font) font = g_var->camera.d3dxFont2;
        if (!font) font = g_var->camera.d3dxFont3;
        if (!font)
            return;

        for (int i = 0; i < g_d3dxRunCount; ++i)
        {
            const auto& run = g_d3dxRuns[i];
            RECT rect;
            rect.left   = static_cast<LONG>(run.x);
            rect.top    = static_cast<LONG>(run.y);
            rect.right  = rect.left + 600;
            rect.bottom = rect.top + 24;
            font->DrawTextA(nullptr, run.text, -1, &rect, DT_NOCLIP, run.color);
        }

        g_d3dxRunCount = 0;
    }

    void render_ingame_chat()
    {
        load_state();
        g_d3dxRunCount = 0;   // reset the D3DX text queue each frame
        if (!is_game_scene_stable())
            return;

        NativeChatMetrics metrics;
        if (!read_native_chat_metrics(metrics))
            return;

        static NativeChatLine upperLines[kMaxRenderLinesPerPanel]{};
        static NativeChatLine lowerLines[kMaxRenderLinesPerPanel]{};
        int upperCount = 0;
        int lowerCount = 0;

        int start = (g_count < kMaxParallelMsgs) ? 0 : g_head;
        for (int i = 0; i < g_count; ++i)
        {
            int idx = (start + i) % kMaxParallelMsgs;
            const auto& msg = g_ring[idx];

            // Respect the native "Filter Option" checkboxes (Damage/Acquire/
            // Death/Notice, Common/Party/Guild/Trade). Read live so toggling a
            // box hides/shows existing messages just like the native chat.
            if (!chat_type_visible(msg.chatType))
                continue;

            auto flag = g_msgFlags[idx];

            // Build display text, appending duplicate count if flagged
            char displayBuf[600]{};
            if (flag == kMsgFlagDuplicate && g_msgDupeCount[idx] >= kDuplicateThreshold)
                std::snprintf(displayBuf, sizeof(displayBuf),
                              "%s (x%d)", msg.text, g_msgDupeCount[idx]);
            else
                strncpy_s(displayBuf, msg.text, _TRUNCATE);

            if (is_upper_chat_type(msg.chatType))
            {
                if (upperCount < kMaxRenderLinesPerPanel)
                    upperCount = append_wrapped_lines(
                        upperLines,
                        upperCount,
                        std::min(static_cast<int>(std::size(upperLines)),
                                 kMaxRenderLinesPerPanel),
                        msg.chatType,
                        displayBuf,
                        g_upperCharsPerLine);
            }
            else
            {
                if (lowerCount < kMaxRenderLinesPerPanel)
                    lowerCount = append_wrapped_lines(
                        lowerLines,
                        lowerCount,
                        std::min(static_cast<int>(std::size(lowerLines)),
                                 kMaxRenderLinesPerPanel),
                        msg.chatType,
                        displayBuf,
                        g_lowerCharsPerLine);
            }
        }

        // Background draw list: chat renders over the game world but stays
        // behind ImGui windows, so overlay panels remain readable above it.
        auto* drawList = ImGui::GetBackgroundDrawList();
        draw_line_stack(
            drawList,
            metrics.textX,
            metrics.upperFirstLineY,
            metrics.upperVisibleLines,
            upperLines,
            upperCount,
            read_native_chat_scroll_offset(true),
            false);   // upper panel: single drop shadow
        // Skip the lower panel while a native chat dropdown (channel selector
        // or item list) is expanded — it covers the bottom-left of the lower
        // chat, and our overlay would otherwise draw on top of it.  The popup
        // is transient (only while picking a channel), so the upper combat
        // panel keeps rendering and the lower chat returns once it closes.
        if (!native_chat_dropdown_open())
            draw_line_stack(
                drawList,
                metrics.textX,
                metrics.lowerFirstLineY,
                metrics.lowerVisibleLines,
                lowerLines,
                lowerCount,
                read_native_chat_scroll_offset(false),
                true);    // lower panel: 4-direction outline
    }

    void render_options()
    {
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Upper wrap", &g_upperCharsPerLine);
        g_upperCharsPerLine = std::clamp(g_upperCharsPerLine, 20, 120);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Lower wrap", &g_lowerCharsPerLine);
        g_lowerCharsPerLine = std::clamp(g_lowerCharsPerLine, 20, 120);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##chat_wrap"))
        {
            g_upperCharsPerLine = kDefaultNativeUpperCharsPerLine;
            g_lowerCharsPerLine = kDefaultNativeLowerCharsPerLine;
        }

        ImGui::Separator();
        draw_color_controls();
    }

    // Formats a "[System] <name> <suffix>" line and pushes it into the chat
    // ring as system feedback (chat type 24).
    void push_mute_feedback(const char* name, const char* suffix)
    {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "[System] %s %s", name, suffix);
        push_msg_raw(24, buf, kMsgFlagNone, 0);
    }

    bool mute_player(const char* name)
    {
        if (!name || !name[0])
            return false;

        load_state();
        auto lower = to_lower(name);
        if (lower.empty() || lower.size() > 24)
            return false;

        if (g_muteList.names.size() >= kMaxMutedPlayers)
            return false;

        if (g_muteList.names.count(lower))
        {
            push_mute_feedback(name, "is already muted.");
            return true;
        }

        g_muteList.names.insert(lower);
        save_mute_entry(lower, true);

        push_mute_feedback(name, "has been muted.");
        return true;
    }

    bool unmute_player(const char* name)
    {
        if (!name || !name[0])
            return false;

        load_state();
        auto lower = to_lower(name);
        if (lower.empty())
            return false;

        auto it = g_muteList.names.find(lower);
        if (it == g_muteList.names.end())
        {
            push_mute_feedback(name, "is not muted.");
            return true;
        }

        g_muteList.names.erase(it);
        save_mute_entry(lower, false);

        push_mute_feedback(name, "has been unmuted.");
        return true;
    }

}
