# Moves the mouse pointer to screen coordinates (full-resolution pixels) without clicking, if the game is in front.
param([int]$x, [int]$y)
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\focus_game.ps1"
if ($LASTEXITCODE -ne 0) { Write-Host "hover skipped: game not focused"; exit 1 }
Add-Type -Name H -Namespace W7 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
'@
[W7.H]::SetProcessDPIAware() | Out-Null
[W7.H]::SetCursorPos($x, $y) | Out-Null
"moved to $x,$y"
