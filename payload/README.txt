GANGSTERS: ORGANIZED CRIME - WINDOWS 10/11 COMPATIBILITY PATCH
===============================================================

Makes the GOG release of Gangsters: Organized Crime (1998) run on modern Windows PCs and
high-resolution monitors. No installer: just copy the files.

INSTALL
  1. Close the game.
  2. Extract this zip INTO THE GAME FOLDER (the folder that contains gangsters.exe), e.g.
         C:\Program Files (x86)\GOG Galaxy\Games\Gangsters
     Windows may ask for administrator permission to copy into Program Files - allow it.
     (In GOG Galaxy: click the game, the gear icon, "Manage installation" > "Show folder".)
  3. Start the game as usual from GOG Galaxy. Nothing is renamed or overwritten.

UNINSTALL
  Delete the files you extracted: ddraw.dll, cnc-ddraw.dll, ddraw.ini, gangsters_patch.ini,
  gangsters_patch.log (if present), the Shaders folder and the GangstersPatch_info folder.
  (GOG Galaxy's "Verify / Repair" also restores the game folder, but may leave these files.)

WHAT IT FIXES
  * Crash a couple of seconds after launch / "gangsters.exe cannot be started"
  * "Unable to start game as I can't access the direct draw driver" (256-colour error)
  * No music on Windows 10/11
  * Tiny window on 1440p / 4K screens (now borderless fullscreen, scaled, 4:3 kept)
  * Menu buttons and text in the wrong place, missing or unreadable (menus, scenario
    descriptions, tutorial list, game options ...)
  * Crash when starting a new game
  * The game no longer needs "Run as administrator" (settings are stored per user)

SETTINGS (both files are in the game folder)
  ddraw.ini            display: windowed/fullscreen, scaling filter (see the Shaders folder)
  gangsters_patch.ini  fix switches and debug options
  Alt+Enter toggles windowed / fullscreen.

TROUBLESHOOTING
  * If the game used to be set to "Run as administrator" or a compatibility mode (right-click
    gangsters.exe > Properties > Compatibility), untick those options.
  * If you installed an earlier test build with Install.bat: run its Uninstall.bat first (it
    restores the original DSETUP.dll), then extract this zip.
  * Set the trace_* lines in gangsters_patch.ini to 1 and look at gangsters_patch.log in the
    game folder, then report the problem with that file.

NOTES
  * Contains no game files; you need your own (GOG) copy. Only the GOG release is supported.
  * Uses cnc-ddraw (MIT) and miniaudio (public domain); see the GangstersPatch_info folder.
  * The DLLs are unsigned, so some antivirus tools may warn. The source is public.
