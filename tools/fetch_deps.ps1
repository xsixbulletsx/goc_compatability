# Downloads third-party dependencies into third_party/ (not committed).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ua = @{ 'User-Agent' = 'goc_patch' }

# cnc-ddraw (MIT) - DirectDraw wrapper: fixes the 256-colour mode error and provides scaling
$ddZip = "$root\third_party\cnc-ddraw\cnc-ddraw.zip"
$ddSha = '0B13AB89A64C9918189B1DADD449EF6ED3CB3B7B19CABD96D8ADBD95505BB908'
New-Item -ItemType Directory -Force "$root\third_party\cnc-ddraw" | Out-Null
if (-not (Test-Path "$root\third_party\cnc-ddraw\extracted\ddraw.dll")) {
    Invoke-WebRequest 'https://github.com/FunkyFr3sh/cnc-ddraw/releases/download/v7.1.0.0/cnc-ddraw.zip' -OutFile $ddZip -UseBasicParsing
    if ((Get-FileHash $ddZip -Algorithm SHA256).Hash -ne $ddSha) { throw 'cnc-ddraw hash mismatch' }
    Expand-Archive $ddZip "$root\third_party\cnc-ddraw\extracted" -Force
}

# miniaudio (public domain / MIT-0) - single header audio library with built-in mp3 decoder
New-Item -ItemType Directory -Force "$root\third_party\miniaudio" | Out-Null
if (-not (Test-Path "$root\third_party\miniaudio\miniaudio.h")) {
    # pinned to the version this project was developed against (0.11.25)
    Invoke-WebRequest 'https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h' -OutFile "$root\third_party\miniaudio\miniaudio.h" -UseBasicParsing
}
'dependencies ready'
