# Builds everything and assembles dist\GangstersPatch-<version>.zip.
# The zip is meant to be extracted straight into the game folder: it contains only NEW files (no game files are
# replaced or renamed), plus a README and licences in sub folders.
param([string]$Version = '0.2.0')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& "$root\tools\fetch_deps.ps1"
& "$root\build.ps1"

$out = "$root\dist\GangstersPatch"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out, "$out\GangstersPatch_info" | Out-Null

# files that end up next to gangsters.exe
Copy-Item "$root\build\ddraw.dll" "$out\ddraw.dll"                                    # our shim, forwards to cnc-ddraw.dll
Copy-Item "$root\third_party\cnc-ddraw\extracted\ddraw.dll" "$out\cnc-ddraw.dll"      # the real DirectDraw wrapper
Copy-Item "$root\third_party\cnc-ddraw\extracted\Shaders" "$out\Shaders" -Recurse
Copy-Item "$root\payload\ddraw.ini" "$out\ddraw.ini"
Copy-Item "$root\payload\gangsters_patch.ini" "$out\gangsters_patch.ini"
# documentation (kept in a sub folder so it does not clutter the game folder)
Copy-Item "$root\payload\README.txt" "$out\GangstersPatch_info\README.txt"
Copy-Item "$root\third_party\licenses\*" "$out\GangstersPatch_info"
Copy-Item "$root\third_party\NOTICE.md" "$out\GangstersPatch_info\NOTICE.md"

$zip = "$root\dist\GangstersPatch-v$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$out\*" -DestinationPath $zip
"packaged: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB)  sha256 $((Get-FileHash $zip -Algorithm SHA256).Hash)"
