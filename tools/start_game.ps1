# Launches the game and gets to the first in-game screen (New Game -> Start Game). Usage: start_game.ps1 [-dir work\zip_test]
param($dir = 'work\zip_test', $shot = 'work\shots\ingame.png')
$root = Split-Path $PSScriptRoot -Parent
& powershell -NoProfile -File "$root\tools\run_to_menu.ps1" -shot work\shots\_menu.png -dir $dir | Out-Null
& powershell -NoProfile -File "$root\tools\click.ps1" -x 1280 -y 297 | Out-Null; Start-Sleep 4     # New Game
& powershell -NoProfile -File "$root\tools\click.ps1" -x 704 -y 1354 | Out-Null; Start-Sleep 14    # Start Game
& powershell -NoProfile -File "$root\tools\shot.ps1" -out "$root\$shot" -scale 0.5
