# Offline only: native helper compiles but never enumerates or opens devices.
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '../tools/capture_instrument_midi.ps1'
$tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
foreach($name in @('Get-ThyMidiPnpBinding','Select-ThyMidiEndpoint','Test-ThyMidiCandidateInstance','ConvertFrom-ThyMidiShort','Update-ThyMidiLedger','Save-ThyMidiPackets')) {
    $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
    if($found.Count -ne 1) {throw 'Missing MIDI helper'}
    Invoke-Expression $found[0].Extent.Text
}
$script:checks=0
function Check([bool]$Value,[string]$Message) {$script:checks++;if(-not $Value) {throw $Message}}
function Reject([scriptblock]$Action,[string]$Message) {$bad=$false;try {& $Action|Out-Null} catch {$bad=$true};Check $bad $Message}
$good=[pscustomobject]@{Index=7;Name='Diagnostic MIDI RX';InterfacePath='opaque-interface';InstanceId='MIDI-NODE';QueryError=$null}
Check ((Select-ThyMidiEndpoint @($good) 'Diagnostic MIDI RX' 'opaque-interface').Index -eq 7) 'Exact endpoint selection failed'
Reject {Select-ThyMidiEndpoint @($good) 'Instrument MIDI' 'opaque-interface'} 'Name mismatch accepted'
Reject {Select-ThyMidiEndpoint @($good) 'Diagnostic MIDI RX' 'guessed-interface'} 'Path mismatch accepted'
Reject {Select-ThyMidiEndpoint @($good,$good) 'Diagnostic MIDI RX' 'opaque-interface'} 'Duplicate endpoint accepted'
$bad=$good.psobject.Copy();$bad.QueryError='No identity'
Reject {Select-ThyMidiEndpoint @($bad) 'Diagnostic MIDI RX' 'opaque-interface'} 'Unqueryable endpoint accepted'
$serial='000100020003000400050006';$hub='USB\VID_0BDA&PID_5411\1&SYNTHETIC&0&1'
$physical='USB\VID_CAFE&PID_4009\'+$serial;$midi='USB\VID_CAFE&PID_4009&MI_02\A'
Check (Test-ThyMidiCandidateInstance $midi) 'Instrument MIDI instance filtered out'
foreach($other in @('SWD\MMDEVAPI\other','USB\VID_CAFE&PID_4009&MI_00\A','USB\VID_CAFE&PID_40090&MI_02\A','')) {Check (-not (Test-ThyMidiCandidateInstance $other)) 'Unrelated instance included in candidate inventory'}
$script:nodes=@([pscustomobject]@{InstanceId=$midi;Class='MEDIA';Status='OK';FriendlyName='Cached name'},[pscustomobject]@{InstanceId=$physical;Class='USB';Status='OK';FriendlyName='Composite'})
$script:parents=@{};$script:parents[$midi]=$physical;$script:parents[$physical]=$hub
function Get-PnpDevice {param([switch]$PresentOnly,[string]$ErrorAction) return $script:nodes}
function Get-PnpDeviceProperty {param([string]$InstanceId,[string]$KeyName,[string]$ErrorAction) if($KeyName -ne 'DEVPKEY_Device_Parent') {throw 'Unexpected property'};return [pscustomobject]@{Data=$script:parents[$InstanceId]}}
Check ((Get-ThyMidiPnpBinding $midi $serial $hub).Count -eq 2) 'Exact MIDI parent binding failed'
Reject {Get-ThyMidiPnpBinding $midi ('F'*24) $hub} 'Foreign serial accepted'
Reject {Get-ThyMidiPnpBinding $midi $serial 'other-hub'} 'Foreign hub accepted'
Reject {Get-ThyMidiPnpBinding $physical $serial $hub} 'Physical node alone bypassed MI_02'
$script:nodes[0].Status='Error';Reject {Get-ThyMidiPnpBinding $midi $serial $hub} 'Unhealthy MIDI node accepted';$script:nodes[0].Status='OK'
$script:parents[$midi]=$midi;Reject {Get-ThyMidiPnpBinding $midi $serial $hub} 'Cyclic MIDI node accepted';$script:parents[$midi]=$physical
Reject {Get-ThyMidiPnpBinding '' $serial $hub} 'Missing interface identity accepted'
$on=ConvertFrom-ThyMidiShort 0x00603c90
Check ($on.event -ceq 'note_on' -and $on.channel -eq 1 -and $on.data1 -eq 60 -and $on.data2 -eq 96) 'Note-on decode failed'
$off=ConvertFrom-ThyMidiShort 0x00003c90
Check ($off.event -ceq 'note_off') 'Zero-velocity note-on is not note-off'
foreach($bad in @([uint32]0x01003c90,[uint32]0x00003c00,[uint32]0x00803c90,[uint32]0x00608090)) {Reject {ConvertFrom-ThyMidiShort $bad} 'Invalid packed/data MIDI accepted'}
$ledger=@{};$counts=@{received=0;note_on=0;note_off=0;unmatched_note_off=0;repeated_note_on=0;channel_clears=0;driver_errors=0;driver_moredata=0;invalid=0}
Update-ThyMidiLedger $ledger $on $counts
Update-ThyMidiLedger $ledger (ConvertFrom-ThyMidiShort 0x00603c91) $counts
Update-ThyMidiLedger $ledger $off $counts
Check ($ledger.Count -eq 1 -and $ledger.ContainsKey('2:60')) 'Note ledger crossed channels'
Update-ThyMidiLedger $ledger (ConvertFrom-ThyMidiShort 0x00003c81) $counts
Check ($ledger.Count -eq 0 -and $counts.note_on -eq 2 -and $counts.note_off -eq 2) 'Balanced note ledger failed'
Update-ThyMidiLedger $ledger $off $counts
Check ($counts.unmatched_note_off -eq 1) 'Unmatched release was not retained'
Update-ThyMidiLedger $ledger $on $counts;Update-ThyMidiLedger $ledger $on $counts
Check ($counts.repeated_note_on -eq 1 -and $ledger['1:60'] -eq 2) 'Repeated note ownership count lost'
Update-ThyMidiLedger $ledger $off $counts
Check ($ledger['1:60'] -eq 1) 'Repeated note-off removed all counts'
Update-ThyMidiLedger $ledger (ConvertFrom-ThyMidiShort 0x00007bb0) $counts
Check ($ledger.Count -eq 0 -and $counts.channel_clears -eq 1) 'All-notes-off ledger handling failed'
function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {}
$packet=[pscustomobject]@{Message=0x3c5;Packed=0x00603c90;DeviceMilliseconds=500;HostTimestampTicks=0}
Save-ThyMidiPackets @($packet) $ledger $counts
Check ($ledger.Count -eq 0 -and $counts.driver_errors -eq 1) 'Driver error modified note ledger'
$packet.Message=0x3cc;Save-ThyMidiPackets @($packet) $ledger $counts
Check ($counts.driver_moredata -eq 1 -and $ledger.Count -eq 1) 'Driver backlog event was not recorded'
$native=Join-Path $PSScriptRoot '../tools/instrument_midi_winmm.cs'
Add-Type -Path $native
$receiver=[ThyInstrumentObservation.Input]::new()
Check ($receiver.Drain().Length -eq 0 -and $receiver.Dropped -eq 0) 'Fresh bounded native queue is not empty'
$receiver.Dispose() # Empty instance; no native API call.
$source=[IO.File]::ReadAllText($native)
Check (-not [regex]::IsMatch($source,'extern\s+\w+\s+midiOut')) 'An output MIDI API was imported'
Write-Output ('PASS instrument MIDI offline assertions='+$script:checks)
