# Prints the Windows audio-session peak level for a process (e.g. to verify the game is making sound).
# Usage: audio_meter.ps1 [-name gangsters] [-seconds 5]
param($name = 'gangsters', [int]$seconds = 5)
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class AudioMeter {
  [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator {}
  [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IMMDeviceEnumerator { int EnumAudioEndpoints(int f, int m, out IntPtr d); int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice dev); }
  [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IMMDevice { int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o); }
  [ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionManager2 { int a(); int b(); int GetSessionEnumerator(out IAudioSessionEnumerator e); }
  [ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionEnumerator { int GetCount(out int n); int GetSession(int i, out IAudioSessionControl2 s); }
  [ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioSessionControl2 {
    int GetState(out int s); int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string n); int SetDisplayName(string n, ref Guid g);
    int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string p); int SetIconPath(string p, ref Guid g); int GetGroupingParam(out Guid g);
    int SetGroupingParam(ref Guid o, ref Guid g); int RegisterAudioSessionNotification(IntPtr n); int UnregisterAudioSessionNotification(IntPtr n);
    int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id); int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
    int GetProcessId(out uint pid); int IsSystemSoundsSession(); int SetDuckingPreference(bool b); }
  [ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAudioMeterInformation { int GetPeakValue(out float p); }
  public static string Sample(uint pid, int seconds) {
    var en = (IMMDeviceEnumerator)new MMDeviceEnumerator(); IMMDevice dev; en.GetDefaultAudioEndpoint(0, 1, out dev);
    var iid = typeof(IAudioSessionManager2).GUID; object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
    var mgr = (IAudioSessionManager2)o; float max = 0; bool found = false; int state = -1;
    for (int t = 0; t < seconds * 20; t++) {
      IAudioSessionEnumerator se; mgr.GetSessionEnumerator(out se); int n; se.GetCount(out n);
      for (int i = 0; i < n; i++) { IAudioSessionControl2 c; se.GetSession(i, out c); uint p; c.GetProcessId(out p);
        if (p == pid) { found = true; c.GetState(out state); float pk; ((IAudioMeterInformation)c).GetPeakValue(out pk); if (pk > max) max = pk; } }
      System.Threading.Thread.Sleep(50);
    }
    return found ? string.Format("audio session found: state={0} (1=active) peak={1:F4}", state, max) : "no audio session for this process";
  }
}
'@
$p = Get-Process $name -ErrorAction Stop | Select-Object -First 1
[AudioMeter]::Sample([uint32]$p.Id, $seconds)
