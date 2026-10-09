# Launches the game, skips the intro videos with ESC and screenshots the main menu.
# Default target is the development copy (work\game), into which the freshly built build\DSETUP.dll is copied first.
# Use -dir to test another install (e.g. work\install_test) exactly as it is.
# Keys are only sent while the game window is verified to be the foreground window.
param($shot = 'work\shots\menu.png', [int]$introWait = 14, $dir = '')
$root = Split-Path $PSScriptRoot -Parent
if (-not $dir) { $dir = "$root\work\game"; $copyBuild = $true } else { $dir = (Resolve-Path $dir).Path; $copyBuild = $false }
Get-Process gangsters -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 1
if ($copyBuild) { Copy-Item "$root\build\DSETUP.dll" $dir -Force }
Remove-Item "$dir\gangsters_patch.log" -ErrorAction SilentlyContinue
$pr = Start-Process "$dir\gangsters.exe" -WorkingDirectory $dir -PassThru
Start-Sleep $introWait
Add-Type -AssemblyName System.Windows.Forms
foreach ($wait in 5, 7) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\focus_game.ps1"
    if ($LASTEXITCODE -eq 0) { [System.Windows.Forms.SendKeys]::SendWait('{ESC}') } else { Write-Host 'ESC skipped: game not focused' }
    Start-Sleep $wait
}
& powershell -NoProfile -File "$root\tools\shot.ps1" -out "$root\$shot" -scale 0.5
"alive=$([bool](Get-Process gangsters -ErrorAction SilentlyContinue))"
