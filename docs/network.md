# Laycue Network: how it works and why

## The problem
Operators guess a bitrate. Too high and frames drop; too low and quality is wasted. OBS's own Dynamic
Bitrate only lowers when the output buffer backs up, cannot explain itself, and has no floor or memory.

## What it does
1. **Test** (only when the operator clicks, after consent): uploads a few seconds of random data to a
   configurable server, measures typical speed, dips and steadiness, and recommends a bitrate:
   about 75% of typical speed, never above 90% of dips, minus audio, snapped to a standard ladder and capped by
   the platform's ceiling (Twitch, YouTube, Facebook, Kick, custom). An unsteady link gets one step lighter.
2. **Watch** the live stream (dropped frames, congestion, send rate) with a three-minute chart and a health
   state that uses shape and words, not colour alone.
3. **Auto-adjust** (off by default): lowers the bitrate about 20% after two bad seconds, raises it slowly after
   calm, remembers levels that failed, honours a floor and ceiling, and stops by itself if the encoder ignores a
   change. It waits if OBS's Dynamic Bitrate is on, because two controllers would fight.
4. **Report** after each stream: average bitrate, dropped frames, roughest minute.

## Why these choices
- Pure C++ core (`src/core`) with 100+ simulated-network tests, because a bug here changes a live stream.
- Changes go through `obs_encoder_update` on the running encoder; nothing is restarted.
- No accounts and no telemetry. The only network use is the speed test the operator starts.

## Known limits
- Not all hardware encoders accept a bitrate change mid-stream; Laycue detects this and stops.
- Verified against simulated networks; real streaming against a throttled server is still to do.
