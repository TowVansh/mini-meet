# Open real console windows, run each scene and screenshot the window itself.
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public class Shot {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int t, uint f);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
[Shot]::SetProcessDPIAware() | Out-Null
Add-Type -AssemblyName System.Drawing
$img = "E:\Projects\MiniMeet\docs\img"
$scene = "/mnt/e/Projects/MiniMeet/docs/doc/scene.sh"
function Wait($ms) { [Threading.Thread]::CurrentThread.Join($ms) | Out-Null }

function Open($title, $cols, $lines, $cmd) {
  $p = Start-Process cmd.exe -ArgumentList "/c title $title & mode con cols=$cols lines=$lines & $cmd" -PassThru
  Wait 1500
  $p.Refresh()
  return $p
}

function Snap($p, $name) {
  $p.Refresh()
  $h = [IntPtr]$p.MainWindowHandle
  [Shot]::SetWindowPos($h, [IntPtr]::new(-1), 30, 30, 0, 0, 0x41) | Out-Null
  [Shot]::SetForegroundWindow($h) | Out-Null
  Wait 900
  $r = New-Object Shot+RECT
  [Shot]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null
  $w = $r.R - $r.L; $t = $r.B - $r.T
  $b = New-Object System.Drawing.Bitmap $w, $t
  [System.Drawing.Graphics]::FromImage($b).CopyFromScreen($r.L, $r.T, 0, 0, $b.Size)
  $b.Save("$img\$name.png")
  "$name $w x $t"
}

function Scene($name, $cols, $lines, $user, $delay) {
  $p = Open "Mini-Meet" $cols $lines "wsl -d Ubuntu -u $user -- bash $scene $name"
  Wait $delay
  Snap $p "shot-$name"
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}

if ($args[0] -ne "stun") {
# signalling: server window stays open while the client exchange runs
$srv = Open "mm-server" 90 12 "wsl -d Ubuntu -u root -- bash $scene server"
Wait 2500
$cli = Open "Signalling test" 80 18 "wsl -d Ubuntu -u root -- bash $scene signal"
Wait 4000
Snap $cli "shot-signal"
Snap $srv "shot-server"
Stop-Process -Id $cli.Id, $srv.Id -Force -ErrorAction SilentlyContinue
wsl -d Ubuntu -u root -- pkill -x mm-server

Scene "test" 60 6 "root" 15000
Scene "netem" 80 14 "root" 4000
Scene "cc" 100 16 "root" 3500
Scene "nat" 125 20 "root" 3500
}

# Windows client live: STUN finds the public address, then a call on this PC
$s = Start-Process "E:\Projects\MiniMeet\bin\mm-server.exe" -ArgumentList "9703" -WorkingDirectory "E:\Projects\MiniMeet\bin" -WindowStyle Hidden -PassThru
Wait 600
$a = Start-Process "E:\Projects\MiniMeet\bin\mm.exe" -ArgumentList "--server 127.0.0.1:9703 --room demo --name PC2 --test --no-mirror --duration 40" -WorkingDirectory "E:\Projects\MiniMeet\bin" -PassThru
Wait 800
$c = Start-Process cmd.exe -ArgumentList "/c E:\Projects\MiniMeet\docs\doc\client.bat" -PassThru
Wait 14000
Snap $c "shot-stun"
Stop-Process -Name mm -Force -ErrorAction SilentlyContinue
Stop-Process -Id $c.Id, $s.Id -Force -ErrorAction SilentlyContinue
"done"
