# Exercise the actual collector send function with a SerialPort subclass whose
# Write(string) never opens or touches a device. Reads/logging are pure mocks.
$ErrorActionPreference = 'Stop'
$collectorPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\capture_diagnostic_telemetry.ps1'))
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($collectorPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
foreach ($name in @('ConvertFrom-ThyTelemetryLine', 'Send-ThyTelemetryCommand')) {
    $found = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true))
    if ($found.Count -ne 1) { throw ('Expected one function: ' + $name) }
    Invoke-Expression $found[0].Extent.Text
}
Add-Type -TypeDefinition @'
public sealed class ThyOfflineSerialPort : System.IO.Ports.SerialPort {
    public string LastValue;
    public byte[] LastBytes;
    public int WriteCalls;
    public new void Write(string value) {
        LastValue = value;
        LastBytes = Encoding.GetBytes(value);
        WriteCalls++;
    }
}
'@
$script:events = [Collections.Generic.List[object]]::new()
function Write-ThyTelemetryEvent([string]$Kind, [object]$Data) {
    $script:events.Add([pscustomobject]@{ kind = $Kind; data = $Data })
}
function Read-ThyConsoleLine([IO.Ports.SerialPort]$SerialPort) {
    if ($SerialPort.IsOpen) { throw 'Offline fake must never be open' }
    return 'OK THYDIAG/1 commands=help,status,startup,touch,adc,midi,core-test,nor,internal writes=none'
}
$serialProbe = [ThyOfflineSerialPort]::new()
try {
    if ($serialProbe.Encoding.WebName -ne 'us-ascii' -or $serialProbe.IsOpen) { throw 'Unexpected SerialPort defaults' }
    $null = Send-ThyTelemetryCommand $serialProbe 'help' 'OK THYDIAG/1 '
    $helpHex = [BitConverter]::ToString($serialProbe.LastBytes).Replace('-', '').ToLowerInvariant()
    if ($helpHex -ne '68656c700a' -or $serialProbe.LastValue.Length -ne 5 -or $serialProbe.WriteCalls -ne 1) {
        throw ('Actual send function produced unexpected help bytes: ' + $helpHex)
    }
    foreach ($command in @('startup', 'status', 'core-test', 'touch', 'i2c', 'sensors', 'controls', 'led status', 'adc', 'midi')) {
        $null = Send-ThyTelemetryCommand $serialProbe $command 'OK THYDIAG/1 '
        if ($serialProbe.LastValue -cne ($command + "`n")) { throw 'Wrong LF command encoding' }
        if ($serialProbe.LastBytes.Count -ne $command.Length + 1) { throw 'Unexpected padding/BOM/terminator' }
    }
    foreach ($forbidden in @('nor 00000000 256', 'internal 08040000 256', "help`nstatus", 'reset', '',
        'controls clear', 'led off', 'led chase', 'led rgb 1 1 1', 'led all', 'pads start', 'pads stop',
        'LED STATUS', 'led status extra', 'controls ', "controls`nled chase")) {
        $before = $serialProbe.WriteCalls; $rejected = $false
        try { $null = Send-ThyTelemetryCommand $serialProbe $forbidden 'OK THYDIAG/1 ' } catch { $rejected = $true }
        if (-not $rejected -or $serialProbe.WriteCalls -ne $before) { throw 'Disallowed command reached Write' }
    }
    if ($serialProbe.IsOpen) { throw 'Offline test unexpectedly opened a port' }
    [pscustomobject]@{ result = 'PASS'; actual_send_function_help_hex = $helpHex; encoding = $serialProbe.Encoding.WebName; fake_write_calls = $serialProbe.WriteCalls; device_opened = $serialProbe.IsOpen; scope = 'Derived SerialPort Write(string) captures bytes only; no PnP or device API called.' } | ConvertTo-Json
} finally { $serialProbe.Dispose() }
