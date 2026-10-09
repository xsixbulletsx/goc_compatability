# Builds the 32-bit shim (build\ddraw.dll) with Zig's bundled clang.
# It is loaded by the game as ddraw.dll (a static dependency of GOG's PATCH.dll) and forwards every DirectDraw export
# to cnc-ddraw.dll, which is shipped next to it under that name. Nothing of the game is replaced.
# Requires the project venv:  python -m venv .venv ; .venv\Scripts\pip install ziglang
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot; $py = "$root\.venv\Scripts\python.exe"
New-Item -ItemType Directory -Force "$root\build" | Out-Null
$sources = 'dsetup_shim.c', 'mci_music.c', 'file_trace.c', 'patches.c', 'window_fix.c', 'registry_redirect.c', 'mixer_trace.c', 'miniaudio_impl.c', 'ddraw.def' | ForEach-Object { "$root\src\$_" }
& $py -m ziglang cc -target x86-windows-gnu -O2 -shared -DGOC_AS_DDRAW -o "$root\build\ddraw.dll" `
  -I "$root\third_party\miniaudio" @sources -lkernel32 -luser32 -lgdi32 -lole32
if ($LASTEXITCODE) { throw "build failed" }
"built: ddraw.dll " + (Get-Item "$root\build\ddraw.dll").Length + " bytes"
