# Download-only SONG/1 evidence collector. Never upload, mutate settings or send MIDI.
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


function Get-ThySongMilliseconds { return $script:songClock.ElapsedMilliseconds }
function Write-ThySongEvent([string]$Kind,[object]$Data) {
    $event=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=(Get-ThySongMilliseconds);kind=$Kind;data=$Data}
    $script:songEvents.WriteLine(($event|ConvertTo-Json -Depth 14 -Compress));$script:songEvents.Flush()
    if($Kind -eq 'tx-complete') {$script:songWire.WriteLine('TX '+$Data.line)}
    elseif($Kind -eq 'rx') {$script:songWire.WriteLine('RX '+$Data.line)}
    elseif($Kind -eq 'partial-rx') {$script:songWire.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:songWire.Flush()
}
function Read-ThySongLine([object]$SerialPort,[long]$Deadline) {
    $deadlineMs=[Math]::Min($Deadline,$script:songAbsoluteDeadline)
    $bytes=[Collections.Generic.List[byte]]::new()
    try {
        while((Get-ThySongMilliseconds) -lt $deadlineMs) {
            $remaining=$deadlineMs-(Get-ThySongMilliseconds)
            if($remaining -le 0) {break}
            $SerialPort.ReadTimeout=[int][Math]::Min(200,$remaining)
            try {$value=$SerialPort.ReadByte()} catch [TimeoutException] {continue}
            if((Get-ThySongMilliseconds) -ge $deadlineMs) {throw 'Song reply exceeded its deadline'}
            if($value -lt 0) {throw 'Song serial stream closed'}
            $bytes.Add([byte]$value)
            if($bytes.Count -gt 1024) {throw 'Song reply exceeds the bounded line size'}
            if($value -eq 10) {
                if($bytes.Count -lt 2 -or $bytes[$bytes.Count-2] -ne 13) {throw 'Song reply requires CRLF'}
                foreach($b in $bytes.GetRange(0,$bytes.Count-2)) {if($b -lt 32 -or $b -gt 126) {throw 'Non-ASCII song reply'}}
                $line=[Text.Encoding]::ASCII.GetString($bytes.ToArray(),0,$bytes.Count-2)
                Write-ThySongEvent 'rx' @{line=$line}
                return $line
            }
        }
        throw 'Song command or absolute session deadline exceeded'
    } catch {
        if($bytes.Count) {Write-ThySongEvent 'partial-rx' @{hex=[BitConverter]::ToString($bytes.ToArray()).Replace('-','').ToLowerInvariant()}}
        throw
    }
}
function ConvertFrom-ThySongReply([string]$Line,[string]$Kind) {
    $patterns=@{
        status='^SONG version=(?<version>[0-9]+) state=(?<state>idle|upload|download) bytes=(?<bytes>[0-9]+) received=(?<received>[0-9]+) crc32=(?<crc>[0-9a-fA-F]{8})$'
        export='^SONG export bytes=(?<bytes>[0-9]+) crc32=(?<crc>[0-9a-fA-F]{8})$'
        get='^SONG data offset=(?<offset>[0-9]+) hex=(?<hex>[0-9a-fA-F]{2,64})$'
        abort='^SONG aborted$'
    }
    if(-not $patterns.ContainsKey($Kind)) {throw 'Unknown song reply kind'}
    $m=[regex]::Match($Line,$patterns[$Kind],[Text.RegularExpressions.RegexOptions]::CultureInvariant)
    if(-not $m.Success) {throw 'Malformed, unsupported or unexpected song reply'}
    $r=@{}
    foreach($n in @('version','bytes','received','offset')) {
        if($m.Groups[$n].Success) {
            $v=[uint32]0
            if(-not [uint32]::TryParse($m.Groups[$n].Value,[ref]$v)) {throw 'Song reply integer overflow'}
            $r[$n]=$v
        }
    }
    if($Kind -eq 'status') {
        if($r.version -ne 1) {throw 'Only SONG/1 is supported'}
        $r.state=$m.Groups['state'].Value
        if($r.bytes -gt 8812 -or $r.received -gt $r.bytes) {throw 'Invalid song service bounds'}
    }
    if($Kind -eq 'export' -and ($r.bytes -lt 536 -or $r.bytes -gt 8812)) {throw 'Song export size outside 536..8812 bytes'}
    if($m.Groups['crc'].Success) {$r.crc32=[Convert]::ToUInt32($m.Groups['crc'].Value,16)}
    if($Kind -eq 'get') {
        $r.hex=$m.Groups['hex'].Value
        if($r.hex.Length%2 -or $r.offset -gt 8812) {throw 'Invalid song chunk'}
    }
    return $r
}
function Send-ThySongReadCommand([object]$SerialPort,[string]$Command) {
    if(-not $script:songGreeted) {throw 'Song commands require a fresh HELLO'}
    $kind=''
    if($Command -ceq 'status') {$kind='instrument'}
    elseif($script:songBuildBound) {
        if($Command -ceq 'song status') {$kind='status'}
        elseif($Command -ceq 'song export' -and $script:songIdleChecked -and -not $script:songOwnDownload) {$kind='export';$script:songIdleChecked=$false}
        elseif($Command -ceq 'song abort' -and $script:songOwnDownload) {$kind='abort'}
        elseif($Command -cmatch '^song get (0|[1-9][0-9]{0,3}) ([1-9]|[12][0-9]|3[0-2])$' -and $script:songOwnDownload) {
            $offset=[int]$Matches[1];$count=[int]$Matches[2]
            if($offset -ne $script:songNextOffset -or $offset+$count -gt $script:songExportBytes) {throw 'Song request is not the next bounded chunk'}
            $kind='get'
        }
    }
    if(-not $kind) {throw 'Command outside bound download-only song allowlist'}
    $deadline=[Math]::Min((Get-ThySongMilliseconds)+3000,$script:songAbsoluteDeadline)
    if($deadline -le (Get-ThySongMilliseconds)) {throw 'Absolute song session deadline exceeded before write'}
    $SerialPort.WriteTimeout=[int][Math]::Min(2000,$deadline-(Get-ThySongMilliseconds))
    Write-ThySongEvent 'tx-intent' @{line=$Command}
    $SerialPort.Write($Command+"`n")
    Write-ThySongEvent 'tx-complete' @{line=$Command}
    $line=Read-ThySongLine $SerialPort $deadline
    if($kind -eq 'instrument') {return ConvertFrom-ThyInstrumentReply $line status}
    # A syntactically successful export is owned even if its advertised size is
    # invalid: cleanup may release only the snapshot this command just created.
    if($kind -eq 'export' -and $line -cmatch '^SONG export bytes=[0-9]+ crc32=[0-9a-fA-F]{8}$') {$script:songOwnDownload=$true}
    $r=ConvertFrom-ThySongReply $line $kind
    if($kind -eq 'status') {$script:songIdleChecked=$r.state -ceq 'idle'}
    if($kind -eq 'export') {$script:songOwnDownload=$true;$script:songExportBytes=$r.bytes;$script:songNextOffset=0}
    if($kind -eq 'get') {
        if($r.offset -ne $offset -or $r.hex.Length -ne $count*2) {throw 'Song chunk offset or length differs from request'}
        $script:songNextOffset+=$count
    }
    if($kind -eq 'abort') {$script:songOwnDownload=$false}
    return $r
}
function Get-ThySongCrc32([byte[]]$Bytes,[int]$Offset=0) {
    [uint32]$crc=4294967295
    for($n=$Offset;$n -lt $Bytes.Length;$n++) {
        $crc=$crc -bxor $Bytes[$n]
        for($b=0;$b -lt 8;$b++) {if($crc -band 1) {$crc=($crc -shr 1) -bxor [uint32]3988292384} else {$crc=$crc -shr 1}}
    }
    return [uint32]($crc -bxor [uint32]4294967295)
}
function Get-ThySongU16([byte[]]$Bytes,[int]$At) {return [uint16]($Bytes[$At]+256*$Bytes[$At+1])}
function Get-ThySongU32([byte[]]$Bytes,[int]$At) {return [uint32]($Bytes[$At]+256L*$Bytes[$At+1]+65536L*$Bytes[$At+2]+16777216L*$Bytes[$At+3])}
function Assert-ThySongFile([byte[]]$Bytes,[uint32]$OuterCrc) {
    if($Bytes.Length -lt 536 -or $Bytes.Length -gt 8812 -or [Text.Encoding]::ASCII.GetString($Bytes,0,4) -cne 'THYS') {throw 'Invalid THYS header'}
    $version=Get-ThySongU16 $Bytes 4;$stride=24;if($version -eq 2) {$stride=28}
    $eventStart=32+21*$stride
    if($version -notin @(1,2) -or (Get-ThySongU16 $Bytes 6) -ne 32 -or
        (Get-ThySongU32 $Bytes 8) -ne $Bytes.Length -or (Get-ThySongU16 $Bytes 18) -ne 21 -or (Get-ThySongU32 $Bytes 28) -ne 0) {throw 'Invalid THYS header'}
    $events=Get-ThySongU16 $Bytes 16
    if($events -gt 512 -or $Bytes.Length -ne $eventStart+16*$events) {throw 'THYS event count/size mismatch'}
    if((Get-ThySongCrc32 $Bytes) -ne $OuterCrc -or (Get-ThySongCrc32 $Bytes 16) -ne (Get-ThySongU32 $Bytes 12)) {throw 'THYS outer or internal CRC mismatch'}
    $tracks=@();$counts=New-Object int[] 21
    for($t=0;$t -lt 21;$t++) {
        $o=32+$stride*$t;$end=Get-ThySongU32 $Bytes $o;$start=Get-ThySongU32 $Bytes ($o+4);$loop=Get-ThySongU32 $Bytes ($o+8)
        $grid=240;if($version -eq 2) {$grid=Get-ThySongU32 $Bytes ($o+24)}
        if($start -ge $loop -or $loop -gt $end -or $end -gt 1073741823 -or $Bytes[$o+16] -gt 15 -or $Bytes[$o+17] -lt 1 -or $Bytes[$o+17] -gt 7 -or $Bytes[$o+18] -gt 127 -or $Bytes[$o+19] -gt 1 -or $Bytes[$o+20] -gt 2 -or $Bytes[$o+21] -gt 5 -or $Bytes[$o+22] -gt 1 -or ($version -eq 1 -and ($Bytes[$o+23] -band 128)) -or (($Bytes[$o+23] -band 14) -shr 1) -gt 4 -or -not $grid -or $grid -gt 22369622) {throw 'Invalid THYS track'}
        if($version -eq 2 -and [long]$loop-$start -gt 48L*$grid) {throw 'THYS v2 loop exceeds 48 steps'}
        $tracks+=,@{track=$t+1;content_end=$end;loop_start=$start;loop_end=$loop;channel=$Bytes[$o+16]+1;destinations=$Bytes[$o+17];grid_ticks=$grid;created=[bool]($Bytes[$o+23] -band 128)}
    }
    $ids=@{};$maxId=0;$maxGroup=0
    for($n=0;$n -lt $events;$n++) {
        $o=$eventStart+16*$n;$tick=Get-ThySongU32 $Bytes $o;$duration=Get-ThySongU32 $Bytes ($o+4);$id=Get-ThySongU16 $Bytes ($o+8);$group=Get-ThySongU16 $Bytes ($o+10)
        $track=$Bytes[$o+12];$kind=$Bytes[$o+13] -band 127;$pitch=$Bytes[$o+14];$value=$Bytes[$o+15]
        if($tick -ge 1073741823 -or -not $duration -or $duration -gt 1073741823-$tick -or $track -gt 20 -or $kind -gt 5 -or $pitch -gt 127 -or $value -gt 127 -or ($kind -eq 0 -and -not $value) -or -not $id -or -not $group -or $ids.ContainsKey($id)) {throw 'Invalid THYS event'}
        if([long]$tick+$duration -gt $tracks[$track].content_end) {throw 'THYS event exceeds content'}
        if($version -eq 2 -and -not $tracks[$track].created) {throw 'THYS event belongs to an uncreated track'}
        $ids[$id]=$true;$maxId=[Math]::Max($id,$maxId);$maxGroup=[Math]::Max($group,$maxGroup);$counts[$track]++
    }
    $nextId=Get-ThySongU16 $Bytes 20;$nextGroup=Get-ThySongU16 $Bytes 22
    if(($nextId -and $nextId -le $maxId) -or ($nextGroup -and $nextGroup -le $maxGroup)) {throw 'Invalid THYS next identity'}
    for($t=0;$t -lt 21;$t++) {$tracks[$t].events=$counts[$t]}
    return @{version=$version;bytes=$Bytes.Length;events=$events;revision=(Get-ThySongU32 $Bytes 24);tracks=$tracks;outer_crc32=('{0:x8}' -f $OuterCrc);inner_crc32=('{0:x8}' -f (Get-ThySongU32 $Bytes 12))}
}
function Invoke-ThySongDownload([object]$SerialPort,[string]$Build) {
    $script:songGreeted=$false;$script:songBuildBound=$false;$script:songIdleChecked=$false;$script:songOwnDownload=$false
    $script:songNextOffset=0;$script:songExportBytes=0;$script:songCleanupFailure=$null
    Assert-ThyInstrumentGreeting (Read-ThySongLine $SerialPort ((Get-ThySongMilliseconds)+3000))
    $script:songGreeted=$true
    $first=Send-ThySongReadCommand $SerialPort status
    Assert-ThyInstrumentStatus $first $Build $null
    if($first.ready -ne 1) {throw 'Instrument is not ready'}
    $script:songBuildBound=$true
    $before=Send-ThySongReadCommand $SerialPort 'song status'
    if($before.state -cne 'idle') {throw 'Song service belongs to another client; no export or abort sent'}
    try {
        $export=Send-ThySongReadCommand $SerialPort 'song export'
        $bytes=New-Object byte[] $export.bytes
        for($offset=0;$offset -lt $bytes.Length;$offset+=32) {
            $count=[Math]::Min(32,$bytes.Length-$offset)
            $chunk=Send-ThySongReadCommand $SerialPort ('song get '+$offset+' '+$count)
            for($n=0;$n -lt $count;$n++) {$bytes[$offset+$n]=[Convert]::ToByte($chunk.hex.Substring($n*2,2),16)}
        }
        $metadata=Assert-ThySongFile $bytes $export.crc32
    } finally {
        if($script:songOwnDownload) {
            try {$null=Send-ThySongReadCommand $SerialPort 'song abort'} catch {$script:songCleanupFailure=$_.Exception.Message;Write-ThySongEvent 'cleanup-failure' @{message=$script:songCleanupFailure}}
        }
    }
    if($script:songOwnDownload) {throw 'Could not release our snapshot; nothing published; device staging expires after inactivity'}
    $after=Send-ThySongReadCommand $SerialPort 'song status'
    if($after.state -cne 'idle') {throw 'Song service did not return idle'}
    $last=Send-ThySongReadCommand $SerialPort status
    Assert-ThyInstrumentStatus $last $Build $first
    return @{bytes=$bytes;metadata=$metadata;first_status=$first;last_status=$last;service_before=$before;service_after=$after}
}

$ErrorActionPreference='Stop'
$sharedPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$instrumentPath=Join-Path $PSScriptRoot 'capture_instrument_telemetry.ps1'
foreach($source in @(@{path=$sharedPath;names=@('Assert-ThyTelemetryIdentity','Get-ThyLiveCdcBinding')},@{path=$instrumentPath;names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus')})) {
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source.path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($found.Count -ne 1) {throw ('Expected exactly one reviewed helper '+$name)}
        Invoke-Expression $found[0].Extent.Text
    }
}
$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../analysis/local/instrument'))+[IO.Path]::DirectorySeparatorChar
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $outputPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputPath)) {throw 'Output must be a fresh directory below analysis/local/instrument'}
$ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($outputPath))
while($null -ne $ancestor) {
    if($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Output ancestors must not be junctions or symbolic links'}
    $ancestor=$ancestor.Parent
}
$binding=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop|Out-Null
$script:songClock=[Diagnostics.Stopwatch]::StartNew();$script:songAbsoluteDeadline=45000
$serialPort=$null;$script:songEvents=$null;$script:songWire=$null;$bindingAfter=$null;$download=$null
$result=@{completed=$false;failure=$null};$started=[DateTime]::UtcNow.ToString('o')
try {
    $script:songEvents=[IO.StreamWriter]::new([IO.File]::Open((Join-Path $outputPath 'events.jsonl'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read),[Text.UTF8Encoding]::new($false))
    $script:songWire=[IO.StreamWriter]::new([IO.File]::Open((Join-Path $outputPath 'wire.txt'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read),[Text.UTF8Encoding]::new($false))
    Write-ThySongEvent 'binding-before' $binding
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII;$serialPort.Handshake=[IO.Ports.Handshake]::None
    $serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false;$serialPort.ReadTimeout=200;$serialPort.WriteTimeout=2000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open();Write-ThySongEvent 'opened' @{dtr=$true;baud=115200}
    $download=Invoke-ThySongDownload $serialPort $ExpectedBuild
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThySongEvent 'binding-after' $bindingAfter
    if((Get-ThySongMilliseconds) -ge 45000) {throw 'Absolute song session deadline exceeded before publication'}
    $serialPort.Close();$serialPort.Dispose();$serialPort=$null
    $pendingPath=Join-Path $outputPath 'song.thys.pending'
    $file=[IO.File]::Open($pendingPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$file.Write($download.bytes,0,$download.bytes.Length);$file.Flush($true)} finally {$file.Dispose()}
    # File.Move refuses an existing destination. Failed writes remain explicitly
    # pending, never a published native song.
    [IO.File]::Move($pendingPath,(Join-Path $outputPath 'song.thys'))
    $result.completed=$true;$result.metadata=$download.metadata
    $result.first_status=$download.first_status;$result.last_status=$download.last_status
    $result.song_sha256=(Get-FileHash -LiteralPath (Join-Path $outputPath 'song.thys') -Algorithm SHA256).Hash.ToLowerInvariant()
} catch {
    $result.failure=$_.Exception.Message
    if($null -ne $script:songEvents -and $null -ne $script:songWire) {Write-ThySongEvent 'failure' @{message=$result.failure}}
} finally {
    if($null -ne $serialPort) {try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$result.completed=$false;$result.close_failure=$_.Exception.Message}}
    if($null -ne $script:songEvents) {$script:songEvents.Dispose()}
    if($null -ne $script:songWire) {$script:songWire.Dispose()}
    $report=[ordered]@{format='thy666.sequencer-song-capture';version=1;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=(Get-ThySongMilliseconds);port=$Port;expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;expected_build=$ExpectedBuild;identity_before=$binding;identity_after=$bindingAfter;result=$result;cleanup_failure=$script:songCleanupFailure;helper_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();shared_collector_sha256=(Get-FileHash -LiteralPath $sharedPath -Algorithm SHA256).Hash.ToLowerInvariant();instrument_collector_sha256=(Get-FileHash -LiteralPath $instrumentPath -Algorithm SHA256).Hash.ToLowerInvariant();allowed_commands=@('status','song status','song export','song get offset count<=32','song abort (our acknowledged download only)');scope='Download a pinned song snapshot only. No upload, settings write, MIDI, reset, panic, update or flash command. Physical UID is PnP-bound; firmware status identifies the exact build.'}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    $json=[IO.File]::Open((Join-Path $outputPath 'evidence.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $writer=[IO.StreamWriter]::new($json,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($report|ConvertTo-Json -Depth 20))} finally {$writer.Dispose();$json.Dispose()}
}
[pscustomobject]@{path=$outputPath;completed=$result.completed;failure=$result.failure;song_sha256=$result.song_sha256}|ConvertTo-Json
if(-not $result.completed) {throw ('Song download failed; evidence preserved at '+$outputPath)}
