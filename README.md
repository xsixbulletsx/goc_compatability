# Gangsters: Organized Crime - Windows 11 patch

Makes the GOG release of *Gangsters: Organized Crime* (1998) run on modern Windows 11 PCs and high-resolution displays.
The patch contains **no game files**: it is a handful of small DLLs and a config that sit next to your own copy of the game.

## For players

Download the zip from the [latest release](https://github.com/xsixbulletsx/goc_compatability/releases/latest) and **extract it into the game folder** (the one with `gangsters.exe`). No installer, nothing is renamed or overwritten; delete the files to uninstall. Details: `payload/README.txt`. Only the GOG release of the game is supported.

## Problems fixed

| Symptom | Cause | Fix |
|---|---|---|
| Crash ~2 s after launch (`msmpeg2ac3dec.dll`, `0xC0000602`); launcher says "gangsters.exe cannot be started" | The game plays `Music\*.mp3` through MCI; on current Windows 11 builds that routes through DirectShow, whose AC3 decoder fail-fasts | The shim hooks the MCI calls and plays the music with [miniaudio](https://github.com/mackron/miniaudio) |
| "Unable to start game as I can't access the direct draw driver ... 256 colour" | 8-bit DirectDraw modes are unavailable on modern GPUs | [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) (MIT) shipped as `cnc-ddraw.dll`, with our shim loaded as `ddraw.dll` in front of it |
| Tiny 640x480 window on 1440p/4K | Fixed-resolution engine | cnc-ddraw borderless fullscreen with aspect-correct, shader-filtered upscaling |
| Menu buttons in the wrong place, white boxes over the art | The menus are real Win32 child controls in game coordinates; the game paints the button art on its DirectDraw surface and relies on exclusive fullscreen to hide the controls | Shim moves/scales the controls (WH_CBT hook), makes `GetWindowRect` report game-space coordinates to the button painter (call-site patch) and renders the labels itself in small per-pixel-alpha popup windows that forward the mouse to the real control (no manifest needed) |
| Crash starting a new game | Character-appearance code asks for frame 18 of an 18-frame sprite; the blit only validates `w*h` so a zero width loops 2^32 times | In-memory patch rejects non-positive width/height |
| No music even once running | GOG's CD-to-mp3 emulation (`PATCH.dll`) is detoured through winmm and then cnc-ddraw replaces `PATCH.dll`'s own `mciSendCommandA` import, so its mp3 playback silently fails; the game's own calls also stop reaching it | The shim calls `PATCH.dll`'s dispatcher directly (located by byte signature) and puts its mp3 player back in front of `PATCH.dll`'s import before each command |
| Needing "run as administrator" | The game writes `HKLM\SOFTWARE\Hothouse\Gangsters` | Registry redirect to `HKCU\Software\Hothouse` (seeded from HKLM) |

## How it works

Our `ddraw.dll` is a static dependency of GOG's `PATCH.dll`, so Windows loads it before the game runs. It forwards every DirectDraw export to the real wrapper (`cnc-ddraw.dll`, shipped renamed) and, once all DLLs have initialised (it hooks the CRT's first `GetStartupInfoA` call to wait for that), installs import-table hooks and a few byte-verified in-memory patches in the game process. Nothing of the game is replaced, so installing is just copying files and uninstalling is deleting them. A `winmm.dll` proxy does **not** work (a Windows compatibility shim loads the System32 copy first), and neither does replacing `DSETUP.dll` without renaming the original, hence the `ddraw.dll` route.

Source layout (`src/`):

| File | Purpose |
|---|---|
| `dsetup_shim.c` | DllMain (loaded as `ddraw.dll`), deferred install,  IAT hook helper, crash logger, wiring |
| `mci_music.c` | mp3 playback (miniaudio) behind MCI; routing of the game's CD commands to `PATCH.dll` |
| `window_fix.c` | menu control scaling, label rendering, repaint emulation |
| `patches.c` | byte-verified in-memory exe patches |
| `registry_redirect.c` | per-user settings |
| `mixer_trace.c`, `file_trace.c` | optional diagnostics |

## Building

```powershell
python -m venv .venv
.venv\Scripts\pip install ziglang
.\package.ps1        # fetches deps, builds the 32-bit DLL, assembles dist\GangstersPatch-v<version>.zip
```

Diagnostics go to `gangsters_patch.log` in the game folder; see the `[debug]` section of `gangsters_patch.ini`.
`tools/` has helpers used during development (screenshots, window listing, an audio meter for the game's audio session).

## Status

Working on a Windows 11 / 1440p machine with the GOG release: launch, intros, menus, new game, music. Still to do: more
in-game testing at high resolution, multiplayer (DirectPlay), higher in-game resolutions.




