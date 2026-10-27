# Offline parser/session checks. Import function ASTs only; never open a port.
$ErrorActionPreference='Stop'
foreach($source in @(
    @{path='../tools/observe_ui_feedback.ps1';names=@('ConvertFrom-ThyPolishReply','Send-ThyPolishRead','Invoke-ThyPolishSnapshot')},
    @{path='../tools/capture_instrument_telemetry.ps1';names=@('ConvertFrom-ThyInstrumentReply','ConvertFrom-ThyInstrumentNumericFields','Assert-ThyInstrumentStatus','Assert-ThyInstrumentGreeting')}
)) {
    $tokens=$null;$parseErrors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $source.path),[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($found.Count -ne 1) {throw 'Missing or duplicate observer helper'}
        Invoke-Expression $found[0].Extent.Text
    }
}
$script:assertions=0
function Check([bool]$Value,[string]$Message) {$script:assertions++;if(-not $Value) {throw $Message}}
function Reject([scriptblock]$Action,[string]$Message) {$failed=$false;try {& $Action|Out-Null} catch {$failed=$true};Check $failed $Message}
$saved='SETTINGS version=1 status=saved loaded=1 recovered=0 generation=4 saves=2 errors=0 rejected=0 backend=internal-sectors10-11 updater_preservation=unverified writes=none'
$reply=ConvertFrom-ThyPolishReply $saved settings
Check ($reply.status -ceq 'saved' -and $reply.generation -eq 4 -and $reply.saves -eq 2) 'Settings metadata lost'
Check ($reply.version -eq 1 -and -not $reply.ContainsKey('loaded_version') -and -not $reply.ContainsKey('migration_pending')) 'Legacy v1 observation was reinterpreted'
foreach($bad in @(($saved+' extra=0'),$saved.Replace('version=1','version=2'),$saved.Replace('status=saved','status=unknown'),$saved.Replace('loaded=1','loaded=2'),$saved.Replace('loaded=1','loaded=0'),$saved.Replace('errors=0','errors=1'),$saved.Replace('generation=4','generation=4294967296'),$saved.Replace('saves=2','saves=-1'),$saved.Replace('writes=none','writes=all'),$saved.Replace(' recovered=0',''))) {
    Reject {ConvertFrom-ThyPolishReply $bad settings} 'Malformed or false Saved reply accepted'
}
$savedV2='SETTINGS version=2 status=saved loaded=1 loaded_version=2 migration_pending=0 recovered=0 generation=4 saves=2 errors=0 rejected=0 backend=internal-sectors10-11 updater_preservation=unverified writes=none'
$reply=ConvertFrom-ThyPolishReply $savedV2 settings
Check ($reply.version -eq 2 -and $reply.loaded_version -eq 2 -and $reply.migration_pending -eq 0 -and $reply.generation -eq 4) 'Current v2 metadata lost'
$legacyV2=$savedV2.Replace('loaded_version=2 migration_pending=0','loaded_version=1 migration_pending=1')
$reply=ConvertFrom-ThyPolishReply $legacyV2 settings
Check ($reply.loaded_version -eq 1 -and $reply.migration_pending -eq 1 -and $reply.status -ceq 'saved') 'Valid legacy record pending migration rejected'
$defaultsV2=$savedV2.Replace('status=saved loaded=1 loaded_version=2','status=defaults loaded=0 loaded_version=0')
Check ((ConvertFrom-ThyPolishReply $defaultsV2 settings).loaded_version -eq 0) 'Valid empty v2 journal rejected'
foreach($bad in @(
    ($savedV2+' extra=0'),
    $savedV2.Replace('version=2','version=7'),
    $savedV2.Replace('version=2','version=1'),
    $savedV2.Replace(' loaded_version=2',''),
    $savedV2.Replace(' migration_pending=0',''),
    $savedV2.Replace('loaded_version=2','loaded_version=3'),
    $savedV2.Replace('loaded_version=2','loaded_version=0'),
    $savedV2.Replace('loaded_version=2','loaded_version=1'),
    $savedV2.Replace('migration_pending=0','migration_pending=1'),
    $savedV2.Replace('migration_pending=0','migration_pending=2'),
    $savedV2.Replace('loaded=1','loaded=0'),
    $savedV2.Replace('generation=4','generation=4294967296'),
    $savedV2.Replace('writes=none','writes=all'),
    $legacyV2.Replace('migration_pending=1','migration_pending=0'),
    $defaultsV2.Replace('migration_pending=0','migration_pending=1'),
    $saved.Replace(' recovered=0',' loaded_version=1 migration_pending=1 recovered=0')
)) {Reject {ConvertFrom-ThyPolishReply $bad settings} 'Malformed, future or mixed settings layout accepted'}
foreach($version in @(3,4,5,6)) {
    $current=$savedV2.Replace('version=2 status=',('version='+$version+' status=')).Replace('loaded_version=2',('loaded_version='+$version))
    $reply=ConvertFrom-ThyPolishReply $current settings
    Check ($reply.version -eq $version -and $reply.loaded_version -eq $version -and $reply.migration_pending -eq 0) ('Current v'+$version+' metadata lost')
    foreach($loadedVersion in 1..($version-1)) {
        $legacy=$current.Replace(('loaded_version='+$version+' migration_pending=0'),('loaded_version='+$loadedVersion+' migration_pending=1'))
        $reply=ConvertFrom-ThyPolishReply $legacy settings
        Check ($reply.loaded_version -eq $loadedVersion -and $reply.migration_pending -eq 1 -and $reply.status -ceq 'saved') ('Valid legacy-to-v'+$version+' migration observation rejected')
        Reject {ConvertFrom-ThyPolishReply $legacy.Replace('migration_pending=1','migration_pending=0') settings} 'Legacy record falsely reported fully migrated'
    }
    $defaults=$current.Replace(('status=saved loaded=1 loaded_version='+$version),'status=defaults loaded=0 loaded_version=0')
    $reply=ConvertFrom-ThyPolishReply $defaults settings
    Check ($reply.version -eq $version -and $reply.loaded_version -eq 0 -and $reply.migration_pending -eq 0) ('Valid empty v'+$version+' journal rejected')
    foreach($bad in @(
        ($current+' extra=0'),
        $current.Replace(('version='+$version+' status='),'version=7 status='),
        $current.Replace((' loaded_version='+$version),''),
        $current.Replace(' migration_pending=0',''),
        $current.Replace(('loaded_version='+$version),'loaded_version=7'),
        $current.Replace(('loaded_version='+$version),'loaded_version=0'),
        $current.Replace('migration_pending=0','migration_pending=1'),
        $current.Replace('loaded=1','loaded=0'),
        $current.Replace('errors=0','errors=1'),
        $current.Replace('generation=4','generation=4294967296'),
        $current.Replace('writes=none','writes=all'),
        $defaults.Replace('migration_pending=0','migration_pending=1')
    )) {Reject {ConvertFrom-ThyPolishReply $bad settings} ('Invalid v'+$version+' migration or Saved metadata accepted')}
}
Reject {ConvertFrom-ThyPolishReply $savedV2.Replace('loaded_version=2','loaded_version=4') settings} 'Newer record accepted by v2 firmware metadata'
Reject {ConvertFrom-ThyPolishReply $savedV2.Replace('version=2 status=','version=3 status=').Replace('loaded_version=2','loaded_version=4') settings} 'Newer record accepted by v3 firmware metadata'
$help='OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle'
Check ((ConvertFrom-ThyPolishReply $help help).autosave -ceq 'idle') 'Settings-aware help rejected'
Reject {ConvertFrom-ThyPolishReply $help.Replace(',settings','') help} 'Incomplete new help accepted'

# Display observations describe transport scheduling, never optical refresh.
# Exact legacy framing remains useful when observing older installed builds.
$displayLegacy='DISPLAY period_ms=42 spi_hz=328125 frames=1 chunks=64 command_bytes=24 data_bytes=1024 busy_deferrals=5 last_started_ms=6 last_completed_ms=7 last_duration_ms=8 max_duration_ms=9 last_render_gap_ms=10 max_render_gap_ms=4294967295 active=1 page=7 column=112 faulted=0'
$reply=ConvertFrom-ThyPolishReply $displayLegacy display
Check ($reply.period_ms -eq 42 -and $reply.column -eq 112 -and -not $reply.ContainsKey('version')) 'Legacy display layout lost or reinterpreted'
$reply=ConvertFrom-ThyPolishReply $displayLegacy.Replace('active=1 page=7 column=112','active=0 page=8 column=0') display
Check ($reply.page -eq 8 -and $reply.active -eq 0) 'Legacy completed-page sentinel rejected'
foreach($bad in @(
    ($displayLegacy+' extra=0'),
    $displayLegacy.Replace('period_ms=42','period_ms=16'),
    $displayLegacy.Replace('spi_hz=328125','spi_hz=656250'),
    $displayLegacy.Replace('column=112','column=111'),
    $displayLegacy.Replace('column=112','column=128'),
    $displayLegacy.Replace('page=7','page=8'),
    $displayLegacy.Replace('page=7','page=9'),
    $displayLegacy.Replace('active=1','active=2'),
    $displayLegacy.Replace('faulted=0','faulted=1'),
    $displayLegacy.Replace('faulted=0','faulted=2'),
    $displayLegacy.Replace('last_duration_ms=8','last_duration_ms=10'),
    $displayLegacy.Replace('last_render_gap_ms=10','last_render_gap_ms=4294967296'),
    $displayLegacy.Replace('frames=1','frames=-1'),
    $displayLegacy.Replace(' frames=1',''),
    $displayLegacy.Replace('frames=1 chunks=64','chunks=64 frames=1')
)) {Reject {ConvertFrom-ThyPolishReply $bad display} 'Malformed or mixed legacy display accepted'}
$displayV2='DISPLAY version=2 period_ms=16 spi_hz=328125 frames=1 chunks=64 command_bytes=24 data_bytes=1024 busy_deferrals=5 last_started_ms=6 last_completed_ms=7 last_duration_ms=8 max_duration_ms=9 last_render_gap_ms=10 max_render_gap_ms=4294967295 active=1 page=7 column=127 faulted=0 submitted=11 coalesced=3 unchanged=2 failed_chunks=0 generation=9 last_generation_age_ms=12 max_generation_age_ms=15 dirty_bytes=18 valid_bytes=1024 initial_complete=1 last_render_us=100 max_render_us=500 last_chunk_us=464 max_chunk_us=800'
$reply=ConvertFrom-ThyPolishReply $displayV2 display
Check ($reply.version -eq 2 -and $reply.period_ms -eq 16 -and $reply.column -eq 127 -and $reply.submitted -eq 11 -and $reply.coalesced -eq 3) 'Current dirty-display counters lost'
Check ($reply.initial_complete -eq 1 -and $reply.valid_bytes -eq 1024 -and $reply.dirty_bytes -eq 18) 'Initial-complete dirty display misinterpreted'
Check ($reply.last_render_us -eq 100 -and $reply.max_render_us -eq 500 -and $reply.last_chunk_us -eq 464 -and $reply.max_chunk_us -eq 800) 'Microsecond display timing lost'
Check ((ConvertFrom-ThyPolishReply $displayV2.Replace('column=127','column=1') display).column -eq 1) 'One-byte dirty span rejected'
$displayInitial=$displayV2.Replace('dirty_bytes=18 valid_bytes=1024 initial_complete=1','dirty_bytes=1024 valid_bytes=0 initial_complete=0')
Check ((ConvertFrom-ThyPolishReply $displayInitial display).initial_complete -eq 0) 'Valid initial display upload rejected'
$displayPartial=$displayV2.Replace('dirty_bytes=18 valid_bytes=1024 initial_complete=1','dirty_bytes=1023 valid_bytes=1 initial_complete=0')
Check ((ConvertFrom-ThyPolishReply $displayPartial display).valid_bytes -eq 1) 'Partially confirmed display rejected'
$displayIdle=$displayV2.Replace('active=1','active=0').Replace('dirty_bytes=18','dirty_bytes=0')
Check ((ConvertFrom-ThyPolishReply $displayIdle display).active -eq 0) 'Valid idle dirty display rejected'
Check ((ConvertFrom-ThyPolishReply $displayIdle.Replace('faulted=0','faulted=1') display).faulted -eq 1) 'Inactive display fault could not be reported'
foreach($bad in @(
    ($displayV2+' extra=0'),
    $displayV2.Replace('version=2','version=1'),
    $displayV2.Replace('version=2','version=3'),
    $displayV2.Replace('version=2','version=0'),
    $displayV2.Replace('version=2','version=4294967296'),
    $displayV2.Replace('version=2 ','version=2 version=2 '),
    $displayV2.Replace(' version=2',''),
    $displayV2.Replace('period_ms=16','period_ms=42'),
    $displayV2.Replace('spi_hz=328125','spi_hz=656250'),
    $displayV2.Replace('page=7','page=8'),
    $displayV2.Replace('column=127','column=128'),
    $displayV2.Replace('active=1','active=2'),
    $displayV2.Replace('faulted=0','faulted=1'),
    $displayV2.Replace('faulted=0','faulted=2'),
    $displayV2.Replace('dirty_bytes=18','dirty_bytes=1025'),
    $displayV2.Replace('valid_bytes=1024','valid_bytes=1025'),
    $displayV2.Replace('valid_bytes=1024','valid_bytes=1023'),
    $displayV2.Replace('initial_complete=1','initial_complete=0'),
    $displayV2.Replace('initial_complete=1','initial_complete=2'),
    $displayInitial.Replace('initial_complete=0','initial_complete=1'),
    $displayV2.Replace('last_generation_age_ms=12','last_generation_age_ms=16'),
    $displayV2.Replace('last_render_us=100','last_render_us=501'),
    $displayV2.Replace('last_chunk_us=464','last_chunk_us=801'),
    $displayV2.Replace('last_duration_ms=8','last_duration_ms=10'),
    $displayV2.Replace('max_render_gap_ms=4294967295','max_render_gap_ms=9'),
    $displayV2.Replace('submitted=11','submitted=4294967296'),
    $displayV2.Replace('failed_chunks=0','failed_chunks=-1'),
    $displayV2.Replace(' last_chunk_us=464',''),
    $displayV2.Replace('submitted=11 coalesced=3','coalesced=3 submitted=11'),
    $displayV2.Replace('DISPLAY','display'),
    $displayV2.Replace(' chunks=64','  chunks=64')
)) {Reject {ConvertFrom-ThyPolishReply $bad display} 'Malformed, mixed or future dirty-display reply accepted'}

function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {}
function Assert-ThyPolishDeadline([object]$SerialPort) {if($script:portClock.ElapsedMilliseconds -ge 10000) {throw 'Mock deadline'}}
function Start-Sleep([int]$Milliseconds) {$script:portClock.ElapsedMilliseconds+=$Milliseconds}
function New-FakePort {
    $p=[pscustomobject]@{Writes=[Collections.Generic.List[string]]::new();IsOpen=$true}
    $p|Add-Member ScriptMethod Write {param([string]$Text) $this.Writes.Add($Text)}
    return $p
}
function Read-ThyConsoleLine([object]$SerialPort) {
    if(-not $SerialPort.Writes.Count) {return 'HELLO THYINSTRUMENT/1 alpha; type help'}
    $command=$SerialPort.Writes[$SerialPort.Writes.Count-1].TrimEnd([char]10)
    if($command -ceq 'status') {return 'STATUS THYINSTRUMENT/1 build='+$script:fakeBuild+' ready=1 uptime_ms='+(1000+$script:portClock.ElapsedMilliseconds)+' errors=0 presses=0 releases=0 voices=0 layout=MAJOR C'}
    if($command -ceq 'settings') {
        $n=[Math]::Min($script:replyIndex,$script:settingsReplies.Count-1)
        $script:replyIndex++
        return $script:settingsReplies[$n]
    }
    throw 'Unexpected test command'
}
$ExpectedBuild='266863f82855bd0a';$script:fakeBuild=$ExpectedBuild
$pending=$saved.Replace('status=saved','status=pending')
$script:settingsReplies=@($pending,$pending,$saved);$script:replyIndex=0
$script:portClock=[pscustomobject]@{ElapsedMilliseconds=0}
$script:snapshotEntries=[Collections.Generic.List[object]]::new();$script:lastSettings=$null;$WaitForSaved=$true
$port=New-FakePort
$null=Invoke-ThyPolishSnapshot $port @('settings')
Check ($script:lastSettings.status -ceq 'saved' -and $script:snapshotEntries.Count -eq 3) 'Bounded Saved poll failed'
Check (($port.Writes -join ',') -ceq "status`n,settings`n,status`n,settings`n,status`n,settings`n,status`n") 'Each settings read needs exact-build status'
$script:settingsReplies=@($legacyV2,$legacyV2,$savedV2);$script:replyIndex=0
$script:portClock.ElapsedMilliseconds=0
$script:snapshotEntries=[Collections.Generic.List[object]]::new();$script:lastSettings=$null
$port=New-FakePort
$null=Invoke-ThyPolishSnapshot $port @('settings')
Check ($script:lastSettings.version -eq 2 -and $script:lastSettings.loaded_version -eq 2 -and $script:lastSettings.migration_pending -eq 0 -and $script:snapshotEntries.Count -eq 3) 'WaitForSaved stopped before legacy-to-v2 migration completed'
Check (($port.Writes -join ',') -ceq "status`n,settings`n,status`n,settings`n,status`n,settings`n,status`n") 'Migration observation lost exact-build read binding'
foreach($version in @(3,4,5,6)) {
    $current=$savedV2.Replace('version=2 status=',('version='+$version+' status=')).Replace('loaded_version=2',('loaded_version='+$version))
    $legacy=$current.Replace(('loaded_version='+$version+' migration_pending=0'),('loaded_version='+($version-1)+' migration_pending=1'))
    $script:settingsReplies=@($legacy,$legacy,$current);$script:replyIndex=0
    $script:portClock.ElapsedMilliseconds=0
    $script:snapshotEntries=[Collections.Generic.List[object]]::new();$script:lastSettings=$null
    $port=New-FakePort
    $null=Invoke-ThyPolishSnapshot $port @('settings')
    Check ($script:lastSettings.version -eq $version -and $script:lastSettings.loaded_version -eq $version -and $script:lastSettings.migration_pending -eq 0 -and $script:snapshotEntries.Count -eq 3) ('WaitForSaved stopped before legacy-to-v'+$version+' migration completed')
    Check (($port.Writes -join ',') -ceq "status`n,settings`n,status`n,settings`n,status`n,settings`n,status`n") ('v'+$version+' migration observation lost exact-build read binding')
}
foreach($command in @('settings save','settings erase','panic','reset','flash','status extra','SETTINGS')) {Reject {Send-ThyPolishRead $port $command} 'Mutation or unknown command accepted'}
$script:fakeBuild='ffffffffffffffff';$port=New-FakePort
Reject {Invoke-ThyPolishSnapshot $port @('settings')} 'Wrong build accepted'
Check ($port.Writes.Count -eq 1) 'Settings sent after wrong build'
$script:fakeBuild=$ExpectedBuild;$script:settingsReplies=@($pending);$script:replyIndex=0;$script:lastSettings=$null
$script:portClock.ElapsedMilliseconds=8000;$port=New-FakePort
Reject {Invoke-ThyPolishSnapshot $port @('settings')} 'Saved deadline ignored'
Check ($port.Writes.Count -eq 2) 'Additional command after bounded wait'
$script:portClock.ElapsedMilliseconds=0;$script:settingsReplies=@($saved.Replace('status=saved','status=unsupported'));$script:replyIndex=0;$script:lastSettings=$null;$port=New-FakePort
Reject {Invoke-ThyPolishSnapshot $port @('settings')} 'Unsupported journal silently accepted'
Check ($port.Writes.Count -eq 2) 'Unsupported format triggered more commands'
Write-Output ('PASS UI feedback observer offline assertions='+$script:assertions)
