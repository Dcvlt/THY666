# Read-only configuration capture from an already-running instrument image.
# No updater, reset, sensor configuration values, sensor commands or NVM writes.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(10,60)][int]$CaptureTimeoutSeconds=15
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
    $script:eventWriter.WriteLine(($event|ConvertTo-Json -Depth 20 -Compress));$script:eventWriter.Flush()
    if($Kind -eq 'tx-complete') {$script:wireWriter.Write('TX '+$Data.line+"`n")}
    elseif($Kind -eq 'rx') {$script:wireWriter.Write('RX '+$Data.line+"`r`n")}
    elseif($Kind -eq 'partial-rx') {$script:wireWriter.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:wireWriter.Flush()
}

function ConvertFrom-ThyTouchReply([string]$Line,[string]$Command) {
    if($Command -ceq 'help touch') {
        if($Line -cne 'OK TOUCH/1 commands=touch;touch capture MASK;touch bank BANK;touch page BANK OFFSET;display mask=1..15 bank=0..3 offset=0,16,32,48,64,80,96,112 config_writes=none') {throw 'Unexpected touch readback capability reply'}
        return @{version=1;config_writes='none'}
    }
    if($Command -ceq 'touch') {
        $match=[regex]::Match($Line,'^TOUCH center_poll_ms=(?<center_poll_ms>[0-9]{1,10}) default_center_poll_ms=8 top_poll_ms=20 ready=(?<ready>[0-9]{1,2}) failed=(?<failed>[0-9]{1,2}) capture_pending=(?<capture_pending>[0-9]{1,2}) capture_complete=(?<capture_complete>[0-9]{1,2}) capture_failed=(?<capture_failed>[0-9]{1,2}) uid=(?<uid>[0-9a-f]{24}) config_bytes=128 writes=none$')
        if(-not $match.Success) {throw 'Malformed touch summary'}
        $result=@{uid=$match.Groups['uid'].Value;config_bytes=128;writes='none'}
        foreach($name in @('center_poll_ms','ready','failed','capture_pending','capture_complete','capture_failed')) {
            $value=[uint32]0;if(-not [uint32]::TryParse($match.Groups[$name].Value,[ref]$value)) {throw 'Touch summary integer overflow'}
            $result[$name]=$value
        }
        if($result.center_poll_ms -lt 8 -or $result.center_poll_ms -gt 80) {throw 'Invalid center polling interval'}
        foreach($name in @('ready','failed','capture_pending','capture_complete','capture_failed')) {if($result[$name] -gt 15) {throw 'Invalid touch mask'}}
        if(($result.capture_pending -band $result.capture_complete) -or ($result.capture_pending -band $result.capture_failed) -or ($result.capture_complete -band $result.capture_failed)) {throw 'Overlapping capture states'}
        return $result
    }
    if($Command -ceq 'touch capture 15') {
        if($Line -cne 'TOUCH_CAPTURE mask=15 result=0 reads_only=1') {throw 'Read-only snapshot request was not accepted; do not retry automatically'}
        return @{mask=15;result=0;reads_only=1}
    }
    if($Command -cmatch '^touch bank [0-3]$') {
        $pattern='^TOUCH_BANK bank=(?<bank>[0-3]) address=(?<address>[0-9]{1,3}) state=(?<state>[0-4]) result=(?<result>-?[0-9]{1,10}) family=(?<family>[0-9]{1,3}) device=(?<device>[0-9]{1,5}) revision=(?<revision>[0-9]{1,3}) attempts=(?<attempts>[0-9]{1,10}) started_ms=(?<started_ms>[0-9]{1,10}) completed_ms=(?<completed_ms>[0-9]{1,10}) valid_bytes=(?<valid_bytes>[0-9]{1,3}) stored_crc_valid=(?<stored_crc_valid>[01]) stored_crc16=(?<stored_crc16>[0-9]{1,5}) capture_crc32=(?<capture_crc32>[0-9a-f]{8}) failed_register=(?<failed_register>[0-9]{1,3})$'
        $match=[regex]::Match($Line,$pattern)
        if(-not $match.Success) {throw 'Malformed touch bank metadata'}
        $result=@{capture_crc32=$match.Groups['capture_crc32'].Value}
        foreach($name in @('bank','address','state','family','device','revision','attempts','started_ms','completed_ms','valid_bytes','stored_crc_valid','stored_crc16','failed_register')) {
            $value=[uint32]0;if(-not [uint32]::TryParse($match.Groups[$name].Value,[ref]$value)) {throw 'Bank metadata integer overflow'}
            $result[$name]=$value
        }
        $result.result=[long]$match.Groups['result'].Value
        if($result.bank -ne [int]$Command.Substring(11) -or $result.address -ne @(54,53,57,56)[$result.bank] -or $result.family -gt 255 -or $result.device -gt 65535 -or $result.revision -gt 255 -or $result.valid_bytes -gt 128 -or $result.stored_crc16 -gt 65535 -or $result.failed_register -gt 255 -or $result.result -lt -4 -or $result.result -gt 0) {throw 'Touch bank identity/range mismatch'}
        if($result.state -eq 3 -and ($result.result -ne 0 -or $result.family -ne 154 -or $result.device -ne 2565 -or $result.revision -ne 1 -or $result.valid_bytes -ne 128 -or $result.stored_crc_valid -ne 1)) {throw 'Incomplete or unsupported controller marked COMPLETE'}
        if($result.state -ne 3 -and $result.capture_crc32 -cne '00000000') {throw 'Unverified capture has a provenance checksum'}
        return $result
    }
    if($Command -cmatch '^touch page (?<bank>[0-3]) (?<offset>0|16|32|48|64|80|96|112)$') {
        $wantedBank=[int]$Matches['bank'];$wantedOffset=[int]$Matches['offset']
        $match=[regex]::Match($Line,'^TOUCH_PAGE bank=(?<bank>[0-3]) offset=(?<offset>0|16|32|48|64|80|96|112) state=(?<state>[0-4]) valid=(?<valid>[0-9a-f]{4}) data=(?<data>[0-9a-f]{32}) crc32=(?<crc32>[0-9a-f]{8})$')
        if(-not $match.Success -or [int]$match.Groups['bank'].Value -ne $wantedBank -or [int]$match.Groups['offset'].Value -ne $wantedOffset) {throw 'Malformed or mismatched touch page'}
        return @{bank=$wantedBank;offset=$wantedOffset;state=[int]$match.Groups['state'].Value;valid=$match.Groups['valid'].Value;data=$match.Groups['data'].Value;crc32=$match.Groups['crc32'].Value}
    }
    throw 'Command is outside the touch readback allowlist'
}

function ConvertFrom-ThyTouchHex([string]$Hex) {
    if($Hex -cnotmatch '^(?:[0-9a-f]{2})+$') {throw 'Invalid byte hex'}
    $bytes=[byte[]]::new($Hex.Length/2)
    for($i=0;$i -lt $bytes.Length;$i++) {$bytes[$i]=[Convert]::ToByte($Hex.Substring($i*2,2),16)}
    return ,$bytes
}

function Get-ThyTouchCaptureCrc32([string]$Uid,[hashtable]$Bank,[byte[]]$Bytes) {
    if($Uid -cnotmatch '^[0-9a-f]{24}$' -or $Bytes.Length -ne 128) {throw 'Invalid provenance input'}
    $data=[Collections.Generic.List[byte]]::new()
    $data.AddRange([byte[]]@(84,72,89,84,1))
    for($i=0;$i -lt 3;$i++) {
        $word=[Convert]::ToUInt32($Uid.Substring($i*8,8),16)
        for($shift=0;$shift -lt 32;$shift+=8) {$data.Add([byte](($word -shr $shift) -band 255))}
    }
    $data.AddRange([byte[]]@($Bank.bank,$Bank.address,$Bank.family,($Bank.device -band 255),(($Bank.device -shr 8) -band 255),$Bank.revision))
    $data.AddRange($Bytes)
    $crc=[uint64]4294967295
    foreach($value in $data) {
        $crc=$crc -bxor [uint64]$value
        for($bit=0;$bit -lt 8;$bit++) {
            if($crc -band 1) {$crc=($crc -shr 1) -bxor [uint64]3988292384} else {$crc=$crc -shr 1}
        }
    }
    return ('{0:x8}' -f [uint32]($crc -bxor [uint64]4294967295))
}

function Send-ThyTouchCommand([object]$SerialPort,[string]$Command) {
    if(-not $script:touchGreeted) {throw 'Fresh alpha greeting required'}
    $allowed=$Command -cin @('status','help touch','touch','touch capture 15') -or $Command -cmatch '^touch bank [0-3]$' -or $Command -cmatch '^touch page [0-3] (?:0|16|32|48|64|80|96|112)$'
    if(-not $allowed -or ($Command -cne 'status' -and -not $script:touchBound)) {throw 'Exact build binding and read-only allowlist required'}
    Write-ThyTelemetryEvent 'tx-intent' @{line=$Command}
    $SerialPort.Write($Command+"`n")
    Write-ThyTelemetryEvent 'tx-complete' @{line=$Command}
    $line=Read-ThyConsoleLine $SerialPort
    if($Command -ceq 'status') {return ConvertFrom-ThyInstrumentReply $line status}
    return ConvertFrom-ThyTouchReply $line $Command
}

function Write-ThyTouchJson([string]$Path,[object]$Value) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($Value|ConvertTo-Json -Depth 24))} finally {$writer.Dispose();$stream.Dispose()}
}

function Save-ThyTouchPartial([object]$SerialPort,[hashtable]$Bank,[string]$Uid,[string]$Destination) {
    $pages=@()
    for($offset=0;$offset -lt 128;$offset+=16) {
        $pages+=,(Send-ThyTouchCommand $SerialPort ('touch page '+$Bank.bank+' '+$offset))
    }
    $record=[ordered]@{format='thy666.touch-configuration-partial';version=1;complete=$false;uid=$Uid;metadata=$Bank;pages=$pages;
        scope='Failed bank only; raw byte placeholders and per-page validity retained. Not a verified original, not an apply/restore image.'}
    Write-ThyTouchJson (Join-Path $Destination ('bank'+$Bank.bank+'-partial.json')) $record
}

function Invoke-ThyTouchReadback([object]$SerialPort,[string]$Build,[string]$Uid,[string]$Destination,[int]$Timeout) {
    $script:touchGreeted=$false;$script:touchBound=$false
    Assert-ThyInstrumentGreeting (Read-ThyConsoleLine $SerialPort);$script:touchGreeted=$true
    $before=Send-ThyTouchCommand $SerialPort status
    Assert-ThyInstrumentStatus $before $Build $null;$script:touchBound=$true
    $null=Send-ThyTouchCommand $SerialPort 'help touch'
    $summary=Send-ThyTouchCommand $SerialPort touch
    if($summary.uid -cne $Uid.ToLowerInvariant()) {throw 'Sensor snapshot UID disagrees with physical USB identity'}
    $script:touchRequestAttempted=$true
    $null=Send-ThyTouchCommand $SerialPort 'touch capture 15'
    $clock=[Diagnostics.Stopwatch]::StartNew();$banks=@()
    do {
        $banks=@();$allComplete=$true
        for($bank=0;$bank -lt 4;$bank++) {
            $metadata=Send-ThyTouchCommand $SerialPort ('touch bank '+$bank);$banks+=,$metadata
            Write-ThyTelemetryEvent 'touch-bank' $metadata
            if($metadata.state -eq 4) {
                Save-ThyTouchPartial $SerialPort $metadata $summary.uid $Destination
                throw ('Sensor snapshot failed at bank '+$bank+' register '+$metadata.failed_register+' result '+$metadata.result)
            }
            if($metadata.state -ne 3) {$allComplete=$false}
        }
        if($allComplete) {break}
        if($clock.Elapsed.TotalSeconds -ge $Timeout) {throw 'Timed out waiting for complete controller snapshots'}
        Start-Sleep -Milliseconds 150
    } while($true)
    $saved=@()
    for($bank=0;$bank -lt 4;$bank++) {
        $metadata=$banks[$bank];$hex='';$pages=@()
        for($offset=0;$offset -lt 128;$offset+=16) {
            $page=Send-ThyTouchCommand $SerialPort ('touch page '+$bank+' '+$offset)
            $pages+=,$page
            if($page.state -ne 3 -or $page.valid -cne 'ffff' -or $page.crc32 -cne $metadata.capture_crc32) {throw 'Page is partial or belongs to a changed capture'}
            $hex+=$page.data
        }
        $bytes=ConvertFrom-ThyTouchHex $hex
        $calculated=Get-ThyTouchCaptureCrc32 $summary.uid $metadata $bytes
        if($calculated -cne $metadata.capture_crc32) {throw 'Independent host provenance CRC32 mismatch'}
        if(($bytes[126]+256*$bytes[127]) -ne $metadata.stored_crc16) {throw 'Stored sensor CRC bytes disagree with metadata'}
        $afterBank=Send-ThyTouchCommand $SerialPort ('touch bank '+$bank)
        if($afterBank.state -ne 3 -or $afterBank.capture_crc32 -cne $metadata.capture_crc32 -or $afterBank.started_ms -ne $metadata.started_ms -or $afterBank.completed_ms -ne $metadata.completed_ms) {throw 'Snapshot changed during pagination'}
        $binaryPath=Join-Path $Destination ('bank'+$bank+'-original.bin')
        $stream=[IO.File]::Open($binaryPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        try {$stream.Write($bytes,0,$bytes.Length)} finally {$stream.Dispose()}
        $record=[ordered]@{format='thy666.touch-configuration-snapshot';version=1;complete=$true;uid=$summary.uid;expected_build=$Build.ToLowerInvariant();bank=$bank;metadata=$metadata;pages=$pages;
            bytes_hex=$hex;bytes=@($bytes);byte_count=128;all_bytes_valid=$true;reserved_bytes_preserved=$true;provenance_crc32=$calculated;stored_sensor_crc16_verified=$false;
            binary_sha256=(Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant();scope='Read-only register snapshot with original stored CRC16 preserved; separate UID/controller-bound CRC32 verified on host. No apply/restore or sensor flash operation.'}
        $jsonPath=Join-Path $Destination ('bank'+$bank+'-original.json');Write-ThyTouchJson $jsonPath $record
        $saved+=,@{bank=$bank;binary_path=$binaryPath;json_path=$jsonPath;bytes=128;crc32=$calculated;sha256=$record.binary_sha256}
    }
    $after=Send-ThyTouchCommand $SerialPort status
    Assert-ThyInstrumentStatus $after $Build $null
    if($after.uptime_ms -lt $before.uptime_ms -or $after.presses -lt $before.presses -or $after.releases -lt $before.releases) {throw 'Instrument reset during readback'}
    return @{complete=$true;before=$before;after=$after;summary=$summary;banks=$saved}
}

$ErrorActionPreference='Stop'
$sharedPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$instrumentPath=Join-Path $PSScriptRoot 'capture_instrument_telemetry.ps1'
foreach($source in @(@{path=$sharedPath;names=@('Assert-ThyTelemetryIdentity','Get-ThyLiveCdcBinding','Read-ThyConsoleLine')},@{path=$instrumentPath;names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus')})) {
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source.path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw 'Existing identity/console helper does not parse'}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
        if($found.Count -ne 1) {throw ('Expected one existing helper '+$name)}
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
$streams=@();$serialPort=$null;$bindingAfter=$null;$result=$null;$failure=$null;$completed=$false
$script:touchRequestAttempted=$false;$script:touchGreeted=$false;$script:touchBound=$false
$script:eventWriter=$null;$script:wireWriter=$null
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
    $result=Invoke-ThyTouchReadback $serialPort $ExpectedBuild $ExpectedSerial $outputPath $CaptureTimeoutSeconds
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThyTelemetryEvent 'binding-after' $bindingAfter;$completed=$true
} catch {
    $failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$failure}}
} finally {
    if($null -ne $serialPort) {try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$completed=$false;$failure='CDC close failed: '+$_.Exception.Message}}
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()};if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $report=[ordered]@{format='thy666.touch-readback-session';version=1;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;
        complete=$completed;failure=$failure;capture_request_attempted=$script:touchRequestAttempted;result=$result;port=$Port;expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;expected_build=$ExpectedBuild;
        identity_before=$binding;identity_after=$bindingAfter;allowed_commands=@('status','help touch','touch','touch capture 15','touch bank 0..3','touch page bank offset');scope='Read-only configuration capture. No sensor values, sensor commands, NVM writes, resets, updater or MIDI output.'}
    foreach($pair in @(@('helper',$PSCommandPath),@('identity_helper',$sharedPath),@('instrument_helper',$instrumentPath))) {$report[$pair[0]+'_sha256']=(Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash.ToLowerInvariant()}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    Write-ThyTouchJson (Join-Path $outputPath 'observation.json') $report
}
[pscustomobject]@{path=$outputPath;complete=$completed;failure=$failure}|ConvertTo-Json
if(-not $completed) {throw ('Touch configuration readback incomplete; evidence preserved at '+$outputPath)}
