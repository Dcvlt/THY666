# WinMM input observer. Listing never opens a MIDI or serial endpoint.
# Receiving requires explicit name+opaque interface path and fresh alpha CDC
# greeting/build. No MIDI output functions or firmware mutations are used.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [switch]$ListOnly,
    [ValidatePattern('^$|^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [string]$ExpectedMidiName='',
    [string]$ExpectedInterfacePath='',
    [string]$OutputDirectory='',
    [ValidateRange(10,7200)][int]$DurationSeconds=60
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required:(!$ListOnly) -Pattern '^[0-9A-Fa-f]{16}$'
$ExpectedMidiName = Resolve-ThyLocalValue -Name 'THY666_MIDI_INPUT_NAME' -Value $ExpectedMidiName -Explicit:($PSBoundParameters.ContainsKey('ExpectedMidiName')) -Configuration $localConfiguration
$ExpectedInterfacePath = Resolve-ThyLocalValue -Name 'THY666_MIDI_INTERFACE_PATH' -Value $ExpectedInterfacePath -Explicit:($PSBoundParameters.ContainsKey('ExpectedInterfacePath')) -Configuration $localConfiguration


function Get-ThyMidiPnpBinding([string]$InstanceId,[string]$Serial,[string]$HubParent) {
    if([string]::IsNullOrWhiteSpace($InstanceId)) {throw 'MIDI interface has no SetupAPI instance identity'}
    $present=@(Get-PnpDevice -PresentOnly -ErrorAction Stop)
    $chain=@();$currentId=$InstanceId;$seen=@{};$foundMidi=$false
    for($depth=0;$depth -lt 8;$depth++) {
        $nodes=@($present|Where-Object {$_.InstanceId -eq $currentId})
        if($nodes.Count -ne 1 -or $nodes[0].Status -ne 'OK' -or $seen.ContainsKey($currentId)) {throw 'MIDI parent chain unavailable, ambiguous or cyclic'}
        $seen[$currentId]=$true;$node=$nodes[0]
        $parent=(Get-PnpDeviceProperty -InstanceId $currentId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop).Data
        $chain+=[pscustomobject]@{instance_id=$currentId;parent=$parent;class=$node.Class;status=$node.Status;friendly_name=$node.FriendlyName}
        if($currentId -match '^USB\\VID_CAFE&PID_4009&MI_02\\') {$foundMidi=$true}
        if($currentId -match '^USB\\VID_CAFE&PID_4009\\') {
            if(-not $foundMidi -or $currentId -ne ('USB\VID_CAFE&PID_4009\'+$Serial) -or $parent -ne $HubParent) {throw 'MIDI endpoint is not the explicitly bound instrument physical device/MI_02'}
            return ,$chain
        }
        $currentId=$parent
    }
    throw 'MIDI endpoint physical parent not found within bound'
}

function Select-ThyMidiEndpoint([object[]]$Inventory,[string]$Name,[string]$InterfacePath) {
    $selected=@($Inventory|Where-Object {$_.Name -ceq $Name -and $_.InterfacePath -ceq $InterfacePath -and [string]::IsNullOrEmpty($_.QueryError)})
    if($selected.Count -ne 1) {throw 'Explicit MIDI name and interface path must select exactly one queryable input endpoint'}
    return $selected[0]
}

function Test-ThyMidiCandidateInstance([string]$InstanceId) {
    # This is an inventory filter only. Receiving still resolves and verifies
    # the full live parent chain against the explicitly supplied UID/hub.
    return $InstanceId -match '^USB\\VID_CAFE&PID_4009&MI_02\\'
}

function ConvertFrom-ThyMidiShort([uint32]$Packed) {
    $status=[int]($Packed -band 255);$data1=[int](($Packed -shr 8) -band 255);$data2=[int](($Packed -shr 16) -band 255)
    if(($Packed -band 0xff000000) -ne 0 -or $status -lt 0x80) {throw 'Invalid packed MIDI short message'}
    $kind=$status -band 0xf0;$channel=if($status -lt 0xf0) {($status -band 15)+1} else {$null}
    $length=if($kind -eq 0xc0 -or $kind -eq 0xd0) {2} elseif($status -eq 0xf1 -or $status -eq 0xf3) {2} elseif($status -eq 0xf2 -or $status -lt 0xf0) {3} else {1}
    if(($length -ge 2 -and $data1 -gt 127) -or ($length -eq 3 -and $data2 -gt 127)) {throw 'Invalid MIDI data byte'}
    $event='other'
    if($kind -eq 0x90 -and $data2 -gt 0) {$event='note_on'}
    elseif($kind -eq 0x80 -or ($kind -eq 0x90 -and $data2 -eq 0)) {$event='note_off'}
    elseif($kind -eq 0xb0) {$event='control_change'}
    return @{packed_hex=('{0:x8}' -f $Packed);status=$status;channel=$channel;data1=$data1;data2=$data2;length=$length;event=$event}
}

function Update-ThyMidiLedger([hashtable]$Ledger,[hashtable]$Event,[hashtable]$Counts) {
    if($Event.event -ceq 'note_on') {
        $key=('{0}:{1}' -f $Event.channel,$Event.data1)
        $Counts.note_on++
        if($Ledger.ContainsKey($key)) {$Counts.repeated_note_on++;$Ledger[$key]++} else {$Ledger[$key]=1}
    } elseif($Event.event -ceq 'note_off') {
        $key=('{0}:{1}' -f $Event.channel,$Event.data1);$Counts.note_off++
        if(-not $Ledger.ContainsKey($key)) {$Counts.unmatched_note_off++}
        elseif($Ledger[$key] -gt 1) {$Ledger[$key]--} else {$Ledger.Remove($key)}
    } elseif($Event.event -ceq 'control_change' -and $Event.data1 -in @(120,123)) {
        foreach($key in @($Ledger.Keys)) {if($key.StartsWith(([string]$Event.channel)+':',[StringComparison]::Ordinal)) {$Ledger.Remove($key)}}
        $Counts.channel_clears++
    }
}

function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {
    $script:eventWriter.WriteLine((([ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;kind=$Kind;data=$Data})|ConvertTo-Json -Depth 18 -Compress));$script:eventWriter.Flush()
    if($Kind -eq 'tx-complete') {$script:wireWriter.Write('TX '+$Data.line+"`n")}
    elseif($Kind -eq 'rx') {$script:wireWriter.Write('RX '+$Data.line+"`r`n")}
    elseif($Kind -eq 'partial-rx') {$script:wireWriter.WriteLine('PARTIAL_RX_HEX '+$Data.hex)}
    $script:wireWriter.Flush()
}

function Save-ThyMidiPackets([object[]]$Packets,[hashtable]$Ledger,[hashtable]$Counts) {
    foreach($packet in $Packets) {
        $Counts.received++
        if($packet.Message -eq 0x3c5) {$Counts.driver_errors++}
        if($packet.Message -eq 0x3cc) {$Counts.driver_moredata++}
        try {
            $decoded=ConvertFrom-ThyMidiShort $packet.Packed
            if($packet.Message -ne 0x3c5) {Update-ThyMidiLedger $Ledger $decoded $Counts}
            Write-ThyTelemetryEvent 'midi-short' @{message=$packet.Message;device_ms=$packet.DeviceMilliseconds;host_utc_ticks=$packet.HostTimestampTicks;decoded=$decoded;ledger_keys=$Ledger.Count}
        } catch {
            $Counts.invalid++
            Write-ThyTelemetryEvent 'midi-invalid' @{message=$packet.Message;packed=$packet.Packed;device_ms=$packet.DeviceMilliseconds;host_utc_ticks=$packet.HostTimestampTicks;failure=$_.Exception.Message}
        }
    }
}

$ErrorActionPreference='Stop'
$nativePath=Join-Path $PSScriptRoot 'instrument_midi_winmm.cs'
if(-not ('ThyInstrumentObservation.Input' -as [type])) {Add-Type -Path $nativePath}
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
$binding=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
$inventory=@([ThyInstrumentObservation.Input]::Enumerate())
if($ListOnly) {
    $listed=@()
    foreach($endpoint in $inventory) {
        $chain=$null;$errorText=$endpoint.QueryError
        if([string]::IsNullOrEmpty($errorText)) {
            if(Test-ThyMidiCandidateInstance $endpoint.InstanceId) {
                try {$chain=Get-ThyMidiPnpBinding $endpoint.InstanceId $ExpectedSerial $ExpectedHubParent} catch {$errorText=$_.Exception.Message}
            } else {$errorText='Unrelated input: SetupAPI instance is outside the instrument MI_02 interface'}
        }
        $listed+=[pscustomobject]@{name=$endpoint.Name;interface_path=$endpoint.InterfacePath;instance_id=$endpoint.InstanceId;index_at_listing=$endpoint.Index;bound_to_expected_device=($null -ne $chain);binding=$chain;query_or_binding_error=$errorText}
    }
    [pscustomobject]@{format='thy666.instrument-midi-input-inventory';listing_only=$true;opened_endpoints=0;cdc_binding=$binding;inputs=$listed}|ConvertTo-Json -Depth 16
    exit
}
if([string]::IsNullOrWhiteSpace($ExpectedBuild) -or [string]::IsNullOrWhiteSpace($ExpectedMidiName) -or [string]::IsNullOrWhiteSpace($ExpectedInterfacePath) -or [string]::IsNullOrWhiteSpace($OutputDirectory)) {throw 'Receiving requires ExpectedBuild, ExpectedMidiName, ExpectedInterfacePath and a new OutputDirectory'}
$selected=Select-ThyMidiEndpoint $inventory $ExpectedMidiName $ExpectedInterfacePath
$midiBinding=Get-ThyMidiPnpBinding $selected.InstanceId $ExpectedSerial $ExpectedHubParent
$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../analysis/local'))+[IO.Path]::DirectorySeparatorChar
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $outputPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputPath)) {throw 'Output must be a new directory inside analysis/local'}
$ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($outputPath))
while($null -ne $ancestor -and ($ancestor.FullName+[IO.Path]::DirectorySeparatorChar).StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    if($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Output ancestors must not be junctions or symbolic links'}
    $ancestor=$ancestor.Parent
}
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop|Out-Null
$streams=@();$serialPort=$null;$midiInput=$null;$completed=$false;$failure=$null;$bindingAfter=$null;$midiBindingAfter=$null;$status=$null;$dropped=0
$ledger=@{};$counts=@{received=0;note_on=0;note_off=0;unmatched_note_off=0;repeated_note_on=0;channel_clears=0;driver_errors=0;driver_moredata=0;invalid=0}
$script:observationClock=[Diagnostics.Stopwatch]::StartNew();$started=[DateTime]::UtcNow.ToString('o')
try {
    foreach($name in @('events.jsonl','wire.txt')) {
        $stream=[IO.File]::Open((Join-Path $outputPath $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);$streams+=,$stream
        $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
        if($name -eq 'events.jsonl') {$script:eventWriter=$writer} else {$script:wireWriter=$writer}
    }
    Write-ThyTelemetryEvent 'binding-before' @{cdc=$binding;midi=$midiBinding;endpoint=$selected}
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII;$serialPort.Handshake=[IO.Ports.Handshake]::None;$serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false;$serialPort.ReadTimeout=200;$serialPort.WriteTimeout=2000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open();$script:instrumentGreeted=$false;$script:instrumentBound=$false
    Assert-ThyInstrumentGreeting (Read-ThyConsoleLine $serialPort);$script:instrumentGreeted=$true
    $null=Send-ThyInstrumentObservation $serialPort help
    $status=Send-ThyInstrumentObservation $serialPort status
    Assert-ThyInstrumentStatus $status $ExpectedBuild $null
    if($status.ready -ne 1 -or $status.errors -ne 0) {throw 'Alpha is not ready/error-free; MIDI receiver not opened'}
    $serialPort.Close();$serialPort.Dispose();$serialPort=$null
    # Re-enumerate immediately before opening; indices are never accepted as
    # user input and must still resolve to the exact selected opaque path.
    $selected=Select-ThyMidiEndpoint @([ThyInstrumentObservation.Input]::Enumerate()) $ExpectedMidiName $ExpectedInterfacePath
    $midiBinding=Get-ThyMidiPnpBinding $selected.InstanceId $ExpectedSerial $ExpectedHubParent
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $midiInput=[ThyInstrumentObservation.Input]::new();$midiInput.Open($selected.Index)
    Write-ThyTelemetryEvent 'midi-input-opened' @{endpoint=$selected;duration_seconds=$DurationSeconds}
    Write-Output ('MIDI input receiver running for up to '+$DurationSeconds+' seconds: '+$selected.Name)
    $stopFile=Join-Path $outputPath '.stop'
    $receiveClock=[Diagnostics.Stopwatch]::StartNew()
    while($receiveClock.Elapsed.TotalSeconds -lt $DurationSeconds -and -not [IO.File]::Exists($stopFile)) {Save-ThyMidiPackets @($midiInput.Drain()) $ledger $counts;Start-Sleep -Milliseconds 20}
    Write-ThyTelemetryEvent 'midi-input-stop' @{reason=$(if([IO.File]::Exists($stopFile)) {'host-stop-file'} else {'duration-elapsed'});receive_elapsed_ms=$receiveClock.ElapsedMilliseconds}
    $midiInput.Dispose();Save-ThyMidiPackets @($midiInput.Drain()) $ledger $counts;$dropped=$midiInput.Dropped;$midiInput=$null
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $midiBindingAfter=Get-ThyMidiPnpBinding $selected.InstanceId $ExpectedSerial $ExpectedHubParent
    $completed=$true
} catch {
    $failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$failure}}
} finally {
    if($null -ne $serialPort) {try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {$completed=$false;$failure='CDC close failed: '+$_.Exception.Message}}
    if($null -ne $midiInput) {try {$midiInput.Dispose();Save-ThyMidiPackets @($midiInput.Drain()) $ledger $counts;$dropped=$midiInput.Dropped} catch {$completed=$false;$failure='MIDI input close failed: '+$_.Exception.Message}}
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()};if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $report=[ordered]@{format='thy666.instrument-midi-input';version=1;completed=$completed;failure=$failure;started_utc=$started;ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$script:observationClock.ElapsedMilliseconds;
        expected_build=$ExpectedBuild;expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;port=$Port;endpoint=$selected;identity_before=$binding;identity_after=$bindingAfter;midi_binding_before=$midiBinding;midi_binding_after=$midiBindingAfter;
        alpha_status_before=$status;duration_seconds=$DurationSeconds;counts=$counts;host_queue_dropped=$dropped;remaining_note_ledger=$ledger;
        receive_checks_passed=($completed -and $counts.received -gt 0 -and $counts.invalid -eq 0 -and $counts.driver_errors -eq 0 -and $counts.driver_moredata -eq 0 -and $dropped -eq 0 -and $ledger.Count -eq 0);
        scope='Receive-only WinMM short messages for one explicitly bound input. Host input stop/reset/close discards pending host input, never sends output MIDI. No synth, MIDI output, panic, flash, storage or reset-device command. A zero ledger only covers the observation interval.'}
    foreach($pair in @(@('helper',$PSCommandPath),@('native_helper',$nativePath),@('shared_collector',$sharedPath),@('instrument_collector',$instrumentPath))) {$report[$pair[0]+'_sha256']=(Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash.ToLowerInvariant()}
    foreach($name in @('events.jsonl','wire.txt')) {if(Test-Path -LiteralPath (Join-Path $outputPath $name)) {$report[$name+'_sha256']=(Get-FileHash -LiteralPath (Join-Path $outputPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()}}
    $stream=[IO.File]::Open((Join-Path $outputPath 'observation.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try {$writer.WriteLine(($report|ConvertTo-Json -Depth 20))} finally {$writer.Dispose();$stream.Dispose()}
}
[pscustomobject]@{path=$outputPath;completed=$completed;receive_checks_passed=$report.receive_checks_passed;counts=$counts;remaining_notes=$ledger.Count;failure=$failure}|ConvertTo-Json -Depth 5
if(-not $completed) {throw ('MIDI input observation incomplete; evidence preserved at '+$outputPath)}
