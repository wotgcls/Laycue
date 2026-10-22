# Naming the pack

The working name is **Laycue** (a "lay cue" is the moment the operator lines up what comes next). It already
fits: it is short, unique, and speaks to live production. This file records the options and the reasons.

## Recommendation: keep **Laycue**, name the parts by their job

| Part | Name | Why |
|---|---|---|
| Pack | **Laycue** | Already used in the repo, installers, brand system and dock IDs. Renaming now would cost a migration (saved settings, hotkeys) for no user benefit. |
| Layouts and overlay rules | **Laycue Scenes** | Plain word the operator already uses ("scenes"). |
| Lower thirds and tickers | **Laycue Titles** | Broadcast term ("titles"), covers lower thirds, scripture bars and tickers. |
| Bitrate and connection | **Laycue Network** | Says what it watches. |

Rule used: a name must say the job in one word an operator already knows, and be searchable
("Laycue Titles" is findable; "Laycue Lower Thirds" is longer and hides tickers).

## Alternatives considered

| Pack name | Verdict |
|---|---|
| **Laycue Studio** | Good fallback if the pack grows a fourth plugin. Rejected for now: "Studio" collides with OBS "Studio Mode". |
| **Cuebox** | Short and memorable. Rejected: generic, many existing products use "cue box". |
| **Showrunner** | Strong meaning (the person in charge). Rejected: widely used trademark in TV software. |
| **Crewkit** | Friendly for volunteer crews. Rejected: sounds like a hardware kit. |
| **Gantry / Tally** | Good broadcast words. Rejected: "Tally" is the red on-air light, which our brand reserves as a colour meaning "on air", so it would blur the message. |

## Before launch

Run a trademark and domain search for "Laycue" (this repo cannot do that offline) and check that
`laycue.com` or a `.app` / `.tv` domain is free. If the name is taken, **Laycue Studio** or **Cuebox** is the
next pick; renaming is a find-and-replace plus a settings migration listed in `docs/PACK.md`.
