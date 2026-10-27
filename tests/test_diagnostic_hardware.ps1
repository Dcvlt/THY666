# Offline session logic: unopened SerialPort, scripted CRLF replies and mock clock.
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Import-Functions([string]$Path,[string[]]$Names) {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if($errors.Count) {throw ($errors.Message -join '; ')}
    foreach($name in $Names) {
        $found=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
        if($found.Count -ne 1) {throw ('Expected one function '+$name)}
        Set-Item -Path ('Function:script:'+$name) -Value $found[0].Body.GetScriptBlock()
    }
}
Import-Functions (Join-Path $root 'tools/capture_diagnostic_telemetry.ps1') @('ConvertFrom-ThyTelemetryLine','Get-ThyTelemetryInteger','Assert-ThyGreeting','Assert-ThyStartup','Assert-ThyStatus','Assert-ThyControls','Read-ThyConsoleLine')
$script:actualReader=(Get-Item Function:Read-ThyConsoleLine).ScriptBlock
Import-Functions (Join-Path $root 'tools/test_diagnostic_hardware.ps1') @('Send-ThyHardwareCommand','Invoke-ThyHardwareSession','Assert-ThyCenterStatus')
Add-Type -TypeDefinition @'
public sealed class ThyHardwareOfflinePort : System.IO.Ports.SerialPort {
    public string LastValue;
    public System.Collections.Generic.List<string> Writes = new System.Collections.Generic.List<string>();
    byte[] input; int at;
    public new void Write(string value) { LastValue=value; Writes.Add(value); }
    public void QueueRead(string value) { input=System.Text.Encoding.ASCII.GetBytes(value); at=0; }
    public new int ReadByte() { if(input==null||at>=input.Length)throw new System.TimeoutException(); return input[at++]; }
}
'@
$script:events=[Collections.Generic.List[object]]::new()
function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {$script:events.Add([pscustomobject]@{kind=$Kind;data=$Data})}
function Get-ThyHardwareMilliseconds {return $script:fakeClock}
function Wait-ThyHardwareMilliseconds([int]$Milliseconds) {$script:fakeClock+=$Milliseconds}
$serial='000100020003000400050006';$build='0d99dc2374abf123'
$script:startupLine='STARTUP THYDIAG/1 valid=1 control=0 ipsr=0 primask=0 basepri=0 faultmask=0 msp=0x20020000 psp=0 vtor=0x08040000 mpu_ctrl=0 cpacr=0 fpccr=0 aircr=0 ccr=0 icsr=0 shcsr=0 cfsr=0 hfsr=0 rcc_cr=0 rcc_cfgr=0 rcc_csr=0 rcc_apb1enr=0 iwdg_sr=0 iwdg_pr=0 iwdg_rlr=0 wwdg_cr=0 wwdg_cfr=0'
$script:controlsLine='CONTROLS valid=15 covered=0 total=43 physical_map=observe'
for($bank=0;$bank -lt 4;$bank++) {
    foreach($field in @('raw','seen','rose','fell','logical','known_seen','changes','changed_ms','sample_ms')) {$script:controlsLine+=' bank'+$bank+'_'+$field+'=0'}
    $script:controlsLine+=' bank'+$bank+'_samples=1'
}
function Read-ThyConsoleLine([IO.Ports.SerialPort]$SerialPort) {
    if($SerialPort.IsOpen) {throw 'Offline fake unexpectedly opened'}
    $command=if($null -eq $SerialPort.LastValue) {''} else {$SerialPort.LastValue.TrimEnd("`n")}
    if($script:failCommand -ceq $command -and ($command -cne 'pads status' -or $script:started)) {$line='ERR injected';$script:failCommand=$null}
    elseif($command -ceq '') {$line=if($script:badGreeting) {'HELLO WRONG'} else {'HELLO THYDIAG/1 diagnostic; type help'}}
    elseif($command -ceq 'startup') {$line=$script:startupLine}
    elseif($command -ceq 'status') {
        $uid=if($script:badUid) {'FFFFFFFFFFFFFFFFFFFFFFFF'} else {$serial}
        $line='STATUS THYDIAG/1 build='+$build+' uid='+$uid+' uptime_ms='+(1000+$script:fakeClock)+' oled_frames='+(2+[int][Math]::Floor($script:fakeClock/500))+' cdc_commands='+$SerialPort.Writes.Count+' stage=5 errors=0 hclk=168000000 pclk1=42000000 pclk2=84000000 clock_source=1 usb_clock_ready=1 device_id=0x10076413 flash_kib=1024 usb_initialized=1 usb_mounted=1 core_selftest=0 fault_exception=0 fault_cfsr=0 fault_hfsr=0'
    } elseif($command -ceq 'center status') {
        $driver=if($script:badCenter) {'dma'} else {'cpu'}
        $line='CENTER state=2 mode=1 channel=0 brightness=128 enabled_mask=0x00ffffe0 frames=1 errors=0 updated_ms=1 code=0 driver='+$driver+' lighting_dma=none nvm_writes=none'
    } elseif($command.StartsWith('controls')) {$line=$script:controlsLine}
    elseif($command.StartsWith('led')) {
        if($command -ceq 'led off') {$script:off=$true;$script:ledPending=$true}
        elseif($command -cne 'led status') {$script:off=$false;$script:ledPending=$true}
        if($command -ceq 'led status' -and $script:ledPending) {$script:ledFrames++;$script:ledPending=$false}
        $state=if($script:ledPending) {'queued'} elseif($script:off) {'idle'} else {'holding'}
        $mode=if($script:off) {'off'} else {'walk'}
        $sent=if($script:off -and -not $script:ledPending) {1} else {0}
        $ledErrors=if($script:ledInjectError -and $script:ledFrames -gt 0) {1} else {0}
        $line='LED state='+$state+' mode='+$mode+' off_frame_sent='+$sent+' frames='+$script:ledFrames+' code=0 errors='+$ledErrors+' walk_steps=72 pixel=71'
    } elseif($command.StartsWith('pads')) {
        if($command -ceq 'pads start') {$script:started=$true}
        if($command -ceq 'pads stop') {$script:stopped=$true}
        $fault=$script:started -and ($script:retainedFault -or $script:holdFailure) -and $command -cne 'pads start'
        $active=if(-not $script:started -or ($script:stopped -and -not $script:holdFailure) -or ($fault -and -not $script:holdFailure)) {0} else {1}
        $state=if($fault) {5} elseif($active -eq 0) {0} elseif($command -ceq 'pads start') {1} else {3}
        $progress=$script:started -and $command -cne 'pads start' -and -not $fault -and -not $script:noProgress
        $frames=if($progress) {5} else {0};$scans=if($progress) {80} else {0}
        $code=if($fault) {3} else {0};$errors=if($fault) {1} else {0}
        $busy=if($state -eq 3) {1} else {0};$decoupled=if($script:badProfile) {0} else {1}
        $line='PADS state='+$state+' active='+$active+' busy='+$busy+' lighting_decoupled='+$decoupled+' valid0=0xffffffff valid1=0xffffffff valid2=0xffffffc0 frames='+$frames+' scans='+$scans+' code='+$code+' errors='+$errors
    } elseif($command -ceq 'adc') {
        $line=if($script:holdFailure) {'ADC ok=0 ownership=pad-test snapshots=paused'} else {'ADC ok=0x3f ownership=raw-adc snapshots=active'}
    } else {throw ('Unexpected command '+$command)}
    $SerialPort.QueueRead($line+"`r`n")
    return (& $script:actualReader $SerialPort)
}
function Fresh-Port {
    $script:fakeClock=0;$script:off=$false;$script:stopped=$false;$script:started=$false
    $script:ledPending=$false;$script:ledFrames=0;$script:ledInjectError=$false;$script:retainedFault=$false;$script:noProgress=$false
    $script:badProfile=$false;$script:badCenter=$false
    $script:failCommand=$null;$script:holdFailure=$false;$script:badGreeting=$false;$script:badUid=$false
    $script:hardwareBound=$false
    return [ThyHardwareOfflinePort]::new()
}
function Assert-True([bool]$Condition,[string]$Meaning) {if(-not $Condition) {throw ('Assertion failed: '+$Meaning)}}
foreach($test in @('Controls','Led','Pads')) {
    $probe=Fresh-Port
    try {
        $result=Invoke-ThyHardwareSession $probe $test $build $serial 60 $true 'Walk' 2 95
        Assert-True ($result.completed -and $result.cleanup_completed) ('successful '+$test)
        Assert-True ($probe.Writes.FindAll([Predicate[string]]{param($value) $value -ceq "center status`n"}).Count -eq 2) 'center sampled before and after only'
        if($test -ceq 'Controls') {Assert-True (-not $result.firmware_checks_passed) 'missing full coverage remains incomplete firmware check'}
        else {Assert-True $result.firmware_checks_passed ('firmware checks '+$test)}
        Assert-True (-not $probe.IsOpen) 'port unopened'
        foreach($write in $probe.Writes) {
            Assert-True ($write.EndsWith("`n") -and -not $write.Contains("`r") -and ($write.ToCharArray()|Where-Object {[int]$_ -gt 127}).Count -eq 0) 'ASCII LF'
        }
        if($test -eq 'Led') {Assert-True ($probe.Writes.Contains("led off`n")) 'LED final OFF'}
        if($test -eq 'Pads') {Assert-True ($probe.Writes.Contains("pads stop`n") -and $probe.Writes.Contains("pads bank 5`n") -and $probe.Writes.Contains("adc`n")) 'pads STOP/banks/ADC'}
    } finally {$probe.Dispose()}
}
$probe=Fresh-Port
try {
    $result=Invoke-ThyHardwareSession $probe Led $build $serial 60 $false 'Off' 0 0
    Assert-True ($result.completed -and $result.cleanup_completed) 'explicit OFF succeeds'
    Assert-True ($script:fakeClock -le 1500) 'OFF observation bounded near one second'
    Assert-True (@($probe.Writes|Where-Object {$_ -match '^led (all|walk|pixel) '}).Count -eq 0) 'OFF never lights LEDs'
    Assert-True (@($probe.Writes|Where-Object {$_ -ceq "led off`n"}).Count -eq 2) 'initial OFF and final OFF only'
} finally {$probe.Dispose()}
foreach($kind in @('badGreeting','badUid')) {
    $probe=Fresh-Port;Set-Variable -Scope Script -Name $kind -Value $true
    try {
        $result=Invoke-ThyHardwareSession $probe Led $build $serial
        Assert-True (-not $result.completed) 'binding rejection'
        Assert-True (@($probe.Writes|Where-Object {$_ -notin @("startup`n","status`n")}).Count -eq 0) 'no hardware before binding'
    } finally {$probe.Dispose()}
}
$probe=Fresh-Port;$script:failCommand='pads status'
try {
    $result=Invoke-ThyHardwareSession $probe Pads $build $serial
    Assert-True (-not $result.completed -and $result.cleanup_completed) 'failure preserves cleanup'
    Assert-True (@($probe.Writes|Where-Object {$_ -ceq "pads start`n"}).Count -eq 1) 'no test restart'
    Assert-True (@($probe.Writes|Where-Object {$_ -ceq "pads stop`n"}).Count -eq 1) 'one cleanup STOP'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:holdFailure=$true
try {
    $result=Invoke-ThyHardwareSession $probe Pads $build $serial
    Assert-True (-not $result.completed -and -not $result.cleanup_completed) 'held ownership rejected'
    Assert-True ($script:fakeClock -le 12500) 'cleanup bounded'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:retainedFault=$true
try {
    $result=Invoke-ThyHardwareSession $probe Pads $build $serial
    Assert-True ($result.completed -and $result.cleanup_completed) 'inactive retained fault with restored ADC is quiesced'
    Assert-True (-not $result.firmware_checks_passed -and $result.acquisition_fault_retained) 'retained acquisition fault cannot pass firmware checks'
    Assert-True ($result.final_adc.ownership -ceq 'raw-adc') 'fault outcome preserves ADC restoration'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:noProgress=$true
try {
    $result=Invoke-ThyHardwareSession $probe Pads $build $serial
    Assert-True ($result.completed -and $result.cleanup_completed -and -not $result.firmware_checks_passed) 'zero progress is observed without inventing host failure'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:ledInjectError=$true
try {
    $result=Invoke-ThyHardwareSession $probe Led $build $serial
    Assert-True ($result.completed -and $result.cleanup_completed -and -not $result.firmware_checks_passed) 'LED error counter is retained without blanket pass'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:badProfile=$true
try {
    $result=Invoke-ThyHardwareSession $probe Pads $build $serial
    Assert-True (-not $result.completed -and -not $probe.Writes.Contains("pads start`n")) 'coupled profile cannot start'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:badCenter=$true
try {
    $result=Invoke-ThyHardwareSession $probe Led $build $serial
    Assert-True (-not $result.completed -and -not $probe.Writes.Contains("led all 0`n")) 'unexpected center driver blocks output test'
} finally {$probe.Dispose()}
$probe=Fresh-Port
try {
    $rejected=$false
    try {$null=Invoke-ThyHardwareSession $probe Led '64e55acc1eb9ec05' $serial} catch {$rejected=$true}
    Assert-True ($rejected -and $probe.Writes.Count -eq 0) 'previous build rejected before commands'
} finally {$probe.Dispose()}
$probe=Fresh-Port;$script:hardwareBound=$true
try {
    foreach($command in @('',"led off`nstatus",'led all 3','led pixel 96 0','pads bank 6','reset','midi','nor 0 1','internal 08040000 1','LED OFF','center','center all','center off','center channel 5','center status extra',"center status`ncenter off")) {
        $before=$probe.Writes.Count;$rejected=$false
        try {$null=Send-ThyHardwareCommand $probe $command 'LED '} catch {$rejected=$true}
        Assert-True ($rejected -and $probe.Writes.Count -eq $before) 'forbidden never written'
    }
} finally {$probe.Dispose()}
Assert-True (@($script:events|Where-Object {$_.kind -eq 'tx-complete' -and $_.data.hex.EndsWith('0a')}).Count -gt 0) 'TX bytes logged'
Assert-True (@($script:events|Where-Object {$_.kind -eq 'rx' -and $_.data.hex.EndsWith('0d0a')}).Count -gt 0) 'CRLF RX bytes logged'
[pscustomobject]@{result='PASS';build=$build;tests='Controls/LED/Pads, center read-only snapshots, build/UID gates, decoupling, quiesced retained fault vs held ownership, zero progress, ASCII allowlist';device_opened=$false;clock='mock';pnp_queries=0}|ConvertTo-Json
