# Branded transitions (Laycue Scenes)

Laycue Scenes adds nine scene transitions to OBS's own transition list (the **Scene Transitions** dock, **+**). They
are native OBS transitions, so they work with Studio Mode, hotkeys and the T-bar like the built-in ones, and their
length is OBS's transition **Duration**.

The quickest way to add one: in the Laycue panel's **Shots** tab, click **Transition: … ▾** and pick
**+ Add Laycue Push to OBS…** (or any other). Laycue opens OBS's own add step, so you type a name (or press OK),
then set the colours and logo. The new transition is selected for Go Live and saved with your scene collection.
Transitions already in OBS's list are picked from the same menu.

| Transition | What it does |
|---|---|
| **Laycue Push** | The new shot pushes the old one off to the left, with a band of brand colour between them. |
| **Laycue Brand Wipe** | A wide brand-colour bar sweeps across carrying your logo; the new shot is behind it. |
| **Laycue Zoom Through** | The old shot rushes toward you into a wash of brand colour and logo; the new shot settles out of it. |
| **Laycue Split Doors** | The old shot splits down the middle and slides apart, with brand-colour edges on the doors. |
| **Laycue Logo Sting** | A brand panel rises over the picture, your logo pops on it, and it lifts away to show the new shot. |
| **Laycue Card Stack** | The old shot steps back and dims; the new one slides up over it like a card. |
| **Laycue Move** | Cameras and pictures that are in both shots glide from where they were to where they will be (for example from a single shot into a split layout); the rest cross-fades. Sources inside a nested scene move too. |
| **Laycue Morph** | The old shot swells and melts into the new one, which settles in from slightly smaller, with a touch of brand colour. |
| **Laycue Stripes** | Bands of the two brand colours slide in one after another, the logo shows, then they slide on out. |

## Branding

Right-click the transition in the **Scene Transitions** dock > **Properties**:

- **Brand colour** and **Second colour**.
- **Logo (picture or animated GIF)**: a PNG with a transparent background looks best. An animated GIF plays while
  the transition runs. Brand Wipe, Zoom Through, Logo Sting and Stripes show it.

Add the same transition more than once (with different names) to keep several brand looks, for example one for
Sunday service and one for a conference.

Code: `plugins/scenes/src/obs/brand-transitions.cpp`. Everything is drawn with OBS's own graphics (no video files),
and the scene audio cross-fades as with OBS's Fade.
