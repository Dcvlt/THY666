# Receive/observe the integrated instrument alpha. No update, panic, reset,
# memory, storage, pad-acquisition or output commands are permitted here.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(10,7200)][int]$DurationSeconds=60,
    [ValidateRange(1,10)][int]$PollSeconds=5,
    [switch]$IncludePadTelemetry,
    [ValidateRange(0,95)][int[]]$WatchPads=@(0,1,2,48,49,90),
    [string]$SamplingControlFile=''
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{16}$'


function Assert-ThyInstrumentGreeting([string]$Line) {
    if($Line -cne 'HELLO THYINSTRUMENT/1 alpha; type help') {throw 'Unexpected alpha greeting; no commands sent'}
}

function ConvertFrom-ThyInstrumentReply([string]$Line,[string]$Command) {
    if($Command -cmatch '^velocity (?:[0-9]|[1-8][0-9]|9[0-5])$') {
        $names=@('id','logical','mode','override','soft','hard','curve','minimum','maximum','last','peak','sample_ms')
        $fields=ConvertFrom-ThyInstrumentNumericFields $Line 'VELOCITY ' $names @()
        if($fields.id -ne [int]$Command.Substring(9) -or $fields.logical -gt 95 -or $fields.mode -gt 1 -or $fields.override -gt 1 -or $fields.soft -ge $fields.hard -or $fields.hard -gt 4095 -or $fields.curve -gt 2 -or $fields.minimum -lt 1 -or $fields.minimum -gt $fields.maximum -or $fields.maximum -gt 127 -or $fields.last -gt 127 -or $fields.peak -gt 4095) {throw 'Invalid velocity observation range or identity'}
        return $fields
    }
    if($Command -cmatch '^pad (?:[0-9]|[1-8][0-9]|9[0-5])$') {
        $names=@('id','logical','raw','baseline','delta','signed_delta','attack','release','profile','held','blocked','warmup','attack_count','release_count','pressure','sample_ms','transition_ms')
        $fields=ConvertFrom-ThyInstrumentNumericFields $Line 'PAD ' $names @('signed_delta','attack','release')
        if($fields.id -ne [int]$Command.Substring(4) -or $fields.logical -gt 95 -or $fields.raw -gt 4095 -or $fields.baseline -gt 4095 -or $fields.delta -gt 4095 -or [Math]::Abs($fields.signed_delta) -gt 4095 -or $fields.profile -gt 2 -or $fields.held -gt 1 -or $fields.blocked -gt 1 -or $fields.warmup -gt 8 -or $fields.pressure -gt 127 -or $fields.attack_count -gt 2 -or $fields.release_count -gt 2) {throw 'Invalid per-pad field range or identity'}
        return $fields
    }
    if($Command -cmatch '^edges (?:0|[1-9][0-9]{0,9})$') {return ConvertFrom-ThyInstrumentEdges $Line}
    switch -CaseSensitive ($Command) {
        'help' {
            $legacy='OK THYINSTRUMENT/1 commands=status,pads,panic,help storage_writes=none'
            $sensors='OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help storage_writes=none'
            $settings='OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle'
            if($Line -ceq $settings) {return @{commands='status,pads,pad,edges,io,panic,help,settings';storage_writes='none';autosave='idle'}}
            if($Line -cne $legacy -and $Line -cne $sensors) {throw 'Unexpected or incomplete instrument help'}
            return @{commands=$(if($Line -ceq $sensors) {'status,pads,pad,edges,io,panic,help'} else {'status,pads,panic,help'});storage_writes='none'}
        }
        'status' {
            # Layout names contain spaces. Match named fields and the final
            # entire layout string, never split the reply on whitespace.
            $pattern='^STATUS THYINSTRUMENT/1 build=(?<build>[0-9a-f]{16}) ready=(?<ready>[01]) uptime_ms=(?<uptime_ms>[0-9]{1,10}) errors=(?<errors>[0-9]{1,10}) presses=(?<presses>[0-9]{1,10}) releases=(?<releases>[0-9]{1,10}) voices=(?<voices>[0-9]{1,10}) layout=(?<layout>[ -~]{1,512})$'
            $match=[regex]::Match($Line,$pattern,[Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if(-not $match.Success) {throw 'Malformed or incomplete instrument status'}
            $result=@{build=$match.Groups['build'].Value;layout=$match.Groups['layout'].Value}
            foreach($name in @('ready','uptime_ms','errors','presses','releases','voices')) {
                $value=[uint32]0
                if(-not [uint32]::TryParse($match.Groups[$name].Value,[ref]$value)) {throw ('Invalid uint32 '+$name)}
                $result[$name]=$value
            }
            if($result.layout -match '(?:^| )[A-Za-z_][A-Za-z0-9_]*=') {throw 'Ambiguous extra field in layout'}
            return $result
        }
        'pads' {
            $match=[regex]::Match($Line,'^PADS frames=(?<frames>[0-9]{1,10}) scans=(?<scans>[0-9]{1,10}) errors=(?<errors>[0-9]{1,10}) profile=150us-21MHz velocity=(?<velocity>fixed[1-9][0-9]{0,2}|strike|mixed) calibration=unverified$')
            if(-not $match.Success) {throw 'Malformed or incomplete instrument pads observation'}
            $velocityTag=$match.Groups['velocity'].Value
            if($velocityTag.StartsWith('fixed',[StringComparison]::Ordinal) -and [int]$velocityTag.Substring(5) -gt 127) {throw 'Invalid fixed velocity'}
            $result=@{profile='150us-21MHz';velocity=$velocityTag;calibration='unverified'}
            foreach($name in @('frames','scans','errors')) {
                $value=[uint32]0
                if(-not [uint32]::TryParse($match.Groups[$name].Value,[ref]$value)) {throw ('Invalid uint32 '+$name)}
                $result[$name]=$value
            }
            return $result
        }
        'io' {
            $io=ConvertFrom-ThyInstrumentNumericFields $Line 'IO ' @('din_ready','din_errors','din_queued','din_tx','din_accept','usb_ready','usb_mounted','usb_accepted','din_events','usb_dropped','transport_fault','core_queued','core_pending','din_pending','usb_pending','held','blocked','warmed','led_errors','led_frames','led_fault') @()
            foreach($name in @('din_ready','usb_ready','usb_mounted','transport_fault','led_fault')) {if($io[$name] -gt 1) {throw 'Invalid I/O flag'}}
            foreach($name in @('held','blocked','warmed')) {if($io[$name] -gt 96) {throw 'Invalid I/O sensor count'}}
            return $io
        }
        default {throw 'Command is outside the instrument observation allowlist'}
    }
}

function ConvertFrom-ThyInstrumentNumericFields([string]$Line,[string]$Prefix,[string[]]$Names,[string[]]$Signed) {
    if(-not $Line.StartsWith($Prefix,[StringComparison]::Ordinal)) {throw 'Unexpected instrument observation prefix'}
    $parts=$Line.Substring($Prefix.Length).Split(' ')
    if($parts.Count -ne $Names.Count) {throw 'Missing or extra observation fields'}
    $result=@{}
    for($i=0;$i -lt $Names.Count;$i++) {
        $name=$Names[$i];$prefixField=$name+'='
        if(-not $parts[$i].StartsWith($prefixField,[StringComparison]::Ordinal)) {throw 'Unexpected observation field order/name'}
        $valueText=$parts[$i].Substring($prefixField.Length);$value=[long]0
        if($valueText -cnotmatch '^-?[0-9]{1,10}$' -or -not [long]::TryParse($valueText,[ref]$value) -or $value -gt [uint32]::MaxValue -or $value -lt -32768 -or ($name -cin $Signed -and $value -gt 32767) -or ($name -cnotin $Signed -and $valueText.StartsWith('-',[StringComparison]::Ordinal))) {throw 'Observation integer out of range'}
        $result[$name]=$value
    }
    return $result
}

function ConvertFrom-ThyInstrumentEdges([string]$Line) {
    $parts=[regex]::Split($Line,' data=')
    if($parts.Count -ne 2) {throw 'Malformed transition journal framing'}
    $fields=ConvertFrom-ThyInstrumentNumericFields $parts[0] 'EDGES ' @('oldest','newest','overwritten','count','next') @()
    if($fields.count -gt 8 -or $fields.oldest -gt $fields.newest -or $fields.next -gt $fields.newest) {throw 'Invalid journal bounds'}
    $entries=@()
    if($fields.count -eq 0) {if($parts[1] -cne '-') {throw 'Empty journal has data'}}
    else {
        $rows=$parts[1].Split(';')
        if($rows.Count -ne $fields.count) {throw 'Incomplete transition journal'}
        $names=@('sequence','at_ms','electrical','logical','action','raw','baseline','signed_delta','attack','release','blocked','profile')
        foreach($row in $rows) {
            $values=$row.Split(',')
            if($values.Count -ne $names.Count) {throw 'Incomplete transition entry'}
            $textFields=for($i=0;$i -lt $names.Count;$i++) {$names[$i]+'='+$values[$i]}
            $entry=ConvertFrom-ThyInstrumentNumericFields ('EDGE '+($textFields -join ' ')) 'EDGE ' $names @('signed_delta','attack','release')
            if($entry.sequence -lt $fields.oldest -or $entry.sequence -gt $fields.newest -or $entry.electrical -gt 95 -or $entry.logical -gt 95 -or $entry.action -lt 1 -or $entry.action -gt 3 -or $entry.raw -gt 4095 -or $entry.baseline -gt 4095 -or [Math]::Abs($entry.signed_delta) -gt 4095 -or $entry.blocked -gt 1 -or $entry.profile -gt 2) {throw 'Invalid transition field range'}
            if($entries.Count -gt 0 -and $entry.sequence -ne $entries[-1].sequence+1) {throw 'Nonconsecutive transition batch'}
            $entries+=,$entry
        }
        if($entries[-1].sequence -ne $fields.next) {throw 'Journal cursor does not match its last entry'}
    }
    $fields.entries=$entries
    return $fields
}

function Assert-ThyInstrumentStatus([hashtable]$Fields,[string]$Build,[hashtable]$Previous) {
    if($Fields.build -cne $Build.ToLowerInvariant()) {throw 'Instrument build mismatch; stop observation'}
    if($null -ne $Previous) {
        if($Fields.uptime_ms -le $Previous.uptime_ms) {throw 'Uptime did not advance; reset, wrap or stalled loop'}
        foreach($name in @('presses','releases')) {
            if($Fields[$name] -lt $Previous[$name]) {throw ('Counter decreased: '+$name)}
        }
    }
}

function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {
    $event=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;kind=$Kind;data=$Data}
    $script:eventWriter.WriteLine(($event|ConvertTo-Json -Depth 18 -Compress));$script:eventWriter.Flush()
    if($Kind -eq 'tx-complete') {$script:wireWriter.Write('TX '+$Data.line+"`n")}
    elseif($Kind -eq 'rx') {$script:wireWriter.Write('RX '+$Data.line+"`r`n")}
    elseif($Kind -eq 'partial-rx') {$script:wireWriter.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:wireWriter.Flush()
}

function Send-ThyInstrumentObservation([object]$SerialPort,[string]$Command) {
    $detail=$Command -ceq 'io' -or $Command -cmatch '^(?:pad|velocity) (?:[0-9]|[1-8][0-9]|9[0-5])$' -or $Command -cmatch '^edges (?:0|[1-9][0-9]{0,9})$'
    if($Command -cnotin @('help','status','pads') -and -not $detail) {throw 'Command is outside the instrument observation allowlist'}
    if($Command.StartsWith('edges ',[StringComparison]::Ordinal) -and [long]$Command.Substring(6) -gt [uint32]::MaxValue) {throw 'Journal cursor out of range'}
    if(-not $script:instrumentGreeted) {throw 'Fresh alpha greeting is required before any command'}
    if(($Command -ceq 'pads' -or $detail) -and -not $script:instrumentBound) {throw 'Exact alpha build binding is required before detailed observation'}
    Write-ThyTelemetryEvent 'tx-intent' @{line=$Command}
    $SerialPort.Write($Command+"`n")
    Write-ThyTelemetryEvent 'tx-complete' @{line=$Command}
    return ConvertFrom-ThyInstrumentReply (Read-ThyConsoleLine $SerialPort) $Command
}

function Get-ThyInstrumentMilliseconds {return $script:observationClock.ElapsedMilliseconds}
function Wait-ThyInstrumentPoll([int]$Seconds) {Start-Sleep -Seconds $Seconds}
function Wait-ThyInstrumentMilliseconds([int]$Milliseconds) {if($Milliseconds -gt 0) {Start-Sleep -Milliseconds $Milliseconds}}
function Test-ThyInstrumentStop {return $script:instrumentStopPath -and [IO.File]::Exists($script:instrumentStopPath)}

function ConvertFrom-ThySamplingControl([string]$Json) {
    $value=ConvertFrom-Json -InputObject $Json -ErrorAction Stop
    if($null -eq $value -or $value -is [array] -or $value -is [string] -or $value -is [ValueType]) {throw 'Sampling control must be a JSON object'}
    $names=@($value.PSObject.Properties.Name)
    if($names.Count -ne 3 -or @($names|Where-Object {$_ -cnotin @('phase','watch_pads','poll_ms')}).Count -ne 0) {throw 'Sampling control requires exactly phase, watch_pads and poll_ms'}
    if($value.phase -isnot [string] -or $value.phase -cnotmatch '^[ -~]{1,128}$') {throw 'Sampling phase must be 1..128 printable ASCII characters'}
    if(($value.poll_ms -isnot [int] -and $value.poll_ms -isnot [long]) -or $value.poll_ms -lt 10 -or $value.poll_ms -gt 10000) {throw 'Sampling poll_ms must be an integer from 10 to 10000'}
    if($null -eq $value.watch_pads -or $value.watch_pads -isnot [array] -or $value.watch_pads.Count -gt 96) {throw 'Sampling watch_pads must be an array of at most 96 pad IDs'}
    $ids=@();$seen=@{}
    foreach($id in $value.watch_pads) {
        if(($id -isnot [int] -and $id -isnot [long]) -or $id -lt 0 -or $id -gt 95 -or $seen.ContainsKey([int]$id)) {throw 'Sampling pad IDs must be unique integers from 0 to 95'}
        $seen[[int]$id]=$true;$ids+=,[int]$id
    }
    return @{phase=$value.phase;watch_pads=$ids;poll_ms=[int]$value.poll_ms}
}

function Read-ThySamplingControl([string]$Path) {
    if(-not [IO.File]::Exists($Path)) {throw 'Sampling control file is missing'}
    $info=[IO.FileInfo]::new($Path)
    if(($info.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $info.Length -gt 16384) {throw 'Sampling control must be a regular file of at most 16384 bytes'}
    $json=[IO.File]::ReadAllText($Path)
    $control=ConvertFrom-ThySamplingControl $json
    $control.raw_json=$json
    return $control
}

function Read-ThyInstrumentDetails([object]$SerialPort,[int[]]$Ids,[uint32]$Cursor) {
    $io=Send-ThyInstrumentObservation $SerialPort io
    foreach($id in $Ids) {$null=Send-ThyInstrumentObservation $SerialPort ('pad '+$id)}
    $gap=0L
    # Bounded catch-up: overflow remains explicit; reads never clear the ring.
    for($batch=0;$batch -lt 8;$batch++) {
        $journal=Send-ThyInstrumentObservation $SerialPort ('edges '+$Cursor)
        if($Cursor -gt 0 -and $journal.oldest -gt [long]$Cursor+1) {$gap+=$journal.oldest-([long]$Cursor+1)}
        Write-ThyTelemetryEvent 'transitions' @{cursor_before=$Cursor;journal=$journal}
        $Cursor=[uint32]$journal.next
        if($journal.count -eq 0 -or $journal.next -eq $journal.newest) {break}
    }
    return @{cursor=$Cursor;missed=$gap;io=$io}
}

function Invoke-ThyInstrumentSession([object]$SerialPort,[string]$Build,[int]$Seconds,[int]$Poll,[bool]$Detail=$false,[int[]]$Ids=@(),[string]$ControlPath='') {
    if($ControlPath -and -not $Detail) {throw 'Sampling control requires per-pad telemetry'}
    $script:instrumentGreeted=$false;$script:instrumentBound=$false
    Assert-ThyInstrumentGreeting (Read-ThyConsoleLine $SerialPort)
    $script:instrumentGreeted=$true
    $features=Send-ThyInstrumentObservation $SerialPort 'help'
    $previous=Send-ThyInstrumentObservation $SerialPort 'status'
    Assert-ThyInstrumentStatus $previous $Build $null
    $script:instrumentBound=$true
    if($Detail -and $features.commands -cnotin @('status,pads,pad,edges,io,panic,help','status,pads,pad,edges,io,panic,help,settings')) {throw 'This alpha does not expose per-pad observations'}
    $cursor=[uint32]0;$missed=0L;$lastIo=$null
    if($Detail) {
        $detailState=Read-ThyInstrumentDetails $SerialPort @(0..95) $cursor
        $cursor=$detailState.cursor;$missed+=$detailState.missed;$lastIo=$detailState.io
    }
    $first=$previous;$firstPads=Send-ThyInstrumentObservation $SerialPort 'pads';$previousPads=$firstPads
    $healthy=$previous.ready -eq 1 -and $previous.errors -eq 0 -and $firstPads.errors -eq 0
    if($Detail) {$healthy=$healthy -and $lastIo.din_ready -eq 1 -and $lastIo.din_errors -eq 0 -and $lastIo.transport_fault -eq 0 -and $lastIo.led_errors -eq 0 -and $lastIo.led_fault -eq 0}
    $samples=1;$start=Get-ThyInstrumentMilliseconds
    $controlRaw=$null;$rawSamples=0L;$healthDue=$start+1000;$rawDue=$start
    while((Get-ThyInstrumentMilliseconds)-$start -lt $Seconds*1000 -and -not (Test-ThyInstrumentStop)) {
        if($ControlPath) {
            $control=Read-ThySamplingControl $ControlPath
            if($control.raw_json -cne $controlRaw) {
                Write-ThyTelemetryEvent 'sampling-control' @{phase=$control.phase;watch_pads=$control.watch_pads;poll_ms=$control.poll_ms;raw_json=$control.raw_json}
                $controlRaw=$control.raw_json
                $rawDue=Get-ThyInstrumentMilliseconds
            }
            $cycleStart=Get-ThyInstrumentMilliseconds
            $sampleNow=$cycleStart -ge $rawDue
            $cycleIds=@()
            if($sampleNow) {$cycleIds=@($control.watch_pads);$rawDue=$cycleStart+$control.poll_ms}
            # A sentinel guarantees health checks also run with no watched pads.
            foreach($id in $cycleIds+@(-1)) {
                if((Get-ThyInstrumentMilliseconds) -ge $healthDue) {
                    $current=Send-ThyInstrumentObservation $SerialPort 'status'
                    Assert-ThyInstrumentStatus $current $Build $previous
                    $pads=Send-ThyInstrumentObservation $SerialPort 'pads'
                    if($pads.frames -lt $previousPads.frames -or $pads.scans -lt $previousPads.scans) {throw 'Pad counters decreased; reset or counter wrap'}
                    $detailState=Read-ThyInstrumentDetails $SerialPort @() $cursor
                    $cursor=$detailState.cursor;$missed+=$detailState.missed;$lastIo=$detailState.io
                    $healthy=$healthy -and $current.ready -eq 1 -and $current.errors -eq 0 -and $pads.errors -eq 0 -and $lastIo.din_ready -eq 1 -and $lastIo.din_errors -eq 0 -and $lastIo.transport_fault -eq 0 -and $lastIo.led_errors -eq 0 -and $lastIo.led_fault -eq 0
                    Write-ThyTelemetryEvent 'sample' @{status=$current;pads=$pads;phase=$control.phase}
                    $previous=$current;$previousPads=$pads;$samples++
                    $healthDue=(Get-ThyInstrumentMilliseconds)+1000
                }
                if($id -ge 0) {
                    if(Test-ThyInstrumentStop) {break}
                    $readStart=Get-ThyInstrumentMilliseconds
                    $reading=Send-ThyInstrumentObservation $SerialPort ('pad '+$id)
                    Write-ThyTelemetryEvent 'pad-sample' @{phase=$control.phase;poll_ms=$control.poll_ms;cycle_started_ms=$cycleStart;started_ms=$readStart;completed_ms=(Get-ThyInstrumentMilliseconds);pad=$reading}
                    $rawSamples++
                }
            }
            # Ten millisecond slices keep control/stop and the 1 s health path
            # responsive even when a caller requests a long raw sampling cycle.
            $until=$rawDue
            while((Get-ThyInstrumentMilliseconds) -lt $until -and (Get-ThyInstrumentMilliseconds) -lt $healthDue -and -not (Test-ThyInstrumentStop)) {
                Wait-ThyInstrumentMilliseconds ([int][Math]::Min(10,$until-(Get-ThyInstrumentMilliseconds)))
            }
            continue
        }
        Wait-ThyInstrumentPoll $Poll
        $current=Send-ThyInstrumentObservation $SerialPort 'status'
        Assert-ThyInstrumentStatus $current $Build $previous
        $pads=Send-ThyInstrumentObservation $SerialPort 'pads'
        if($pads.frames -lt $previousPads.frames -or $pads.scans -lt $previousPads.scans) {throw 'Pad counters decreased; reset or counter wrap'}
        $healthy=$healthy -and $current.ready -eq 1 -and $current.errors -eq 0 -and $pads.errors -eq 0
        Write-ThyTelemetryEvent 'sample' @{status=$current;pads=$pads}
        if($Detail) {
            $detailState=Read-ThyInstrumentDetails $SerialPort $Ids $cursor
            $cursor=$detailState.cursor;$missed+=$detailState.missed;$lastIo=$detailState.io
            $healthy=$healthy -and $lastIo.din_ready -eq 1 -and $lastIo.din_errors -eq 0 -and $lastIo.transport_fault -eq 0 -and $lastIo.led_errors -eq 0 -and $lastIo.led_fault -eq 0
        }
        $previous=$current;$previousPads=$pads;$samples++
    }
    if($Detail) {
        $detailState=Read-ThyInstrumentDetails $SerialPort @(0..95) $cursor
        $cursor=$detailState.cursor;$missed+=$detailState.missed;$lastIo=$detailState.io
        $healthy=$healthy -and $lastIo.din_ready -eq 1 -and $lastIo.din_errors -eq 0 -and $lastIo.transport_fault -eq 0 -and $lastIo.led_errors -eq 0 -and $lastIo.led_fault -eq 0
    }
    return @{completed=$true;status_samples=$samples;firmware_checks_passed=($healthy -and $previousPads.frames -gt $firstPads.frames -and $previousPads.scans -gt $firstPads.scans);
        first_status=$first;last_status=$previous;first_pads=$firstPads;last_pads=$previousPads;
        presses_delta=([long]$previous.presses-[long]$first.presses);releases_delta=([long]$previous.releases-[long]$first.releases);
        detailed=$Detail;transition_cursor=$cursor;missed_transitions=$missed;last_io=$lastIo;sampled_raw_reads=$rawSamples;failure=$null}
}

$ErrorActionPreference='Stop'
$collectorPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($collectorPath,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
foreach($name in @('Assert-ThyTelemetryIdentity','Get-ThyLiveCdcBinding','Read-ThyConsoleLine')) {
    $found=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
    if($found.Count -ne 1) {throw ('Expected exactly one shared function '+$name)}
    Invoke-Expression $found[0].Extent.Text
}
$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../analysis/local'))+[IO.Path]::DirectorySeparatorChar
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $outputPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputPath)) {throw 'Output must be a new directory within analysis/local'}
$controlPath=''
if($SamplingControlFile) {
    if(-not $IncludePadTelemetry) {throw 'Sampling control requires IncludePadTelemetry'}
    $controlPath=[IO.Path]::GetFullPath($SamplingControlFile)
    $sessionParent=[IO.Path]::GetDirectoryName($outputPath)+[IO.Path]::DirectorySeparatorChar
    if(-not $controlPath.StartsWith($sessionParent,[StringComparison]::OrdinalIgnoreCase) -or -not $controlPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {throw 'Sampling control must be within the output session parent in analysis/local'}
    $controlAncestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($controlPath))
    while($null -ne $controlAncestor -and ($controlAncestor.FullName+[IO.Path]::DirectorySeparatorChar).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
        if($controlAncestor.Exists -and ($controlAncestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Sampling control ancestors must not be junctions or symbolic links'}
        $controlAncestor=$controlAncestor.Parent
    }
    $null=Read-ThySamplingControl $controlPath
}
$ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($outputPath))
while($null -ne $ancestor -and ($ancestor.FullName+[IO.Path]::DirectorySeparatorChar).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    if($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Output ancestors must not be junctions or symbolic links'}
    $ancestor=$ancestor.Parent
}
$binding=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop|Out-Null
$streams=@();$serialPort=$null;$bindingAfter=$null;$result=@{completed=$false;firmware_checks_passed=$false;failure=$null}
$script:observationClock=[Diagnostics.Stopwatch]::StartNew()
$started=[DateTime]::UtcNow.ToString('o')
try {
    foreach($name in @('events.jsonl','wire.txt')) {
        $stream=[IO.File]::Open((Join-Path $outputPath $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        $streams+=,$stream;$writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
        if($name -eq 'events.jsonl') {$script:eventWriter=$writer} else {$script:wireWriter=$writer}
    }
    Write-ThyTelemetryEvent 'binding-before' $binding
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII;$serialPort.Handshake=[IO.Ports.Handshake]::None
    $serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false;$serialPort.ReadTimeout=200;$serialPort.WriteTimeout=2000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open()
    Write-ThyTelemetryEvent 'opened' @{dtr=$true;baud=115200}
    $script:instrumentStopPath=Join-Path $outputPath '.stop'
    $result=Invoke-ThyInstrumentSession $serialPort $ExpectedBuild $DurationSeconds $PollSeconds $IncludePadTelemetry.IsPresent $WatchPads $controlPath
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThyTelemetryEvent 'binding-after' $bindingAfter
} catch {
    $result.completed=$false;$result.failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$result.failure}}
} finally {
    if($null -ne $serialPort) {
        try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$result.completed=$false;$result.close_failure=$_.Exception.Message}
    }
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()}
    if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $report=[ordered]@{format='thy666.instrument-telemetry';version=1;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;
        port=$Port;expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;expected_build=$ExpectedBuild;
        identity_before=$binding;identity_after=$bindingAfter;requested_duration_seconds=$DurationSeconds;poll_seconds=$PollSeconds;result=$result;
        helper_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
        shared_collector_sha256=(Get-FileHash -LiteralPath $collectorPath -Algorithm SHA256).Hash.ToLowerInvariant();
        include_pad_telemetry=$IncludePadTelemetry.IsPresent;watch_pads=$WatchPads;sampling_control_file=$controlPath;
        allowed_commands=@('help','status','pads','pad 0..95','velocity 0..95','edges uint32','io');scope='CDC observation of an already-running alpha; optional per-pad snapshots and non-destructive bounded journal reads. No panic, memory, output, update, reset or storage commands. Physical UID is PnP-bound; alpha status does not report UID. Journal gaps and overwritten history are explicit.'}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    $stream=[IO.File]::Open((Join-Path $outputPath 'observation.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($report|ConvertTo-Json -Depth 20))} finally {$writer.Dispose();$stream.Dispose()}
}
[pscustomobject]@{path=$outputPath;completed=$result.completed;firmware_checks_passed=$result.firmware_checks_passed;failure=$result.failure}|ConvertTo-Json
if(-not $result.completed) {throw ('Instrument observation incomplete; evidence preserved at '+$outputPath)}
