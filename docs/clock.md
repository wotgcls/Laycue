# Laycue Clock: how it works

A timer drawn inside OBS: a countdown ("Service starts in 04:32"), a count up (sermon length), or a countdown to a time of day
("10:00"). It is a native source, like Laycue Titles, so it needs no browser source and no web page.

## Eight designs, three backgrounds

Designs: **Big digits**, **Corner pill**, **Lower bar**, **Ring**, **Flip cards**, **Progress bar**, **Starting soon**, **Minimal**.
Each one works on any of three backgrounds, chosen separately:

- **None (over video):** only the digits and caption, with a soft shadow, so it reads on top of a camera or video source.
- **Card:** a rounded card behind the clock, in a colour and strength you choose.
- **Full screen:** a colour (and strength) over the whole picture, for a "Starting soon" scene. Put your own video or picture
  under it in OBS and lower the strength to let it show through.

You can also place the clock over any video or picture source in OBS, since its background is transparent by default.

## How it counts

- **Down:** type a length like `5`, `5:30` or `1:30:00`, or press a preset (1, 5, 10, 15, 30 minutes).
- **Up:** a stopwatch.
- **To a time:** type a time of day like `10:00` or `9:30 am`; it runs by itself and goes to tomorrow if the time has passed.
- **Warning:** the digits change colour when the chosen amount of time is left (default 60 seconds), and again at zero.
- **Keep counting after zero:** a countdown carries on as `+00:12`, so a speaker can see how far over they are.
- **Start, Pause, Reset, +1 min, -1 min** in the panel, and two hotkeys in OBS (Settings > Hotkeys): start or pause, and reset.

The time is measured from a steady clock, not counted frame by frame, so it does not drift. The clock keeps running while its
scene is not on screen, like a real timer.

## Putting it in OBS

The panel makes the clock layer for you: **Add to this scene** (or **Add to all scenes**). The layer is a normal OBS source, so
use OBS's own eye icon to show or hide it and the usual source tools to move it behind or in front of other things.
The same settings are in the source's Properties window.

## Not built yet

A separate **stage clock** for the speaker (a private screen only they see, with "5 minutes left" colours). It needs its own
output window, so it waits until someone asks for it.

## Counting to a time of day ("At a time")

The first tab. Pick the time the service starts and the clock counts down to it using this computer's clock, so nobody
has to work out the minutes. Type a time (`5:00 PM`, `5pm`, `17:00`), nudge it with -5 and +5, or press **In 15 min**,
**In 30 min** or **Next hour**. A line under it shows the PC time and what is left ("Now 4:18 PM. Starts in 42 min."), and
says so if that time has already passed today (then it counts to tomorrow). It starts by itself: there is no Start to press.
After zero it keeps counting up as overtime unless that is turned off in Look.
