# Offline only: import definitions, fake every serial byte, never execute the collector entry point.
$ErrorActionPreference='Stop'
foreach($source in @(
    @{path='../tools/capture_sequencer_song.ps1';all=$true},
    @{path='../tools/capture_instrument_telemetry.ps1';names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus')},
    @{path='../tools/capture_diagnostic_telemetry.ps1';names=@('Assert-ThyTelemetryIdentity')}
)) {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $source.path),[ref]$tokens,[ref]$errors)
    if($errors.Count) {throw ($errors.Message -join '; ')}
    $definitions=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true))
    foreach($f in $definitions) {if($source.all -or $f.Name -cin $source.names) {Invoke-Expression $f.Extent.Text}}
}
$script:checks=0
function Check([bool]$Condition,[string]$Message) {$script:checks++;if(-not $Condition) {throw $Message}}
function Reject([scriptblock]$Action,[string]$Message) {$failed=$false;try {& $Action|Out-Null} catch {$failed=$true};Check $failed $Message}
function Set-U16([byte[]]$B,[int]$At,[uint16]$N) {$B[$At]=[byte]($N -band 255);$B[$At+1]=[byte]($N -shr 8)}
function Set-U32([byte[]]$B,[int]$At,[uint32]$N) {for($i=0;$i -lt 4;$i++) {$B[$At+$i]=[byte](($N -shr ($i*8)) -band 255)}}
function Fix-Crc([byte[]]$B) {Set-U32 $B 12 (Get-ThySongCrc32 $B 16)}
function New-Song([int]$Count=1,[int]$Version=1) {
    $stride=24;if($Version -eq 2) {$stride=28};$start=32+21*$stride
    $b=New-Object byte[] ($start+16*$Count)
    [Text.Encoding]::ASCII.GetBytes('THYS').CopyTo($b,0);Set-U16 $b 4 $Version;Set-U16 $b 6 32;Set-U32 $b 8 $b.Length
    Set-U16 $b 16 $Count;Set-U16 $b 18 21;Set-U16 $b 20 ($Count+1);Set-U16 $b 22 ($Count+1)
    for($t=0;$t -lt 21;$t++) {$o=32+$t*$stride;Set-U32 $b $o 3840;Set-U32 $b ($o+8) 3840;$b[$o+16]=$t%16;$b[$o+17]=5;if($Version -eq 2) {Set-U32 $b ($o+24) 240;if($t -eq 0) {$b[$o+23]=128}}}
    for($n=0;$n -lt $Count;$n++) {$o=$start+$n*16;Set-U32 $b ($o+4) 240;Set-U16 $b ($o+8) ($n+1);Set-U16 $b ($o+10) ($n+1);$b[$o+14]=60;$b[$o+15]=96}
    Fix-Crc $b;return ,$b
}
function Get-ThySongMilliseconds {return $script:fakeClock}
function Write-ThySongEvent([string]$Kind,[object]$Data) {$script:fakeEvents.Add(@{kind=$Kind;data=$Data})}
function Queue-Line([object]$P,[string]$Line) {foreach($b in [Text.Encoding]::ASCII.GetBytes($Line+"`r`n")) {$P.Queue.Enqueue($b)}}
function New-FakePort([string]$Mode='ok') {
    $script:fakeClock=0;$script:songAbsoluteDeadline=45000;$script:fakeEvents=[Collections.Generic.List[object]]::new()
    $p=[pscustomobject]@{Queue=[Collections.Generic.Queue[byte]]::new();Writes=[Collections.Generic.List[string]]::new();Mode=$Mode;ReadTimeout=200;WriteTimeout=2000;Statuses=0;Blob=(New-Song);Service='idle'}
    $p|Add-Member ScriptMethod ReadByte {
        if(-not $this.Queue.Count) {$script:fakeClock+=$this.ReadTimeout;throw [TimeoutException]::new('fake read timeout')}
        $script:fakeClock++;return [int]$this.Queue.Dequeue()
    }
    $p|Add-Member ScriptMethod Write {
        param([string]$Text)
        $command=$Text.TrimEnd([char]10);$this.Writes.Add($command)
        if($this.Mode -eq 'write-timeout') {throw [TimeoutException]::new('fake write timeout')}
        if($command -ceq 'status') {
            $this.Statuses++;$build='7edc4e435b4de4cf';if($this.Mode -eq 'wrong-build') {$build='0000000000000000'}
            Queue-Line $this ('STATUS THYINSTRUMENT/1 build='+$build+' ready=1 uptime_ms='+(1000+$this.Statuses*100)+' errors=0 presses=0 releases=0 voices=0 layout=MAJOR C')
        } elseif($command -ceq 'song status') {
            $state=$this.Service;if($this.Mode -eq 'busy') {$state='upload'}
            $version=1;if($this.Mode -eq 'version') {$version=2}
            Queue-Line $this ('SONG version='+$version+' state='+$state+' bytes=0 received=0 crc32=00000000')
        } elseif($command -ceq 'song export') {
            if($this.Mode -eq 'export-error') {Queue-Line $this 'ERR song-busy';return}
            if($this.Mode -eq 'malformed-export') {Queue-Line $this 'SONG export garbage';return}
            $size=$this.Blob.Length;if($this.Mode -eq 'oversize') {$size=8813}
            $this.Service='download';$crc=Get-ThySongCrc32 $this.Blob;if($this.Mode -eq 'outer-crc') {$crc=$crc -bxor 1}
            Queue-Line $this ('SONG export bytes='+$size+' crc32='+('{0:x8}' -f $crc))
        } elseif($command -cmatch '^song get ([0-9]+) ([0-9]+)$') {
            $offset=[int]$Matches[1];$count=[int]$Matches[2]
            if($this.Mode -eq 'timeout') {return}
            if($this.Mode -eq 'absolute-timeout') {$script:fakeClock=45000;return}
            if($this.Mode -eq 'get-error') {Queue-Line $this 'ERR song-offset';return}
            $hex=[BitConverter]::ToString($this.Blob,$offset,$count).Replace('-','').ToLowerInvariant()
            if($this.Mode -eq 'offset') {$offset++}
            if($this.Mode -eq 'short-chunk') {$hex=$hex.Substring(2)}
            Queue-Line $this ('SONG data offset='+$offset+' hex='+$hex)
        } elseif($command -ceq 'song abort') {$this.Service='idle';Queue-Line $this 'SONG aborted'}
        else {throw ('Unexpected fake command '+$command)}
    }
    $banner='HELLO THYINSTRUMENT/1 alpha; type help';if($Mode -eq 'greeting') {$banner='HELLO THYDIAG/1 diagnostic; type help'}
    Queue-Line $p $banner
    return $p
}
$build='7edc4e435b4de4cf'
Check ((Get-ThySongCrc32 ([Text.Encoding]::ASCII.GetBytes('123456789'))) -eq [uint32]3421780262) 'IEEE CRC vector differs'
$blob=New-Song
$decoded=Assert-ThySongFile $blob (Get-ThySongCrc32 $blob)
Check ($decoded.events -eq 1 -and $decoded.tracks[0].events -eq 1 -and $decoded.tracks[0].channel -eq 1) 'Valid native song rejected'
foreach($change in @('magic','version','length','tracks','reserved','inner-crc','event-id','event-type','event-end','track-route','quantize','duplicate-id','next-id')) {
    $bad=New-Song 2
    switch($change) {
        magic {$bad[0]=0};version {Set-U16 $bad 4 3};length {Set-U32 $bad 8 536};tracks {Set-U16 $bad 18 22}
        reserved {$bad[28]=1};inner-crc {$bad[12]=$bad[12] -bxor 1}
        event-id {Set-U16 $bad 544 0};event-type {$bad[549]=6};event-end {Set-U32 $bad 536 3840}
        track-route {$bad[49]=0};quantize {$bad[55]=10};duplicate-id {Set-U16 $bad 560 1};next-id {Set-U16 $bad 20 1}
    }
    if($change -ne 'inner-crc') {Fix-Crc $bad}
    Reject {Assert-ThySongFile $bad (Get-ThySongCrc32 $bad)} ('Invalid THYS accepted: '+$change)
}
$p=New-FakePort;$result=Invoke-ThySongDownload $p $build
Check ($result.metadata.events -eq 1 -and [Convert]::ToBase64String($result.bytes) -ceq [Convert]::ToBase64String($p.Blob)) 'Successful fake download differs'
Check ($p.Writes[0] -ceq 'status' -and $p.Writes[1] -ceq 'song status' -and $p.Writes[2] -ceq 'song export') 'Identity/service ordering changed'
Check (@($p.Writes|Where-Object {$_ -ceq 'song abort'}).Count -eq 1 -and $p.Service -ceq 'idle') 'Own snapshot not released exactly once'
Check (@($p.Writes|Where-Object {$_ -cnotmatch '^(status|song status|song export|song get [0-9]+ ([1-9]|[12][0-9]|3[0-2])|song abort)$'}).Count -eq 0) 'Download issued an unauthorized command'
foreach($mode in @('greeting','wrong-build','busy','version','export-error','malformed-export','write-timeout')) {
    $p=New-FakePort $mode
    Reject {Invoke-ThySongDownload $p $build} ('Bad session accepted: '+$mode)
    Check (@($p.Writes|Where-Object {$_ -ceq 'song abort'}).Count -eq 0) ('Aborted someone else or an unacknowledged export: '+$mode)
    if($mode -cin @('greeting','wrong-build','busy','version')) {Check (@($p.Writes|Where-Object {$_ -ceq 'song export'}).Count -eq 0) ('Export before binding: '+$mode)}
}
foreach($mode in @('oversize','outer-crc','offset','short-chunk','get-error','timeout')) {
    $p=New-FakePort $mode
    Reject {Invoke-ThySongDownload $p $build} ('Invalid download accepted: '+$mode)
    Check (@($p.Writes|Where-Object {$_ -ceq 'song abort'}).Count -eq 1) ('Owned failed download not cleaned up: '+$mode)
    if($mode -eq 'timeout') {Check ($script:fakeClock -lt 6000) 'Per-command timeout was not bounded'}
}
$p=New-FakePort;$p.Blob[12]=$p.Blob[12] -bxor 1
Reject {Invoke-ThySongDownload $p $build} 'Inner CRC failure with valid outer CRC accepted'
Check (@($p.Writes|Where-Object {$_ -ceq 'song abort'}).Count -eq 1) 'Inner CRC error leaked our snapshot'
$p=New-FakePort 'absolute-timeout'
Reject {Invoke-ThySongDownload $p $build} 'Absolute timeout accepted'
Check ($script:fakeClock -le 45000 -and @($p.Writes|Where-Object {$_ -ceq 'song abort'}).Count -eq 0) 'Cleanup exceeded absolute deadline'
Check ($script:songCleanupFailure -and $script:songOwnDownload) 'Expired session did not record cleanup limitation'
foreach($line in @('SONG version=2 state=idle bytes=0 received=0 crc32=00000000','SONG version=1 state=idle bytes=8813 received=0 crc32=00000000','SONG version=1 state=idle bytes=4 received=5 crc32=00000000','SONG version=1 state=idle bytes=4294967296 received=0 crc32=00000000','SONG version=1 state=idle bytes=0 received=0 crc32=00000000 extra=1')) {Reject {ConvertFrom-ThySongReply $line status} 'Malformed service accepted'}
$p=New-FakePort;$script:songGreeted=$true;$script:songBuildBound=$true;$script:songOwnDownload=$false;$script:songIdleChecked=$false
foreach($command in @('song begin 536 0','song put 0 00','song commit','settings','panic','reset','midi','song abort','song export','song get 0 32','status extra')) {Reject {Send-ThySongReadCommand $p $command} ('Allowlist accepted '+$command)}
Check ($p.Writes.Count -eq 0) 'Rejected commands reached fake wire'
$script:songOwnDownload=$true;$script:songExportBytes=552;$script:songNextOffset=0
foreach($command in @('song get 0 0','song get 0 33','song get 01 32','song get 32 32','song get 0 32 extra')) {Reject {Send-ThySongReadCommand $p $command} ('Unsafe chunk request accepted '+$command)}
Check ($p.Writes.Count -eq 0) 'Invalid owned chunk reached wire'
foreach($raw in @("hello`n",(("x"*1025)+"`r`n"),(([string][char]1)+"`r`n"))) {
    $p=New-FakePort;$p.Queue.Clear();foreach($b in [Text.Encoding]::ASCII.GetBytes($raw)) {$p.Queue.Enqueue($b)}
    Reject {Read-ThySongLine $p 3000} ('Invalid serial framing accepted: '+[BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes($raw)).Substring(0,[Math]::Min(32,$raw.Length*3-1)))
}
$p=New-FakePort;$p.Blob=New-Song 512;$r=Invoke-ThySongDownload $p $build
Check ($r.bytes.Length -eq 8728 -and $r.metadata.events -eq 512 -and $script:fakeClock -lt 45000) 'Maximum size download failed'
$p=New-FakePort;$p.Blob=New-Song 512 2;$r=Invoke-ThySongDownload $p $build
Check ($r.bytes.Length -eq 8812 -and $r.metadata.version -eq 2 -and $r.metadata.events -eq 512 -and $r.metadata.tracks[0].created -and $r.metadata.tracks[0].grid_ticks -eq 240) 'Maximum v2 download failed'
foreach($change in @('zero-grid','large-grid','missing-track','too-many-steps')) {
    $bad=New-Song 1 2
    switch($change) {zero-grid {Set-U32 $bad 56 0};large-grid {Set-U32 $bad 56 22369623};missing-track {$bad[55]=0};too-many-steps {Set-U32 $bad 56 1}}
    Fix-Crc $bad
    Reject {Assert-ThySongFile $bad (Get-ThySongCrc32 $bad)} ('Invalid v2 song accepted: '+$change)
}
Write-Output ("Offline song collector: $script:checks assertions passed; no device accessed")
