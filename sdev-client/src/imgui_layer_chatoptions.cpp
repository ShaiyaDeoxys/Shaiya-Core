#include "include/imgui_layer_internal.h"
#include "include/config.h"

// ---------------------------------------------------------------------------
// Parallel chat window layout persistence (CONFIG.ini [CHATOPTIONS]).
// Font and text style are intentionally hardcoded.
// ---------------------------------------------------------------------------

namespace imgui_layer
{
    static constexpr const char* kSection = "CHATOPTIONS";

    static float read_chat_float(const char* key, float fallback)
    {
        char buf[32]{};
        char fallbackBuf[32]{};
        snprintf(fallbackBuf, sizeof(fallbackBuf), "%.3f", fallback);
        GetPrivateProfileStringA(kSection, key, fallbackBuf, buf, sizeof(buf),
            config::ini_path().c_str());
        return static_cast<float>(std::atof(buf));
    }

    static void write_chat_float(const char* key, float value)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", value);
        WritePrivateProfileStringA(kSection, key, buf,
            config::ini_path().c_str());
    }

    void load_chat_window_layout()
    {
        g_upperChatPosition.x = read_chat_float("UpperX", kChatNormX);
        g_upperChatPosition.y = read_chat_float("UpperY", kUpperNormY);
        g_upperChatSize.x = read_chat_float("UpperW", kChatNormW);
        g_upperChatSize.y = read_chat_float("UpperH", kUpperNormH);

        g_lowerChatPosition.x = read_chat_float("LowerX", kChatNormX);
        g_lowerChatPosition.y = read_chat_float("LowerY", kLowerNormY);
        g_lowerChatSize.x = read_chat_float("LowerW", kChatNormW);
        g_lowerChatSize.y = read_chat_float("LowerH", kLowerNormH);
    }

    void save_chat_window_layout()
    {
        write_chat_float("UpperX", g_upperChatPosition.x);
        write_chat_float("UpperY", g_upperChatPosition.y);
        write_chat_float("UpperW", g_upperChatSize.x);
        write_chat_float("UpperH", g_upperChatSize.y);

        write_chat_float("LowerX", g_lowerChatPosition.x);
        write_chat_float("LowerY", g_lowerChatPosition.y);
        write_chat_float("LowerW", g_lowerChatSize.x);
        write_chat_float("LowerH", g_lowerChatSize.y);
    }
}
