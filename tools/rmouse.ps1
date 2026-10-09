# Relative mouse input for the in-game UI (the game reads DirectInput deltas, so SetCursorPos does not move its cursor).
# Usage: rmouse.ps1 -dx 100 -dy 50 [-click] [-reset]   (-reset first slams the pointer into the top-left corner)
param([int]$dx = 0, [int]$dy = 0, [switch]$click, [switch]$reset)
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\focus_game.ps1"
if ($LASTEXITCODE -ne 0) { Write-Host "rmouse skipped: game not focused"; exit 1 }
Add-Type -Name R -Namespace W8 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, System.IntPtr e);
'@
function Move-Rel($x, $y) {
    $step = 40
    while ($x -ne 0 -or $y -ne 0) {
        $sx = [Math]::Max(-$step, [Math]::Min($step, $x)); $sy = [Math]::Max(-$step, [Math]::Min($step, $y))
        [W8.R]::mouse_event(1, $sx, $sy, 0, [IntPtr]::Zero)      # MOUSEEVENTF_MOVE (relative)
        $x -= $sx; $y -= $sy; Start-Sleep -Milliseconds 8
    }
}
if ($reset) { Move-Rel -4000 -4000 }
Move-Rel $dx $dy
Start-Sleep -Milliseconds 150
if ($click) { [W8.R]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80; [W8.R]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 100 }
"moved ($dx,$dy)" + $(if ($click) { ' + click' } else { '' })
