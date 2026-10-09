# Brings the game window to the foreground and verifies it. Exit code 0 = focused, 1 = could not focus.
param($name = 'gangsters')
Add-Type -Name F -Namespace W6 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
'@
$p = Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "no $name window"; exit 1 }
for ($i = 0; $i -lt 5; $i++) {
    [W6.F]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W6.F]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # ALT tap unlocks SetForegroundWindow
    [W6.F]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [W6.F]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 250
    $fg = [W6.F]::GetForegroundWindow(); $fpid = 0; [W6.F]::GetWindowThreadProcessId($fg, [ref]$fpid) | Out-Null
    if ($fpid -eq $p.Id) { exit 0 }
}
Write-Host "could not bring $name to the foreground"; exit 1
