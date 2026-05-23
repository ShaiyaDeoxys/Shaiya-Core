#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstring>

#include <external/imgui/imgui.h>
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CMonster.h"
#include "include/shaiya/CWorldMgr.h"
#include "include/shaiya/Static.h"
#include "include/shaiya/TargetType.h"
#include "include/speed_monitor.h"

using namespace shaiya;

namespace speed_monitor
{
    namespace
    {
        // Rolling average window size.
        constexpr int kSampleCount = 20;

        // Minimum dt to avoid division by near-zero on very fast frames.
        constexpr float kMinDt = 0.0001f;

        // ---------- State ----------

        float g_samples[kSampleCount]{};
        int   g_sampleIdx = 0;
        bool  g_samplesFull = false;

        LARGE_INTEGER g_lastTime{};
        LARGE_INTEGER g_freq{};
        bool g_timerInit = false;

        D3DVECTOR g_lastPos{};
        bool g_hasLastPos = false;

        float g_currentSpeed = 0.0f;
        float g_peakSpeed = 0.0f;

        // Tracked entity identity -- reset when target changes.
        uint32_t g_trackedId = 0;
        TargetType g_trackedType = TargetType::Default;

        // ---------- Helpers ----------

        // Horizontal distance only (X/Z plane).  The Y axis is vertical
        // height — jumping or falling should not inflate the speed value.
        float xz_dist(const D3DVECTOR& a, const D3DVECTOR& b)
        {
            float dx = a.x - b.x;
            float dz = a.z - b.z;
            return std::sqrtf(dx * dx + dz * dz);
        }

        void reset_tracking()
        {
            std::memset(g_samples, 0, sizeof(g_samples));
            g_sampleIdx = 0;
            g_samplesFull = false;
            g_hasLastPos = false;
            g_currentSpeed = 0.0f;
            g_peakSpeed = 0.0f;
            g_trackedId = 0;
            g_trackedType = TargetType::Default;
        }

        float rolling_average()
        {
            int count = g_samplesFull ? kSampleCount : g_sampleIdx;
            if (count == 0)
                return 0.0f;

            float sum = 0.0f;
            for (int i = 0; i < count; ++i)
                sum += g_samples[i];
            return sum / static_cast<float>(count);
        }

        void push_sample(float speed)
        {
            g_samples[g_sampleIdx] = speed;
            g_sampleIdx = (g_sampleIdx + 1) % kSampleCount;
            if (g_sampleIdx == 0)
                g_samplesFull = true;
        }

        // Resolve the entity to track.  Returns the position and fills
        // outName/outMoveSpeed.  Returns false if nothing is available.
        bool resolve_target(D3DVECTOR& outPos, const char*& outName,
                            uint8_t& outMoveSpeed)
        {
            auto targetId   = g_var->targetId;
            auto targetType = g_var->targetType;

            // Track a targeted User or Mob/Npc if one is selected.
            if (targetId != 0)
            {
                if (targetType == TargetType::User)
                {
                    auto* user = CWorldMgr::FindUser(targetId);
                    if (user)
                    {
                        outPos = user->pos;
                        outName = user->charName.data();
                        outMoveSpeed = user->moveSpeed;
                        return true;
                    }
                }
                else if (targetType == TargetType::Mob ||
                         targetType == TargetType::Npc)
                {
                    auto* mob = CWorldMgr::FindMob(targetId);
                    if (mob)
                    {
                        outPos = mob->pos;
                        outName = "(Mob/NPC)";
                        outMoveSpeed = mob->moveSpeed;
                        return true;
                    }
                }
            }

            // Fallback: local player.
            if (g_pWorldMgr && g_pWorldMgr->user)
            {
                auto* self = g_pWorldMgr->user;
                outPos = self->pos;
                outName = self->charName.data();
                outMoveSpeed = self->moveSpeed;
                return true;
            }

            return false;
        }

        // Check if the tracked entity changed — if so, reset samples.
        void check_target_change()
        {
            auto targetId   = g_var->targetId;
            auto targetType = g_var->targetType;

            // Normalise: no target or item target → self (id=0, Default).
            if (targetId == 0 ||
                targetType == TargetType::Item ||
                targetType == TargetType::Default)
            {
                targetId = 0;
                targetType = TargetType::Default;
            }

            if (targetId != g_trackedId || targetType != g_trackedType)
            {
                // Target changed — reset everything except peak
                // (peak resets only via the manual button).
                std::memset(g_samples, 0, sizeof(g_samples));
                g_sampleIdx = 0;
                g_samplesFull = false;
                g_hasLastPos = false;
                g_currentSpeed = 0.0f;

                g_trackedId = targetId;
                g_trackedType = targetType;
            }
        }
    }

    void render_options()
    {
        // Initialise the high-resolution timer on first call.
        if (!g_timerInit)
        {
            QueryPerformanceFrequency(&g_freq);
            QueryPerformanceCounter(&g_lastTime);
            g_timerInit = true;
        }

        // --- Compute dt ---
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - g_lastTime.QuadPart)
                 / static_cast<float>(g_freq.QuadPart);
        g_lastTime = now;

        // --- Resolve target ---
        check_target_change();

        D3DVECTOR pos{};
        const char* name = "N/A";
        uint8_t moveSpeed = 0;

        bool valid = resolve_target(pos, name, moveSpeed);

        if (valid)
        {
            if (g_hasLastPos && dt > kMinDt)
            {
                float dist = xz_dist(pos, g_lastPos);
                float instantSpeed = dist / dt;

                push_sample(instantSpeed);
                g_currentSpeed = rolling_average();

                if (g_currentSpeed > g_peakSpeed)
                    g_peakSpeed = g_currentSpeed;
            }
            g_lastPos = pos;
            g_hasLastPos = true;
        }

        // --- UI ---
        ImGui::Text("Target: %s", name);
        ImGui::SameLine();
        ImGui::TextDisabled("(stat: %u)", moveSpeed);

        ImGui::Text("Speed:  %.1f", g_currentSpeed);
        ImGui::SameLine();
        ImGui::Text("  Peak: %.1f", g_peakSpeed);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##speed_monitor"))
        {
            reset_tracking();
        }

        if (valid)
        {
            ImGui::TextDisabled("Pos: %.1f, %.1f, %.1f",
                pos.x, pos.y, pos.z);
        }
    }
}
