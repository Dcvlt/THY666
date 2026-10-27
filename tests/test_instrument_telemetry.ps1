# Offline only: AST-import pure/session functions and replace every I/O hook.
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '../tools/capture_instrument_telemetry.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count) {throw ($errors.Message -join '; ')}
foreach($name in @('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentNumericFields','ConvertFrom-ThyInstrumentEdges','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus','Send-ThyInstrumentObservation','Test-ThyInstrumentStop','Read-ThyInstrumentDetails','Invoke-ThyInstrumentSession')) {
    $functions=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
    if($functions.Count -ne 1) {throw 'Missing or duplicate helper function'}
    Invoke-Expression $functions[0].Extent.Text
}
$script:assertions=0
function Check([bool]$Condition,[string]$Message) {$script:assertions++;if(-not $Condition) {throw $Message}}
function Reject([scriptblock]$Action,[string]$Message) {$bad=$false;try {& $Action|Out-Null} catch {$bad=$true};Check $bad $Message}
$build='332f075994d2e8ed'
$status='STATUS THYINSTRUMENT/1 build='+$build+' ready=1 uptime_ms=12000 errors=0 presses=3 releases=3 voices=0 layout=C Major (Ionian)'
$fields=ConvertFrom-ThyInstrumentReply $status status
Check ($fields.layout -ceq 'C Major (Ionian)') 'Layout spaces were lost'
Check ($fields.presses -eq 3) 'Numeric field lost'
Assert-ThyInstrumentStatus $fields $build $null
foreach($bad in @($status.Replace(' ready=1',''),$status+' errors=0',$status.Replace('uptime_ms=12000','uptime_ms=4294967296'),$status.Replace('voices=0','voices=-1'),$status.Replace('build='+$build,'build=host-test'),$status.Replace(' ready=1',' ready=2'),$status.Replace(' layout=C Major (Ionian)',' layout='))) {
    Reject {ConvertFrom-ThyInstrumentReply $bad status} 'Malformed/ambiguous/truncated status accepted'
}
Reject {Assert-ThyInstrumentStatus $fields ('f'*16) $null} 'Wrong build accepted'
$next=$fields.Clone();$next.uptime_ms=12001
Assert-ThyInstrumentStatus $next $build $fields
Reject {Assert-ThyInstrumentStatus $fields $build $fields} 'Stalled/reset uptime accepted'
$next.presses=2
Reject {Assert-ThyInstrumentStatus $next $build $fields} 'Press counter reversal accepted'
foreach($bad in @('', 'HELLO THYDIAG/1 diagnostic; type help','HELLO THYINSTRUMENT/1 alpha; type help extra')) {Reject {Assert-ThyInstrumentGreeting $bad} 'Unexpected greeting accepted'}
Assert-ThyInstrumentGreeting 'HELLO THYINSTRUMENT/1 alpha; type help'
$pads='PADS frames=297 scans=4752 errors=0 profile=150us-21MHz velocity=fixed96 calibration=unverified'
Check ((ConvertFrom-ThyInstrumentReply $pads pads).frames -eq 297) 'Pad frame parsing failed'
foreach($velocity in @(1,96,112,127)) {
    Check ((ConvertFrom-ThyInstrumentReply $pads.Replace('fixed96',('fixed'+$velocity)) pads).velocity -ceq ('fixed'+$velocity)) 'Configured fixed velocity was lost'
}
foreach($velocity in @('0','128','999','096','-1')) {
    Reject {ConvertFrom-ThyInstrumentReply $pads.Replace('fixed96',('fixed'+$velocity)) pads} 'Out-of-range or noncanonical fixed velocity accepted'
}
foreach($velocity in @('strike','mixed')) {
    Check ((ConvertFrom-ThyInstrumentReply $pads.Replace('fixed96',$velocity) pads).velocity -ceq $velocity) 'Strike/mixed velocity tag was lost'
}
foreach($velocity in @('Strike','mixed96','strike0','fixedstrike','fixed1mixed')) {
    Reject {ConvertFrom-ThyInstrumentReply $pads.Replace('fixed96',$velocity) pads} 'Malformed velocity tag accepted'
}
$velocityLine='VELOCITY id=95 logical=48 mode=1 override=1 soft=2 hard=128 curve=2 minimum=7 maximum=119 last=91 peak=73 sample_ms=4294967295'
$velocityFields=ConvertFrom-ThyInstrumentReply $velocityLine 'velocity 95'
Check ($velocityFields.id -eq 95 -and $velocityFields.logical -eq 48 -and $velocityFields.mode -eq 1 -and $velocityFields.override -eq 1 -and $velocityFields.last -eq 91 -and $velocityFields.sample_ms -eq [uint32]::MaxValue) 'Effective velocity telemetry decoding failed'
foreach($bad in @($velocityLine.Replace('id=95','id=94'),$velocityLine.Replace('logical=48','logical=96'),$velocityLine.Replace('mode=1','mode=2'),$velocityLine.Replace('override=1','override=2'),$velocityLine.Replace('soft=2','soft=128'),$velocityLine.Replace('hard=128','hard=4096'),$velocityLine.Replace('curve=2','curve=3'),$velocityLine.Replace('minimum=7','minimum=0'),$velocityLine.Replace('minimum=7','minimum=120'),$velocityLine.Replace('maximum=119','maximum=128'),$velocityLine.Replace('last=91','last=128'),$velocityLine.Replace('peak=73','peak=4096'),$velocityLine.Replace('sample_ms=4294967295','sample_ms=4294967296'),$velocityLine+' extra=0')) {
    Reject {ConvertFrom-ThyInstrumentReply $bad 'velocity 95'} 'Malformed velocity telemetry accepted'
}
foreach($bad in @($pads+' extra=0',$pads.Replace('frames=297','frames=4294967296'),$pads.Replace('calibration=unverified','calibration=verified'))) {Reject {ConvertFrom-ThyInstrumentReply $bad pads} 'Malformed pad schema accepted'}
Reject {ConvertFrom-ThyInstrumentReply 'OK panic' panic} 'Panic was allowed'
Reject {ConvertFrom-ThyInstrumentReply 'OK THYINSTRUMENT/1 commands=status,pads,panic,help' help} 'Truncated help accepted'

# Fake serial and fake clock make command order and duration deterministic.
function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {}
function Get-ThyInstrumentMilliseconds {return $script:clock}
function Wait-ThyInstrumentPoll([int]$Seconds) {$script:clock+=$Seconds*1000}
function New-FakePort {
    $p=[pscustomobject]@{Writes=[Collections.Generic.List[string]]::new()}
    $p|Add-Member ScriptMethod Write {param([string]$Text) $this.Writes.Add($Text)}
    return $p
}
function Read-ThyConsoleLine([object]$SerialPort) {
    if($SerialPort.Writes.Count -eq 0) {return $script:greeting}
    $command=$SerialPort.Writes[$SerialPort.Writes.Count-1].TrimEnd([char]10)
    switch -CaseSensitive ($command) {
        help {return 'OK THYINSTRUMENT/1 commands=status,pads,panic,help storage_writes=none'}
        status {return 'STATUS THYINSTRUMENT/1 build='+$script:fakeBuild+' ready=1 uptime_ms='+(1000+$script:clock)+' errors='+$script:fakeErrors+' presses=0 releases=0 voices=0 layout=C Major (Ionian)'}
        pads {return 'PADS frames='+(10+[int]($script:clock/10))+' scans='+(160+[int]($script:clock/10)*16)+' errors=0 profile=150us-21MHz velocity=fixed96 calibration=unverified'}
        default {throw 'Unexpected test command'}
    }
}
$script:fakeBuild=$build;$script:fakeErrors=0;$script:clock=0;$script:greeting='HELLO THYINSTRUMENT/1 alpha; type help'
$port=New-FakePort
$result=Invoke-ThyInstrumentSession $port $build 10 5
Check ($result.completed -and $result.firmware_checks_passed -and $result.status_samples -eq 3) 'Bounded healthy session failed'
Check ($port.Writes.Count -eq 7) 'Unexpected command count'
Check (($port.Writes -join ',') -ceq "help`n,status`n,pads`n,status`n,pads`n,status`n,pads`n") 'Wrong allowlisted command order/framing'
$port=New-FakePort;$script:clock=0;$script:fakeBuild='ffffffffffffffff'
Reject {Invoke-ThyInstrumentSession $port $build 10 5} 'Build mismatch did not abort'
Check ($port.Writes.Count -eq 2) 'Sent pads after wrong build'
$port=New-FakePort;$script:clock=0;$script:fakeBuild=$build;$script:greeting='HELLO OTHER'
Reject {Invoke-ThyInstrumentSession $port $build 10 5} 'Wrong greeting did not abort'
Check ($port.Writes.Count -eq 0) 'Sent command before greeting accepted'
$port=New-FakePort;$script:clock=0;$script:greeting='HELLO THYINSTRUMENT/1 alpha; type help';$script:fakeErrors=16
$result=Invoke-ThyInstrumentSession $port $build 10 5
Check ($result.completed -and -not $result.firmware_checks_passed) 'Firmware errors should be captured and flagged'
$script:instrumentGreeted=$true;$script:instrumentBound=$true
foreach($command in @('panic','pads start','reset','status extra','HELP')) {Reject {Send-ThyInstrumentObservation $port $command} 'Unsafe/unknown command was allowed'}
$script:instrumentGreeted=$false
Reject {Send-ThyInstrumentObservation $port status} 'Ungreeted command allowed'
$script:instrumentGreeted=$true;$script:instrumentBound=$false
Reject {Send-ThyInstrumentObservation $port pads} 'Unbound pads command allowed'

$newHelp='OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help storage_writes=none'
Check ((ConvertFrom-ThyInstrumentReply $newHelp help).commands -clike '*pad,edges,io,*') 'Detailed help was rejected'
$settingsHelp='OK THYINSTRUMENT/1 commands=status,pads,pad,edges,io,panic,help,settings storage_writes=none autosave=idle'
$settingsHelpFields=ConvertFrom-ThyInstrumentReply $settingsHelp help
Check ($settingsHelpFields.commands -ceq 'status,pads,pad,edges,io,panic,help,settings' -and $settingsHelpFields.autosave -ceq 'idle') 'Current settings-aware help was rejected or altered'
foreach($bad in @($settingsHelp.Replace(' autosave=idle',''),$settingsHelp.Replace('autosave=idle','autosave=off'),$settingsHelp.Replace(',settings',''),$settingsHelp+' extra=0')) {Reject {ConvertFrom-ThyInstrumentReply $bad help} 'Incomplete or expanded settings-aware help accepted'}
$pad='PAD id=48 logical=48 raw=4 baseline=8 delta=0 signed_delta=-4 attack=1377 release=1354 profile=1 held=0 blocked=0 warmup=8 attack_count=0 release_count=0 pressure=0 sample_ms=1234 transition_ms=1000'
$detailPad=ConvertFrom-ThyInstrumentReply $pad 'pad 48'
Check ($detailPad.id -eq 48 -and $detailPad.signed_delta -eq -4 -and $detailPad.attack -eq 1377 -and $detailPad.sample_ms -eq 1234) 'Per-pad snapshot decoding failed'
Check ((ConvertFrom-ThyInstrumentReply $pad.Replace('profile=1','profile=2') 'pad 48').profile -eq 2) 'Custom absolute pad profile rejected'
Reject {ConvertFrom-ThyInstrumentReply $pad.Replace('profile=1','profile=3') 'pad 48'} 'Unknown pad profile accepted'
foreach($bad in @($pad.Replace('raw=4','raw=4096'),$pad.Replace('id=48','id=49'),$pad.Replace('signed_delta=-4','signed_delta=-4096'),$pad.Replace('attack=1377','attack=32768'),$pad.Replace('held=0','held=2'),$pad.Replace('warmup=8','warmup=9'),$pad.Replace('pressure=0','pressure=128'),$pad.Replace('delta=0','delta=-0'),$pad+' extra=0')) {
    Reject {ConvertFrom-ThyInstrumentReply $bad 'pad 48'} 'Invalid per-pad snapshot accepted'
}
$journal='EDGES oldest=1 newest=2 overwritten=0 count=2 next=2 data=1,100,48,48,1,3000,4,2996,1377,1354,0,1;2,200,48,48,2,900,4,896,1377,1354,0,1'
$decodedJournal=ConvertFrom-ThyInstrumentReply $journal 'edges 0'
Check ($decodedJournal.entries.Count -eq 2 -and $decodedJournal.entries[1].action -eq 2 -and $decodedJournal.next -eq 2) 'Journal decode failed'
$customJournal=$journal.Replace(',0,1',',0,2')
Check ((ConvertFrom-ThyInstrumentReply $customJournal 'edges 0').entries[0].profile -eq 2) 'Custom absolute edge profile rejected'
Reject {ConvertFrom-ThyInstrumentReply $customJournal.Replace(',0,2',',0,3') 'edges 0'} 'Unknown edge profile accepted'
Check ((ConvertFrom-ThyInstrumentReply 'EDGES oldest=0 newest=0 overwritten=0 count=0 next=0 data=-' 'edges 0').entries.Count -eq 0) 'Empty journal decode failed'
foreach($bad in @($journal.Replace('count=2','count=3'),$journal.Replace('next=2','next=1'),$journal.Replace(';2,200',';1,200'),$journal.Replace('48,48,1,','96,48,1,'),$journal.Replace('newest=2','newest=4294967296'),$journal+';')) {Reject {ConvertFrom-ThyInstrumentReply $bad 'edges 0'} 'Malformed journal accepted'}
$ioLine='IO din_ready=1 din_errors=0 din_queued=0 din_tx=144 din_accept=48 usb_ready=0 usb_mounted=0 usb_accepted=0 din_events=48 usb_dropped=0 transport_fault=0 core_queued=0 core_pending=0 din_pending=0 usb_pending=0 held=0 blocked=0 warmed=95 led_errors=0 led_frames=1 led_fault=0'
Check ((ConvertFrom-ThyInstrumentReply $ioLine io).warmed -eq 95) 'I/O observation decode failed'
foreach($bad in @($ioLine.Replace('din_ready=1','din_ready=2'),$ioLine.Replace('warmed=95','warmed=97'),$ioLine.Replace('din_pending=0 ','') )) {Reject {ConvertFrom-ThyInstrumentReply $bad io} 'Invalid I/O observation accepted'}
$script:instrumentGreeted=$true;$script:instrumentBound=$true
foreach($command in @('pad -1','pad 96','pad 0 extra','velocity -1','velocity 96','velocity 0 extra','velocity 01','velocity 0;panic','edges -1','edges 4294967296','edges 01','io panic')) {Reject {Send-ThyInstrumentObservation $port $command} 'Non-read-only or invalid detail command accepted'}
$script:instrumentBound=$false
Reject {Send-ThyInstrumentObservation $port 'pad 0'} 'Ungated per-pad command accepted'
Reject {Send-ThyInstrumentObservation $port 'velocity 0'} 'Ungated velocity command accepted'
Reject {Send-ThyInstrumentObservation $port 'edges 0'} 'Ungated journal command accepted'
Reject {Send-ThyInstrumentObservation $port io} 'Ungated I/O command accepted'

# Observe a complete synthetic detailed session without touching hardware.
$script:fakeErrors=0;$script:clock=0;$script:fakeBuild=$build
$script:instrumentStopPath=$null;$script:detailedIo=$ioLine;$script:detailedHelp=$newHelp
function Read-ThyConsoleLine([object]$SerialPort) {
    if($SerialPort.Writes.Count -eq 0) {return 'HELLO THYINSTRUMENT/1 alpha; type help'}
    $command=$SerialPort.Writes[$SerialPort.Writes.Count-1].TrimEnd([char]10)
    if($command -cmatch '^pad ([0-9]+)$') {return 'PAD id='+$Matches[1]+' logical='+$Matches[1]+' raw=4 baseline=4 delta=0 signed_delta=0 attack=1377 release=1354 profile=1 held=0 blocked=0 warmup=8 attack_count=0 release_count=0 pressure=0 sample_ms='+(1000+$script:clock)+' transition_ms=0'}
    if($command -ceq 'velocity 95') {return $script:velocityLine}
    if($command -cmatch '^edges [0-9]+$') {return 'EDGES oldest=0 newest=0 overwritten=0 count=0 next=0 data=-'}
    switch -CaseSensitive ($command) {
        help {return $script:detailedHelp}
        status {return 'STATUS THYINSTRUMENT/1 build='+$script:fakeBuild+' ready=1 uptime_ms='+(1000+$script:clock)+' errors=0 presses=0 releases=0 voices=0 layout=MAJOR C'}
        pads {return 'PADS frames='+(10+[int]($script:clock/10))+' scans='+(160+[int]($script:clock/10)*16)+' errors=0 profile=150us-21MHz velocity=fixed96 calibration=unverified'}
        io {return $script:detailedIo}
        default {throw 'Unexpected detailed test command'}
    }
}
$port=New-FakePort
$result=Invoke-ThyInstrumentSession $port $build 10 5 $true @(0,48)
Check ($result.completed -and $result.detailed -and $result.last_io.din_ready -eq 1 -and $result.last_io.usb_mounted -eq 0 -and $result.missed_transitions -eq 0) 'Detailed DIN-only observation failed'
$script:clock=0;$script:detailedHelp=$settingsHelp;$port=New-FakePort
$result=Invoke-ThyInstrumentSession $port $build 10 5 $true @(0,48)
Check ($result.completed -and $result.detailed) 'Current help failed the existing detailed collector'
Check (@($port.Writes|Where-Object {$_ -ceq "settings`n"}).Count -eq 0) 'Help capability widened the observation command allowlist'
$script:instrumentGreeted=$true;$script:instrumentBound=$true
Reject {Send-ThyInstrumentObservation $port settings} 'Settings was added to the old collector send allowlist'
Check ((Send-ThyInstrumentObservation $port 'velocity 95').last -eq 91) 'Read-only velocity command could not pass the bound observation path'
foreach($id in 0..95) {Check (@($port.Writes|Where-Object {$_ -ceq ('pad '+$id+"`n")}).Count -ge 2) 'Initial/final snapshot did not include every pad'}
Check (@($port.Writes|Where-Object {$_ -cmatch '^(panic|reset|flash|calibrate)'}).Count -eq 0) 'Detailed observer sent a mutation'
$script:clock=0;$script:detailedHelp='OK THYINSTRUMENT/1 commands=status,pads,panic,help storage_writes=none';$port=New-FakePort
Reject {Invoke-ThyInstrumentSession $port $build 10 5 $true @(0)} 'Old alpha silently accepted for detailed observations'
Check ($port.Writes.Count -eq 2) 'Detailed commands sent without advertised support'
Write-Output ('PASS instrument telemetry offline assertions='+$script:assertions)
