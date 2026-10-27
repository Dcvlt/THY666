# Offline only. Import pure/scheduling functions, with all device/file hooks mocked.
$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot '../tools/capture_instrument_telemetry.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count) {throw ($errors.Message -join '; ')}
foreach($name in @('ConvertFrom-ThySamplingControl','Assert-ThyInstrumentGreeting','Assert-ThyInstrumentStatus','Invoke-ThyInstrumentSession')) {
    $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
    if($found.Count -ne 1) {throw 'Missing scheduling helper'}
    Invoke-Expression $found[0].Extent.Text
}
$script:assertions=0
function Check([bool]$Condition,[string]$Message) {$script:assertions++;if(-not $Condition) {throw $Message}}
function Reject([scriptblock]$Action) {$bad=$false;try {& $Action|Out-Null} catch {$bad=$true};Check $bad 'Malformed sampling control accepted'}
$valid=ConvertFrom-ThySamplingControl '{"phase":"quiet baseline","watch_pads":[0,48,95],"poll_ms":20}'
Check ($valid.phase -ceq 'quiet baseline' -and $valid.watch_pads.Count -eq 3 -and $valid.poll_ms -eq 20) 'Valid control lost fields'
Check ((ConvertFrom-ThySamplingControl '{"phase":"pause","watch_pads":[],"poll_ms":10000}').watch_pads.Count -eq 0) 'Empty watch set rejected'
foreach($bad in @('null','[]','42','{"phase":"x","watch_pads":[0,0],"poll_ms":20}','{"phase":"x","watch_pads":[96],"poll_ms":20}','{"phase":"x","watch_pads":[1.0],"poll_ms":20}','{"phase":"x","watch_pads":[-1],"poll_ms":20}','{"phase":"x","watch_pads":0,"poll_ms":20}','{"phase":"x","watch_pads":[0],"poll_ms":9}','{"phase":"x","watch_pads":[0],"poll_ms":10001}','{"phase":"x","watch_pads":[0],"poll_ms":20.0}','{"phase":"","watch_pads":[0],"poll_ms":20}','{"phase":"x","watch_pads":[0],"poll_ms":20,"extra":1}','{"phase":"x","watch_pads":[0]}')) {Reject {ConvertFrom-ThySamplingControl $bad}}
$script:clock=0L;$script:events=[Collections.Generic.List[object]]::new();$script:commands=[Collections.Generic.List[object]]::new()
function Get-ThyInstrumentMilliseconds {return $script:clock}
function Wait-ThyInstrumentMilliseconds([int]$Milliseconds) {$script:clock+=$Milliseconds}
function Test-ThyInstrumentStop {return $false}
function Read-ThyConsoleLine([object]$Port) {return 'HELLO THYINSTRUMENT/1 alpha; type help'}
function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {$script:events.Add(@{kind=$Kind;data=$Data;at=$script:clock})}
function Read-ThySamplingControl([string]$Path) {
    if($script:clock -lt 100) {return @{phase='fast';watch_pads=@(0);poll_ms=20;raw_json='fast'}}
    # Long raw intervals must not slow status, I/O or transition drainage.
    return @{phase='slow';watch_pads=@(48);poll_ms=10000;raw_json='slow'}
}
function Send-ThyInstrumentObservation([object]$Port,[string]$Command) {
    $script:commands.Add(@{command=$Command;at=$script:clock});$script:clock++
    switch -CaseSensitive ($Command) {
        help {return @{commands='status,pads,pad,edges,io,panic,help'}}
        status {return @{build='7277522657129cd5';ready=1;errors=0;uptime_ms=$script:clock;presses=0;releases=0;voices=0;layout='MAJOR C'}}
        pads {return @{frames=$script:clock;scans=16*$script:clock;errors=0}}
        default {if($Command -cmatch '^pad ([0-9]+)$') {return @{id=[int]$Matches[1];raw=4;sample_ms=$script:clock}};throw 'Unexpected command in synthetic sampling session'}
    }
}
function Read-ThyInstrumentDetails([object]$Port,[int[]]$Ids,[uint32]$Cursor) {
    $script:commands.Add(@{command='details';at=$script:clock;ids=$Ids});$script:clock++
    return @{cursor=0;missed=0;io=@{din_ready=1;din_errors=0;transport_fault=0;led_errors=0;led_fault=0}}
}
$result=Invoke-ThyInstrumentSession $null '7277522657129cd5' 3 5 $true @(0) 'mock.json'
Check ($result.completed -and $result.firmware_checks_passed -and $result.sampled_raw_reads -gt 3) 'Sampled session did not complete'
$phases=@($script:events|Where-Object {$_.kind -eq 'sampling-control'})
Check ($phases.Count -eq 2 -and $phases[0].data.phase -ceq 'fast' -and $phases[1].data.phase -ceq 'slow') 'Phase transitions not preserved'
$raw=@($script:events|Where-Object {$_.kind -eq 'pad-sample'})
Check ($raw[0].data.pad.id -eq 0 -and $raw[-1].data.pad.id -eq 48) 'Dynamic watch targets not applied'
foreach($sample in $raw) {Check ($sample.data.completed_ms -ge $sample.data.started_ms -and $sample.data.pad.sample_ms -ge $sample.data.started_ms) 'Raw sample timing lost'}
$health=@($script:events|Where-Object {$_.kind -eq 'sample'})
Check ($health.Count -ge 2) 'Long poll interval starved health samples'
for($i=1;$i -lt $health.Count;$i++) {Check (($health[$i].at-$health[$i-1].at) -le 1010) 'Health interval exceeded bounded command overhead'}
$snapshots=@($script:commands|Where-Object {$_.command -eq 'details' -and $_.ids.Count -eq 96})
Check ($snapshots.Count -eq 2) 'Initial/final full-pad snapshots lost'
Check (@($script:commands|Where-Object {$_.command -match 'panic|reset|flash|calibrate'}).Count -eq 0) 'Synthetic sampled session attempted a mutation'
Write-Output ('PASS instrument sampling control offline assertions='+$script:assertions)
