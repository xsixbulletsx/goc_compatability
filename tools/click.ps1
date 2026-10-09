# Click at screen coordinates (full-resolution pixels) - only if the game is (or can be brought to) the foreground.
# Usage: click.ps1 -x 320 -y 97 [-double]
param([int]$x, [int]$y, [switch]$double)
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\focus_game.ps1"
if ($LASTEXITCODE -ne 0) { Write-Host "click skipped: game not focused"; exit 1 }
Add-Type -Name M -Namespace W4 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, System.IntPtr e);
'@
[W4.M]::SetProcessDPIAware() | Out-Null
[W4.M]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 150
$n = if ($double) { 2 } else { 1 }
1..$n | ForEach-Object { [W4.M]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60; [W4.M]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80 }
"clicked $x,$y"
