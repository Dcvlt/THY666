# Explicit one-shot, volatile musical panic for the integrated alpha only.
# No updater, memory, storage, device reset, synth or host MIDI output APIs.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{16}$'


function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {
    $script:eventWriter.WriteLine((([ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;kind=$Kind;data=$Data})|ConvertTo-Json -Depth 16 -Compress));$script:eventWriter.Flush()
    if($Kind -eq 'tx-complete') {$script:wireWriter.Write('TX '+$Data.line+"`n")}
    elseif($Kind -eq 'rx') {$script:wireWriter.Write('RX '+$Data.line+"`r`n")}
    elseif($Kind -eq 'partial-rx') {$script:wireWriter.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:wireWriter.Flush()
}

function Invoke-ThyInstrumentPanicSession([object]$SerialPort,[string]$Build) {
    $script:instrumentGreeted=$false;$script:instrumentBound=$false
    $script:panicAttempted=$false;$script:panicAcknowledged=$false
    Assert-ThyInstrumentGreeting (Read-ThyConsoleLine $SerialPort)
    $script:instrumentGreeted=$true
    $before=Send-ThyInstrumentObservation $SerialPort status
    Assert-ThyInstrumentStatus $before $Build $null
    $script:instrumentBound=$true
    # A separate explicit action: the read-only observer's command allowlist
    # remains unchanged. Mark the sole attempt before writing, never retry it.
    if(-not $script:instrumentBound -or $script:panicAttempted) {throw 'Fresh exact alpha binding and one panic attempt are required'}
    Write-ThyTelemetryEvent 'tx-intent' @{line='panic'}
    $script:panicAttempted=$true
    $SerialPort.Write("panic`n")
    Write-ThyTelemetryEvent 'tx-complete' @{line='panic'}
    $ack=Read-ThyConsoleLine $SerialPort
    if($ack -cne 'OK panic') {throw 'Panic was not acknowledged exactly; do not retry'}
    $script:panicAcknowledged=$true
    $after=Send-ThyInstrumentObservation $SerialPort status
    Assert-ThyInstrumentStatus $after $Build $null
    if($after.voices -ne 0) {throw 'Panic acknowledged but sounding voices remain; do not retry'}
    Write-ThyTelemetryEvent 'panic-verified' @{before=$before;after=$after}
    return @{before=$before;after=$after;voices_zero_confirmed=$true}
}

$ErrorActionPreference='Stop'
$sharedPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$instrumentPath=Join-Path $PSScriptRoot 'capture_instrument_telemetry.ps1'
foreach($source in @(@{path=$sharedPath;names=@('Assert-ThyTelemetryIdentity','Get-ThyLiveCdcBinding','Read-ThyConsoleLine')},@{path=$instrumentPath;names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus','Send-ThyInstrumentObservation')})) {
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source.path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($found.Count -ne 1) {throw ('Expected one reviewed helper '+$name)}
        Invoke-Expression $found[0].Extent.Text
    }
}
$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../analysis/local'))+[IO.Path]::DirectorySeparatorChar
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $outputPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputPath)) {throw 'Output must be a new directory inside analysis/local'}
$ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($outputPath))
while($null -ne $ancestor -and ($ancestor.FullName+[IO.Path]::DirectorySeparatorChar).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    if($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Output ancestors must not be junctions or symbolic links'}
    $ancestor=$ancestor.Parent
}
$binding=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop|Out-Null
$streams=@();$serialPort=$null;$bindingAfter=$null;$failure=$null;$result=$null;$completed=$false
$script:panicAttempted=$false;$script:panicAcknowledged=$false
$script:observationClock=[Diagnostics.Stopwatch]::StartNew();$started=[DateTime]::UtcNow.ToString('o')
try {
    foreach($name in @('events.jsonl','wire.txt')) {
        $stream=[IO.File]::Open((Join-Path $outputPath $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);$streams+=,$stream
        $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
        if($name -eq 'events.jsonl') {$script:eventWriter=$writer} else {$script:wireWriter=$writer}
    }
    Write-ThyTelemetryEvent 'binding-before' $binding
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII;$serialPort.Handshake=[IO.Ports.Handshake]::None
    $serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false;$serialPort.ReadTimeout=200;$serialPort.WriteTimeout=2000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open()
    $result=Invoke-ThyInstrumentPanicSession $serialPort $ExpectedBuild
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThyTelemetryEvent 'binding-after' $bindingAfter
    $completed=$true
} catch {
    $failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$failure}}
} finally {
    if($null -ne $serialPort) {try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$completed=$false;$failure='CDC close failed: '+$_.Exception.Message}}
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()};if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $report=[ordered]@{format='thy666.instrument-panic';version=1;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;
        completed=$completed;failure=$failure;panic_attempted=$script:panicAttempted;panic_acknowledged=$script:panicAcknowledged;result=$result;
        port=$Port;expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;expected_build=$ExpectedBuild;identity_before=$binding;identity_after=$bindingAfter;
        scope='Exactly one explicit volatile alpha musical panic after fresh HELLO/build/PnP binding, followed by sounding-voices-zero verification. No retry, device reset, arbitrary memory, storage, update, host MIDI output or synth command.'}
    foreach($pair in @(@('helper',$PSCommandPath),@('shared_collector',$sharedPath),@('instrument_collector',$instrumentPath))) {$report[$pair[0]+'_sha256']=(Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash.ToLowerInvariant()}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    $stream=[IO.File]::Open((Join-Path $outputPath 'observation.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($report|ConvertTo-Json -Depth 18))} finally {$writer.Dispose();$stream.Dispose()}
}
[pscustomobject]@{path=$outputPath;completed=$completed;panic_attempted=$script:panicAttempted;panic_acknowledged=$script:panicAcknowledged;failure=$failure}|ConvertTo-Json
if(-not $completed) {throw ('One-shot panic observation incomplete; do not repeat automatically; evidence preserved at '+$outputPath)}
