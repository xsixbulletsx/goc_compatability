# Lists all windows (top-level + children) owned by a process. Usage: windows.ps1 [-name gangsters]
param($name = 'gangsters')
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class WinList {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
  public static List<string> Run(uint pid) {
    SetProcessDPIAware();
    var res = new List<string>();
    EnumProc child = null;
    child = (h, l) => { Add(res, h, pid, "    child "); return true; };
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid) { Add(res, h, pid, "top   "); EnumChildWindows(h, child, IntPtr.Zero); }
      return true; }, IntPtr.Zero);
    return res;
  }
  static void Add(List<string> res, IntPtr h, uint pid, string tag) {
    var c = new StringBuilder(64); GetClassName(h, c, 64);
    var t = new StringBuilder(128); GetWindowText(h, t, 128);
    RECT r; GetWindowRect(h, out r);
    res.Add(string.Format("{0}{1,-10} cls={2,-18} vis={3} rect=({4},{5})-({6},{7}) {8}x{9} parent={10} text='{11}'",
      tag, h.ToString("x"), c, IsWindowVisible(h) ? 1 : 0, r.l, r.t, r.r, r.b, r.r - r.l, r.b - r.t, GetParent(h).ToString("x"), t));
  }
}
'@
$p = Get-Process $name -ErrorAction Stop | Select-Object -First 1
[WinList]::Run([uint32]$p.Id)
