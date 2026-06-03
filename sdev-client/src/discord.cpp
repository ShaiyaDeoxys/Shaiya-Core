#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <external/discord-rpc/include/discord_rpc.h>

namespace
{
    constexpr char kAppId[] = "1402021308056731698";
    constexpr char kDetails[] = "Playing Shaiya";
    constexpr int kUpdateMs = 5000;

    const std::int64_t kStartTime =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    DWORD WINAPI rpc_thread(LPVOID)
    {
        DiscordEventHandlers handlers{};
        Discord_Initialize(kAppId, &handlers, 1, nullptr);
        while (true)
        {
            DiscordRichPresence presence{};
            presence.details = reinterpret_cast<const char8_t*>(kDetails);
            presence.startTimestamp = kStartTime;
            Discord_UpdatePresence(&presence);
            Discord_RunCallbacks();
            Sleep(kUpdateMs);
        }
    }
}

void init_discord_rpc()
{
    CreateThread(nullptr, 0, rpc_thread, nullptr, 0, nullptr);
}
