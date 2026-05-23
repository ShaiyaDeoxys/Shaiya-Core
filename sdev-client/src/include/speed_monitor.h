#pragma once

// ===========================================================================
// speed_monitor -- Real-time speed tracker for the F9 debug panel
// ===========================================================================
//
// Monitors the local player or the currently targeted player/mob and
// calculates a relative speed value from position deltas over time.
//
// ---- How it works ----
//
//  Every frame, the module samples the tracked entity's D3DVECTOR pos.
//  Speed is computed as the 3D Euclidean distance between the current
//  position and the previous sample, divided by the elapsed time (dt)
//  in seconds.  A short rolling average (last N samples) smooths out
//  per-frame jitter from interpolation and micro-corrections.
//
//  The raw speed value has no canonical unit -- it is a relative measure
//  in world-coordinate-units per second.  What matters is comparing
//  values: a normal running player produces a consistent baseline, and
//  anything significantly above that baseline is suspect.
//
// ---- Target selection ----
//
//  - No target / target is item  -> tracks the local player (self).
//  - Target is User              -> tracks that player via FindUser().
//  - Target is Mob/Npc           -> tracks that entity via FindMob().
//  - If the tracked entity disappears, falls back to self.
//
// ---- Usage ----
//
//  Called from debug_panel::render() inside a CollapsingHeader.
//  Displays: tracked name, current speed, peak speed, stat moveSpeed,
//  and a reset button.
//
// ===========================================================================

namespace speed_monitor
{
    // Draws the speed monitor controls inside the debug panel.
    void render_options();
}
