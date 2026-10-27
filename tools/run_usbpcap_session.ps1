# Run from an elevated powershell.exe console. No updater or device OUT operations.
# USBPcap cmd.c wait_for_exit_signal uses ReadConsoleInput(KEY_EVENT, key-down ASCII 'q').
[CmdletBinding()]
param(
    [Alias('Root')][ValidatePattern('^\\\\\.\\USBPcap[1-9][0-9]?$')][string]$CaptureDevice='',
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,79}$')][string]$SessionName,
    [ValidatePattern('^$|^[0-9]+(,[0-9]+)*$')][string]$Addresses = '',
    [switch]$CaptureAll,
    [ValidateRange(1,600)][int]$MaxSeconds = 600,
    [ValidateSet('hidden','visible')][string]$ConsoleVisibility = 'hidden',
    [string]$ToolPath = ''
)

. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$CaptureDevice = Resolve-ThyLocalValue -Name 'THY666_USBPCAP_DEVICE' -Value $CaptureDevice -Explicit:($PSBoundParameters.ContainsKey('CaptureDevice')) -Configuration $localConfiguration -Required -Pattern '^\\\\\.\\USBPcap[1-9][0-9]?$'
$Addresses = Resolve-ThyLocalValue -Name 'THY666_USBPCAP_ADDRESSES' -Value $Addresses -Explicit:($PSBoundParameters.ContainsKey('Addresses')) -Configuration $localConfiguration -Required:(!$CaptureAll) -Pattern '^[0-9]+(,[0-9]+)*$'
$ToolPath = Resolve-ThyLocalValue -Name 'THY666_USBPCAP_PATH' -Value $ToolPath -Explicit:($PSBoundParameters.ContainsKey('ToolPath')) -Configuration $localConfiguration -Default (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'USBPcap/USBPcapCMD.exe') -AsPath -Required

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'analysis\local\usb-update-capture'))
$sessionDir = [IO.Path]::GetFullPath((Join-Path $outputRoot $SessionName))
if (-not $sessionDir.StartsWith($outputRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid session path.' }
$preflightIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$preflightElevated = (New-Object Security.Principal.WindowsPrincipal($preflightIdentity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $preflightElevated) { throw 'This terminal is not elevated. Open an administrator PowerShell console and rerun this recorder; no session directory was created.' }
if (Test-Path -LiteralPath $sessionDir) { throw 'Session directory already exists; never overwrite a capture.' }
if (-not $CaptureAll -and @($Addresses.Split(',') | Where-Object { [int]$_ -lt 1 -or [int]$_ -gt 127 }).Count) { throw 'USB addresses must be 1..127.' }
[void](New-Item -ItemType Directory -Path $sessionDir)
$pcapFile = Join-Path $sessionDir 'capture.pcap'
$stderrFile = Join-Path $sessionDir 'stderr.log'
$stateFile = Join-Path $sessionDir 'state.json'
$stopFile = Join-Path $sessionDir '.stop'
$child = $null; $childStart = $null; $consoleInput = [IntPtr]::Zero; $exitStatus = 2
$audit = [ordered]@{
    Format='thy333.usbpcap-session'; Version=3; StartedUtc=[DateTime]::UtcNow.ToString('o')
    SupervisorPid=$PID; State='preparing'; Elevated=$false; CaptureDevice=$CaptureDevice
    CaptureMode=$(if ($CaptureAll) { 'all-devices-on-selected-root' } else { 'selected-addresses-and-new-devices' })
    Addresses=$(if ($CaptureAll) { @() } else { @($Addresses.Split(',')) }); MaxSeconds=$MaxSeconds
    ToolPath=$ToolPath; ToolSha256=$null; ChildPid=$null; ChildStartUtc=$null; ChildPath=$null
    PcapFile=$pcapFile; StdoutFile=$null; StdoutDestination=($ConsoleVisibility + '-supervisor-console'); ConsoleVisibility=$ConsoleVisibility; StderrFile=$stderrFile; StopFile=$stopFile
    ConsoleInputValidated=$false; ConsoleOutputValidated=$false; StopRequested=$false; StopReason=$null; QEventsWritten=0
    GracefulExitConfirmed=$false; ForcedStop=$false; ChildExitCode=$null; PcapBytes=0
    BaselineBytes=$null; FileGrowthObserved=$false; LiveTrafficConfirmed=$false; Errors=@()
}
function Save-State {
    $audit.UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    $stateTemp = $stateFile + '.tmp'
    [IO.File]::WriteAllText($stateTemp, ($audit | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $stateTemp -Destination $stateFile -Force
}
function Assert-OwnedChild {
    if ($child.HasExited) { return }
    if ($child.MainModule.FileName -ine $ToolPath -or $child.StartTime.ToUniversalTime() -ne $childStart) {
        throw 'Child process identity changed; refusing console input or forced stop.'
    }
}
try {
    Save-State
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $audit.Elevated = (New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $audit.Elevated) { throw 'Administrator token required; no auto-elevation or alternate worker.' }
    $ToolPath = [IO.Path]::GetFullPath($ToolPath)
    if ([IO.Path]::GetFileName($ToolPath) -ine 'USBPcapCMD.exe') { throw 'Expected the pinned USBPcapCMD executable.' }
    $audit.ToolSha256 = (Get-FileHash -LiteralPath $ToolPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($audit.ToolSha256 -ne '8b13173e9453fda9914d707c732880de44081e20e1113b3a2c827abd7a28b15d') { throw 'USBPcapCMD differs from the reviewed installed binary.' }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ThyCaptureConsole {
  [StructLayout(LayoutKind.Explicit, Size=20)] public struct InputRecord {
    [FieldOffset(0)] public ushort EventType; [FieldOffset(4)] public int KeyDown;
    [FieldOffset(8)] public ushort Repeat; [FieldOffset(10)] public ushort VirtualKey;
    [FieldOffset(12)] public ushort Scan; [FieldOffset(14)] public ushort Unicode;
    [FieldOffset(16)] public uint Control;
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr GetStdHandle(int kind);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetConsoleMode(IntPtr h, out uint mode);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr CreateFileW(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool WriteConsoleInputW(IntPtr h, InputRecord[] records, uint count, out uint written);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
  public static InputRecord[] QRecords() {
    InputRecord down = new InputRecord { EventType=1, KeyDown=1, Repeat=1, VirtualKey=0x51, Scan=0x10, Unicode=0x71 };
    InputRecord up=down; up.KeyDown=0; return new InputRecord[] { down, up };
  }
  public static uint SendQ(IntPtr h) {
    uint written;
    if (!WriteConsoleInputW(h, QRecords(), 2, out written) || written!=2)
      throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Console q injection failed");
    return written;
  }
}
'@
    [uint32]$inputMode = 0
    if (-not [ThyCaptureConsole]::GetConsoleMode([ThyCaptureConsole]::GetStdHandle(-10), [ref]$inputMode)) { throw 'Supervisor STDIN is not a console input handle; cannot guarantee q control.' }
    if (-not [ThyCaptureConsole]::GetConsoleMode([ThyCaptureConsole]::GetStdHandle(-11), [ref]$inputMode)) { throw 'Supervisor STDOUT must be a console so USBPcap attaches to it.' }
    $audit.ConsoleOutputValidated = $true
    $consoleInput = [ThyCaptureConsole]::CreateFileW('CONIN$', 0x40000000, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    if ($consoleInput -eq [IntPtr]::Zero -or $consoleInput -eq [IntPtr](-1)) { throw 'Cannot open the supervisor console input for q control.' }
    $audit.ConsoleInputValidated = $true
    $filterArgs = if ($CaptureAll) { '--capture-from-all-devices' } else { '--devices ' + $Addresses + ' --capture-from-new-devices' }
    $captureArgs = '-d "{0}" -o "{1}" --snaplen 65535 --bufferlen 16777216 --inject-descriptors {2}' -f $CaptureDevice,$pcapFile,$filterArgs
    $audit.Arguments = $captureArgs
    # Keep STDOUT on the hidden console: USBPcap skips AttachConsole if both outputs are redirected.
    # -NoNewWindow supplies inherited STDIN; do not redirect either STDIN or STDOUT.
    if (@([Diagnostics.Process]::GetProcessesByName('USBPcapCMD')).Count) { throw 'A USBPcap recorder is already running; no duplicate child was started.' }
    $child = Start-Process -FilePath $ToolPath -ArgumentList $captureArgs -WorkingDirectory $sessionDir -NoNewWindow -RedirectStandardError $stderrFile -PassThru
    $childStart = $child.StartTime.ToUniversalTime()
    $audit.ChildPid=$child.Id; $audit.ChildStartUtc=$childStart.ToString('o'); $audit.ChildPath=$child.MainModule.FileName
    Assert-OwnedChild
    $audit.State='running-control-prepared'; Save-State
    $elapsed = [Diagnostics.Stopwatch]::StartNew(); $lastSaved = 0
    while (-not $child.HasExited -and $elapsed.Elapsed.TotalSeconds -lt $MaxSeconds) {
        if (Test-Path -LiteralPath $stopFile) { $audit.StopReason='local-stop-file'; break }
        if (Test-Path -LiteralPath $pcapFile) { $audit.PcapBytes=(Get-Item -LiteralPath $pcapFile).Length }
        if ($null -eq $audit.BaselineBytes -and $elapsed.Elapsed.TotalSeconds -ge 2) { $audit.BaselineBytes=$audit.PcapBytes }
        if ($null -ne $audit.BaselineBytes -and $audit.PcapBytes -gt $audit.BaselineBytes) { $audit.FileGrowthObserved=$true }
        if ($elapsed.Elapsed.TotalSeconds -ge $lastSaved + 1) { Save-State; $lastSaved=$elapsed.Elapsed.TotalSeconds }
        Start-Sleep -Milliseconds 250
    }
    if (-not $child.HasExited) {
        if (-not $audit.StopReason) { $audit.StopReason='bounded-session-deadline' }
        $audit.StopRequested=$true; $audit.State='graceful-stop-requested'; Assert-OwnedChild
        $audit.QEventsWritten=[ThyCaptureConsole]::SendQ($consoleInput); Save-State
        $audit.GracefulExitConfirmed=$child.WaitForExit(5000)
    } else { $audit.StopReason='child-exited-before-stop-request' }
    if (-not $child.HasExited) { Assert-OwnedChild; $audit.ForcedStop=$true; $child.Kill(); [void]$child.WaitForExit(5000) }
    $audit.ChildExitCode=$child.ExitCode
    $audit.State='exited'
    if ($audit.GracefulExitConfirmed -and -not $audit.ForcedStop -and $child.ExitCode -eq 0) { $exitStatus=0 }
} catch {
    $audit.State='error'; $audit.Errors += $_.Exception.Message
} finally {
    if ($null -ne $child -and -not $child.HasExited) {
        try { Assert-OwnedChild; $audit.ForcedStop=$true; $child.Kill(); [void]$child.WaitForExit(5000) }
        catch { $audit.Errors += ('Own-child cleanup failed: ' + $_.Exception.Message) }
    }
    if ($consoleInput -ne [IntPtr]::Zero -and $consoleInput -ne [IntPtr](-1)) { [void][ThyCaptureConsole]::CloseHandle($consoleInput) }
    if (Test-Path -LiteralPath $pcapFile) { $audit.PcapBytes=(Get-Item -LiteralPath $pcapFile).Length }
    $audit.FinishedUtc=[DateTime]::UtcNow.ToString('o'); Save-State
}
exit $exitStatus
