# Bounded, read-only observation of an already-running instrument alpha.
# No updater, MIDI output, reset, panic, capture request or configuration writes.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('help','help touch','display','touch','io','pads','settings')]
    [string[]]$Commands=@('settings','display','io'),
    [switch]$WaitForSaved
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{16}$'


function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {
    $event=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;kind=$Kind;data=$Data}
    $script:eventWriter.WriteLine(($event|ConvertTo-Json -Depth 22 -Compress));$script:eventWriter.Flush()
    if($Kind -eq 'tx-complete') {$script:wireWriter.Write('TX '+$Data.line+"`n")}
    elseif($Kind -eq 'rx') {$script:wireWriter.Write('RX '+$Data.line+"`r`n")}
    elseif($Kind -eq 'partial-rx') {$script:wireWriter.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:wireWriter.Flush()
}

function ConvertFrom-ThyPolishReply([string]$Line,[string]$Command) {
    if($Command -ceq 'settings') {
        # Separate exact layouts: a future version, mixed fields or truncated
        # migration metadata must never be mistaken for a known Saved reply.
        $v1='^SETTINGS version=1 status=(?<status>defaults|saved|pending|saving|error|unsupported) loaded=(?<loaded>[01]) recovered=(?<recovered>[01]) generation=(?<generation>[0-9]{1,10}) saves=(?<saves>[0-9]{1,10}) errors=(?<errors>[0-9]{1,10}) rejected=(?<rejected>[0-9]{1,10}) backend=internal-sectors10-11 updater_preservation=unverified writes=none$'
        # Versions 2, 3, 4 and 5 share the exact metadata layout emitted by
        # firmware/instrument/protocol.c. Keep the supported-version allowlist
        # closed; the migration checks below bind loaded_version to version.
        $modern='^SETTINGS version=(?<version>[23456]) status=(?<status>defaults|saved|pending|saving|error|unsupported) loaded=(?<loaded>[01]) loaded_version=(?<loaded_version>[0-6]) migration_pending=(?<migration_pending>[01]) recovered=(?<recovered>[01]) generation=(?<generation>[0-9]{1,10}) saves=(?<saves>[0-9]{1,10}) errors=(?<errors>[0-9]{1,10}) rejected=(?<rejected>[0-9]{1,10}) backend=internal-sectors10-11 updater_preservation=unverified writes=none$'
        $version=1
        $match=[regex]::Match($Line,$v1,[Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if(-not $match.Success) {
            $match=[regex]::Match($Line,$modern,[Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if($match.Success) {$version=[int]$match.Groups['version'].Value}
        }
        if(-not $match.Success) {throw 'Malformed or incomplete settings observation'}
        $fields=@{version=$version;status=$match.Groups['status'].Value;backend='internal-sectors10-11';updater_preservation='unverified';writes='none'}
        $numeric=@('loaded','recovered','generation','saves','errors','rejected')
        if($version -ge 2) {$numeric+=@('loaded_version','migration_pending')}
        foreach($name in $numeric) {
            $value=[uint32]0
            if(-not [uint32]::TryParse($match.Groups[$name].Value,[ref]$value)) {throw ('Invalid settings uint32 '+$name)}
            $fields[$name]=$value
        }
        if($version -ge 2 -and (
            ($fields.loaded -eq 0 -and ($fields.loaded_version -ne 0 -or $fields.migration_pending -ne 0)) -or
            ($fields.loaded -eq 1 -and ($fields.loaded_version -eq 0 -or
                $fields.loaded_version -gt $version -or
                ($fields.loaded_version -lt $version -and $fields.migration_pending -ne 1) -or
                ($fields.loaded_version -eq $version -and $fields.migration_pending -ne 0))))) {throw 'Settings loaded version and migration metadata disagree'}
        if($fields.status -ceq 'saved' -and ($fields.loaded -ne 1 -or $fields.errors -ne 0)) {throw 'Saved observation lacks a valid committed record'}
        return $fields
    }
    if($Command -ceq 'help') {
        if($Line -cne 'OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle') {throw 'Unexpected or incomplete settings-aware instrument help'}
        return @{commands='status,pads,pad,edges,io,panic,help,settings';storage_writes='none';autosave='idle'}
    }
    if($Command -ceq 'display') {
        # Separate closed wire layouts: legacy frozen frames and current dirty
        # drains have different periods, coordinates and counter semantics.
        $names=@(
            'period_ms','spi_hz','frames','chunks','command_bytes','data_bytes','busy_deferrals',
            'last_started_ms','last_completed_ms','last_duration_ms','max_duration_ms',
            'last_render_gap_ms','max_render_gap_ms','active','page','column','faulted')
        $modern=$Line.StartsWith('DISPLAY version=',[StringComparison]::Ordinal)
        if($modern) {
            $names=@('version')+$names+@(
                'submitted','coalesced','unchanged','failed_chunks','generation',
                'last_generation_age_ms','max_generation_age_ms','dirty_bytes','valid_bytes','initial_complete',
                'last_render_us','max_render_us','last_chunk_us','max_chunk_us')
        }
        $fields=ConvertFrom-ThyInstrumentNumericFields $Line 'DISPLAY ' $names @()
        if($fields.spi_hz -ne 328125 -or $fields.active -gt 1 -or $fields.faulted -gt 1 -or
            ($fields.active -eq 1 -and $fields.faulted -ne 0) -or
            $fields.last_duration_ms -gt $fields.max_duration_ms -or
            $fields.last_render_gap_ms -gt $fields.max_render_gap_ms) {throw 'Invalid display observation constants, coordinates or timing bounds'}
        if($modern) {
            if($fields.version -ne 2 -or $fields.period_ms -ne 16 -or
                $fields.page -gt 7 -or $fields.column -gt 127 -or
                $fields.dirty_bytes -gt 1024 -or $fields.valid_bytes -gt 1024 -or
                $fields.initial_complete -gt 1 -or
                ($fields.initial_complete -eq 1) -ne ($fields.valid_bytes -eq 1024) -or
                $fields.last_generation_age_ms -gt $fields.max_generation_age_ms -or
                $fields.last_render_us -gt $fields.max_render_us -or
                $fields.last_chunk_us -gt $fields.max_chunk_us) {throw 'Invalid version 2 dirty-display observation'}
        } elseif($fields.period_ms -ne 42 -or $fields.page -gt 8 -or
            $fields.column -gt 112 -or $fields.column % 16 -ne 0 -or
            ($fields.page -eq 8 -and ($fields.active -ne 0 -or $fields.column -ne 0))) {throw 'Invalid legacy frozen-display observation'}
        return $fields
    }
    if($Command -cin @('touch','help touch')) {return ConvertFrom-ThyTouchReply $Line $Command}
    if($Command -cin @('status','help','io','pads')) {return ConvertFrom-ThyInstrumentReply $Line $Command}
    throw 'Command is outside the polish snapshot read-only allowlist'
}

function Assert-ThyPolishDeadline([object]$SerialPort) {
    if($script:portDeadline.Expired -or $script:portClock.ElapsedMilliseconds -ge 10000 -or -not $SerialPort.IsOpen) {
        throw 'Ten-second CDC snapshot deadline reached; no additional command sent'
    }
}

function Send-ThyPolishRead([object]$SerialPort,[string]$Command) {
    if($Command -cnotin @('status','help','help touch','display','touch','io','pads','settings')) {throw 'Command is outside the UI feedback snapshot read-only allowlist'}
    if(-not $script:polishGreeted) {throw 'Fresh instrument greeting required before commands'}
    if($Command -cne 'status' -and -not $script:polishBound) {throw 'Fresh exact-build status required before each observation'}
    Assert-ThyPolishDeadline $SerialPort
    # One fixed handle is retained throughout this short session. PnP is
    # verified before opening and after closing, as in the existing observer;
    # every detail additionally requires a fresh exact-build status reply.
    Write-ThyTelemetryEvent 'tx-intent' @{line=$Command}
    $SerialPort.Write($Command+"`n")
    Write-ThyTelemetryEvent 'tx-complete' @{line=$Command}
    $reply=ConvertFrom-ThyPolishReply (Read-ThyConsoleLine $SerialPort) $Command
    Assert-ThyPolishDeadline $SerialPort
    if($Command -ceq 'status') {
        Assert-ThyInstrumentStatus $reply $ExpectedBuild $null
        if($script:previousStatus -and ($reply.uptime_ms -lt $script:previousStatus.uptime_ms -or
            $reply.presses -lt $script:previousStatus.presses -or $reply.releases -lt $script:previousStatus.releases)) {throw 'Instrument counters decreased during snapshot'}
        $script:previousStatus=$reply;$script:polishBound=$true
    } else {
        # One status authorizes exactly one read-only detail, never later reads.
        $script:polishBound=$false
        if($Command -ceq 'touch' -and $reply.uid -cne $ExpectedSerial.ToLowerInvariant()) {throw 'Touch UID disagrees with physical USB identity'}
    }
    Write-ThyTelemetryEvent 'parsed' @{command=$Command;reply=$reply}
    return $reply
}

function Invoke-ThyPolishSnapshot([object]$SerialPort,[string[]]$Requested) {
    $script:polishGreeted=$false;$script:polishBound=$false;$script:previousStatus=$null
    Assert-ThyInstrumentGreeting (Read-ThyConsoleLine $SerialPort)
    Assert-ThyPolishDeadline $SerialPort
    $script:polishGreeted=$true
    foreach($command in $Requested) {
        $status=Send-ThyPolishRead $SerialPort status
        $reply=Send-ThyPolishRead $SerialPort $command
        $entry=[ordered]@{command=$command;status_before=$status;reply=$reply}
        $script:snapshotEntries.Add($entry)
        if($command -ceq 'settings') {$script:lastSettings=$reply}
    }
    if($WaitForSaved) {
        # Keep the fixed ten-second lifetime. This observes autosave; it never
        # requests a write, commit, erase, reset or configuration change.
        while($null -eq $script:lastSettings -or $script:lastSettings.status -cne 'saved' -or
            ($script:lastSettings.version -ge 2 -and $script:lastSettings.migration_pending -ne 0)) {
            if($null -ne $script:lastSettings -and $script:lastSettings.status -cin @('error','unsupported')) {throw ('Autosave cannot proceed: '+$script:lastSettings.status)}
            if($script:portClock.ElapsedMilliseconds -ge 8000) {throw 'Settings were not Saved within this bounded observation; leave power connected'}
            Start-Sleep -Milliseconds 500
            $status=Send-ThyPolishRead $SerialPort status
            $reply=Send-ThyPolishRead $SerialPort settings
            $script:snapshotEntries.Add([ordered]@{command='settings';status_before=$status;reply=$reply})
            $script:lastSettings=$reply
        }
    }
    return Send-ThyPolishRead $SerialPort status
}

function Write-ThyPolishJson([string]$Path,[object]$Value) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($Value|ConvertTo-Json -Depth 26))} finally {$writer.Dispose();$stream.Dispose()}
}

$ErrorActionPreference='Stop'
if(-not $Commands.Count -or $Commands.Count -gt 7 -or @($Commands|Select-Object -Unique).Count -ne $Commands.Count) {throw 'Choose one to seven unique read-only observations'}
# Import only the named functions, never the executable collector bodies.
$sharedPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$instrumentPath=Join-Path $PSScriptRoot 'capture_instrument_telemetry.ps1'
$touchPath=Join-Path $PSScriptRoot 'read_touch_configuration.ps1'
foreach($source in @(
    @{path=$sharedPath;names=@('Assert-ThyTelemetryIdentity','Get-ThyLiveCdcBinding','Read-ThyConsoleLine')},
    @{path=$instrumentPath;names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','ConvertFrom-ThyInstrumentNumericFields','Assert-ThyInstrumentStatus')},
    @{path=$touchPath;names=@('ConvertFrom-ThyTouchReply')})) {
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source.path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw 'Existing observation helper does not parse'}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
        if($found.Count -ne 1) {throw ('Expected exactly one shared function '+$name)}
        Invoke-Expression $found[0].Extent.Text
    }
}
# A managed timer closes the port even if PnP enumeration or a reply blocks.
# The callback uses reflection only to avoid extra SerialPort assembly loading.
if(-not ('ThyInstrumentPolishDeadline' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Threading;
public sealed class ThyInstrumentPolishDeadline : IDisposable {
    private readonly Timer timer;
    private int expired;
    public bool Expired { get { return Interlocked.CompareExchange(ref expired, 0, 0) != 0; } }
    public ThyInstrumentPolishDeadline(object port) {
        timer = new Timer(delegate(object unused) {
            Interlocked.Exchange(ref expired, 1);
            try { port.GetType().GetMethod("Close", Type.EmptyTypes).Invoke(port, null); } catch { }
        }, null, 10000, Timeout.Infinite);
    }
    public void Dispose() { timer.Dispose(); }
}
'@
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
$streams=@();$serialPort=$null;$bindingAfter=$null;$failure=$null;$completed=$false;$statusAfter=$null
$script:eventWriter=$null;$script:wireWriter=$null;$script:portDeadline=$null;$script:portClock=$null
$script:snapshotEntries=[Collections.Generic.List[object]]::new()
$script:lastSettings=$null
$script:observationClock=[Diagnostics.Stopwatch]::StartNew();$started=[DateTime]::UtcNow.ToString('o');$portElapsed=0L;$deadlineExpired=$false
try {
    foreach($name in @('events.jsonl','wire.txt')) {
        $stream=[IO.File]::Open((Join-Path $outputPath $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);$streams+=,$stream
        $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
        if($name -eq 'events.jsonl') {$script:eventWriter=$writer} else {$script:wireWriter=$writer}
    }
    Write-ThyTelemetryEvent 'binding-before' $binding
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII;$serialPort.Handshake=[IO.Ports.Handshake]::None
    $serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false;$serialPort.ReadTimeout=200;$serialPort.WriteTimeout=1000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open();$script:portClock=[Diagnostics.Stopwatch]::StartNew()
    $script:portDeadline=[ThyInstrumentPolishDeadline]::new($serialPort)
    Write-ThyTelemetryEvent 'opened' @{dtr=$true;baud=115200;maximum_open_ms=10000}
    $statusAfter=Invoke-ThyPolishSnapshot $serialPort $Commands
    Assert-ThyPolishDeadline $serialPort
    $completed=$true
} catch {
    $failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$failure}}
} finally {
    if($null -ne $serialPort) {
        try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$completed=$false;$failure='CDC close failed: '+$_.Exception.Message}
    }
    if($null -ne $script:portClock) {$portElapsed=$script:portClock.ElapsedMilliseconds;$script:portClock.Stop()}
    if($null -ne $script:portDeadline) {$deadlineExpired=$script:portDeadline.Expired;$script:portDeadline.Dispose()}
    if($deadlineExpired -or $portElapsed -ge 10000) {$completed=$false;if(-not $failure) {$failure='Ten-second CDC snapshot deadline reached'}}
    # Re-enumeration after closure cannot prolong ownership of the port.
    try {$bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent;if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'binding-after' $bindingAfter}}
    catch {$completed=$false;if(-not $failure) {$failure='Final physical USB binding failed: '+$_.Exception.Message}}
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()};if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $report=[ordered]@{format='thy666.ui-feedback-snapshot';version=1;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;
        complete=$completed;failure=$failure;port=$Port;port_open_elapsed_ms=$portElapsed;port_deadline_ms=10000;port_deadline_expired=$deadlineExpired;
        expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;expected_build=$ExpectedBuild.ToLowerInvariant();identity_before=$binding;identity_after=$bindingAfter;
        requested_commands=$Commands;wait_for_saved=[bool]$WaitForSaved;last_settings=$script:lastSettings;observations=@($script:snapshotEntries.ToArray());status_after=$statusAfter;
        allowed_commands=@('status','help','help touch','display','touch','io','pads','settings');scope='Read-only status/settings/display/poll-policy observation. No save/erase request, sensor commands, reset, panic, updater or MIDI output. Firmware autosave runs independently.'}
    foreach($pair in @(@('helper',$PSCommandPath),@('identity_helper',$sharedPath),@('instrument_helper',$instrumentPath),@('touch_helper',$touchPath))) {$report[$pair[0]+'_sha256']=(Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash.ToLowerInvariant()}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    Write-ThyPolishJson (Join-Path $outputPath 'snapshot.json') $report
}
[pscustomobject]@{path=$outputPath;complete=$completed;failure=$failure;port_open_elapsed_ms=$portElapsed;observations=$script:snapshotEntries.Count}|ConvertTo-Json
if(-not $completed) {throw ('Polish snapshot incomplete; evidence preserved at '+$outputPath)}
