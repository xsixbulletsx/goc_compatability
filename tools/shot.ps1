# Captures the primary screen to a PNG (DPI-aware). Usage: shot.ps1 -out path.png [-scale 0.5]
param($out, [double]$scale = 0.5)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -Name D -Namespace W3 -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[W3.D]::SetProcessDPIAware() | Out-Null
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$w = [int]($b.Width * $scale); $h = [int]($b.Height * $scale)
$small = New-Object System.Drawing.Bitmap $bmp, $w, $h
$small.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $out ${w}x${h} (screen $($b.Width)x$($b.Height))"
