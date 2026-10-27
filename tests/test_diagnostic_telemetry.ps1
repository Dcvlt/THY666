# Offline tests. Extract function ASTs only: never execute collector top-level,
# query PnP, open a COM port, assert DTR, or send a device command.
$ErrorActionPreference = 'Stop'
$collectorPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\capture_diagnostic_telemetry.ps1'))
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($collectorPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
foreach ($name in @('ConvertFrom-ThyTelemetryLine', 'Get-ThyTelemetryInteger', 'Assert-ThyGreeting', 'Assert-ThyTelemetryIdentity', 'Assert-ThyStartup', 'Assert-ThyStatus', 'Assert-ThyI2c', 'Assert-ThySensors', 'Assert-ThyControls')) {
    $found = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true))
    if ($found.Count -ne 1) { throw ('Expected exactly one pure function: ' + $name) }
    Invoke-Expression $found[0].Extent.Text
}
function Expect-Reject([scriptblock]$Action, [string]$Meaning) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw ('Expected rejection: ' + $Meaning) }
}
Assert-ThyGreeting 'HELLO THYDIAG/1 diagnostic; type help'
Assert-ThyGreeting 'HELLO THYDIAG/1 read-only candidate; type help'
foreach ($badGreeting in @('HELLO THYDIAG/2 diagnostic; type help', 'HELLO THYDIAG/1 diagnostic; type help extra',
    'hello THYDIAG/1 diagnostic; type help', 'HELLO THYDIAG/1 diagnostic;', '')) {
    Expect-Reject { Assert-ThyGreeting $badGreeting } 'unexpected/truncated greeting'
}
function New-Chain {
    @(
        [pscustomobject]@{ instance_id = 'USB\VID_CAFE&PID_4009&MI_00\A'; parent = 'USB\VID_CAFE&PID_4009\000100020003000400050006'; class = 'Ports'; status = 'OK' },
        [pscustomobject]@{ instance_id = 'USB\VID_CAFE&PID_4009\000100020003000400050006'; parent = 'USB\VID_0BDA&PID_5411\1&SYNTHETIC&0&1'; class = 'USB'; status = 'OK' }
    )
}
$serial = '000100020003000400050006'; $hubParent = 'USB\VID_0BDA&PID_5411\1&SYNTHETIC&0&1'; $build = '257d051c392fec93'
Assert-ThyTelemetryIdentity (New-Chain) $serial $hubParent
Assert-ThyTelemetryIdentity (New-Chain) $serial.ToLowerInvariant() $hubParent.ToLowerInvariant()
Expect-Reject { Assert-ThyTelemetryIdentity (New-Chain) ('F' * 24) $hubParent } 'wrong physical serial'
Expect-Reject { Assert-ThyTelemetryIdentity (New-Chain) $serial 'USB\OTHER-HUB' } 'same USB identity on another hub'
Expect-Reject { $chain = New-Chain; $chain[0].instance_id = 'USB\VID_CAFE&PID_40090&MI_00\A'; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'partial PID must not match'
Expect-Reject { $chain = New-Chain; $chain[0].instance_id = 'USB\VID_CAFE&PID_4009&MI_03\A'; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'MIDI interface is not CDC'
Expect-Reject { $chain = New-Chain; $chain[0].class = 'MEDIA'; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'incorrect interface class'
Expect-Reject { $chain = New-Chain; $chain[1].status = 'Error'; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'unhealthy physical node'
Expect-Reject { $chain = New-Chain; $chain[0].parent = 'USB\UNRELATED'; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'unrelated parent chain'
Expect-Reject { $chain = New-Chain; $chain[1].instance_id = $chain[0].instance_id; Assert-ThyTelemetryIdentity $chain $serial $hubParent } 'cyclic identity chain'

$startupLine = 'STARTUP THYDIAG/1 valid=1 control=0x00000002 ipsr=0 primask=0 basepri=0x00000000 faultmask=0 msp=0x20020000 psp=0x00000000 vtor=0x08040000 mpu_ctrl=0x00000000 cpacr=0x00F00000 fpccr=0xC0000000 aircr=0xFA050000 ccr=0x00000200 icsr=0x00000000 shcsr=0x00000000 cfsr=0x00000000 hfsr=0x00000000 rcc_cr=0x03035683 rcc_cfgr=0x0000940A rcc_csr=0x0E000000 rcc_apb1enr=0x00000000 iwdg_sr=0x00000000 iwdg_pr=0x00000000 iwdg_rlr=0x00000FFF wwdg_cr=0x0000007F wwdg_cfr=0x0000007F'
$startup = ConvertFrom-ThyTelemetryLine $startupLine 'STARTUP THYDIAG/1 '
Assert-ThyStartup $startup
Expect-Reject { $copy = $startup.Clone(); $copy.Remove('rcc_csr'); Assert-ThyStartup $copy } 'incomplete startup snapshot'
foreach ($pair in @(@('valid', '0'), @('ipsr', '3'), @('control', '0x00000001'), @('fpccr', '0xC0000001'))) {
    $copy = $startup.Clone(); $copy[$pair[0]] = $pair[1]
    Expect-Reject { Assert-ThyStartup $copy } ('startup ' + $pair[0])
}
Expect-Reject { ConvertFrom-ThyTelemetryLine 'STARTUP THYDIAG/1 valid=1 valid=0' 'STARTUP THYDIAG/1 ' } 'duplicate field ambiguity'
Expect-Reject { ConvertFrom-ThyTelemetryLine 'ERR unknown-command' 'STARTUP THYDIAG/1 ' } 'wrong reply prefix'
Expect-Reject { Get-ThyTelemetryInteger @{ value = '-1' } value } 'negative integer'
Expect-Reject { Get-ThyTelemetryInteger @{ value = '4294967296' } value } '32-bit overflow'
Expect-Reject { Get-ThyTelemetryInteger @{} value } 'missing integer'

$statusLine = 'STATUS THYDIAG/1 build=257d051c392fec93 uptime_ms=12000 stage=5 errors=0x00000000 hclk=168000000 pclk1=42000000 pclk2=84000000 clock_source=1 usb_clock_ready=1 device_id=0x100F6413 flash_kib=1024 uid=000100020003000400050006 oled_frames=20 usb_initialized=1 usb_mounted=1 cdc_commands=3 core_selftest=0x00000000 fault_exception=0 fault_cfsr=0x00000000 fault_hfsr=0x00000000'
$status = ConvertFrom-ThyTelemetryLine $statusLine 'STATUS THYDIAG/1 '
Assert-ThyStatus $status $build $serial $null
$next = $status.Clone(); $next['uptime_ms'] = '17000'; $next['oled_frames'] = '30'; $next['cdc_commands'] = '9'
Assert-ThyStatus $next $build $serial $status
# First-test peripheral observations do not abort safe telemetry acquisition.
foreach ($peripheralError in @('0x00000004', '0x00000008', '0x00000010', '0x00000020')) {
    $copy = $status.Clone(); $copy['errors'] = $peripheralError
    Assert-ThyStatus $copy $build $serial $null
}
foreach ($pair in @(@('stage', '6'), @('clock_source', '0'), @('hclk', '16000000'), @('usb_clock_ready', '0'), @('errors', '0x00000100'), @('device_id', '0x00000450'), @('flash_kib', '512'), @('fault_exception', '3'), @('fault_cfsr', '0x00000001'), @('fault_hfsr', '0x40000000'), @('core_selftest', '0x00000001'), @('usb_mounted', '0'))) {
    $copy = $status.Clone(); $copy[$pair[0]] = $pair[1]
    Expect-Reject { Assert-ThyStatus $copy $build $serial $null } ('unsafe status ' + $pair[0])
}
Expect-Reject { Assert-ThyStatus $status '0000000000000000' $serial $null } 'wrong build'
Expect-Reject { Assert-ThyStatus $status $build ('F' * 24) $null } 'wrong UID'
foreach ($counter in @('uptime_ms', 'oled_frames', 'cdc_commands')) {
    $copy = $next.Clone(); $copy[$counter] = $status[$counter]
    Expect-Reject { Assert-ThyStatus $copy $build $serial $status } ('stalled counter ' + $counter)
    $copy[$counter] = '0'
    Expect-Reject { Assert-ThyStatus $copy $build $serial $status } ('reset counter ' + $counter)
}
# All four latest attempts work, with a retained startup failure on bank 0.
$i2c = @{ attempted = '0x0000000f'; ok = '0x0000000f'; history = '0x00000001' }
$addresses = @(0x36, 0x35, 0x39, 0x38)
for ($bank = 0; $bank -lt 4; $bank++) {
    $prefix = 'bank' + $bank + '_'
    foreach ($field in @('attempts', 'successes', 'code', 'errors', 'last_code', 'sr1', 'sr2', 'at_ms')) { $i2c[$prefix + $field] = '0' }
    $i2c[$prefix + 'addr'] = [string]$addresses[$bank]
    $i2c[$prefix + 'attempts'] = '10'; $i2c[$prefix + 'successes'] = '10'
    $i2c[$prefix + 'phase'] = 'none'; $i2c[$prefix + 'flags'] = 'NONE'
}
$i2c['bank0_errors'] = '1'; $i2c['bank0_successes'] = '9'
$i2c['bank0_last_code'] = '3'; $i2c['bank0_phase'] = 'addr-write'; $i2c['bank0_sr1'] = '0x00000400'; $i2c['bank0_flags'] = 'NACK'
Assert-ThyI2c $i2c
# Current failure remains an observation, preserving the failed bank details.
$failed = $i2c.Clone(); $failed['ok'] = '0x0000000e'; $failed['bank0_code'] = '3'
Assert-ThyI2c $failed
$unpolled = $i2c.Clone(); $unpolled['attempted'] = '0'; $unpolled['ok'] = '0'; $unpolled['history'] = '0'
Assert-ThyI2c $unpolled
Expect-Reject { $copy = $i2c.Clone(); $copy.Remove('bank3_sr2'); Assert-ThyI2c $copy } 'truncated I2C report'
Expect-Reject { $copy = $i2c.Clone(); $copy['ok'] = '16'; Assert-ThyI2c $copy } 'nonexistent I2C bank'
Expect-Reject { $copy = $i2c.Clone(); $copy['attempted'] = '14'; Assert-ThyI2c $copy } 'healthy bank never attempted'
Expect-Reject { $copy = $i2c.Clone(); $copy['bank0_addr'] = '57'; Assert-ThyI2c $copy } 'bank identity mismatch'
Expect-Reject { $copy = $i2c.Clone(); $copy['bank0_code'] = '3'; Assert-ThyI2c $copy } 'failed result marked healthy'
Expect-Reject { $copy = $failed.Clone(); $copy['bank0_code'] = '0'; Assert-ThyI2c $copy } 'success marked failed'

# Version 3 raw latest success may be an identity read while AA is unready.
$initializing = $failed.Clone(); $initializing['initialized'] = '14'; $initializing['bank0_code'] = '0'
Assert-ThyI2c $initializing
$probing = $unpolled.Clone(); $probing['attempted'] = '15'; $probing['initialized'] = '0'
Assert-ThyI2c $probing
Expect-Reject { $copy = $initializing.Clone(); $copy['initialized'] = '16'; Assert-ThyI2c $copy } 'nonexistent initialized bank'
Expect-Reject { $copy = $initializing.Clone(); $copy['initialized'] = '15'; Assert-ThyI2c $copy } 'initialized bank without valid AA status'
Expect-Reject { $copy = $initializing.Clone(); $copy['bank1_code'] = '3'; Assert-ThyI2c $copy } 'latest raw failure still marked healthy'
Expect-Reject { $copy = $initializing.Clone(); $copy.Remove('initialized'); Assert-ThyI2c $copy } 'legacy two-way code/health gate retained'

$sensorFields = @{ ready = '15'; failed = '0'; readiness = 'bus-and-status'; calibration = 'unverified' }
for ($bank = 0; $bank -lt 4; $bank++) {
    $prefix = 'bank' + $bank + '_'
    $sensorFields[$prefix + 'state'] = 'ready'; $sensorFields[$prefix + 'identity'] = '1'
    $sensorFields[$prefix + 'family'] = '0x9a'; $sensorFields[$prefix + 'device'] = '0x0a05'
    foreach ($name in @('revision', 'ready_ms', 'wake_nacks', 'retries', 'init_failures', 'last_reg', 'resets', 'status_reads', 'status_ms', 'code')) {
        $sensorFields[$prefix + $name] = '0'
    }
}
Assert-ThySensors $sensorFields
foreach ($state in @('settle', 'family', 'identity', 'status')) {
    $copy = $sensorFields.Clone(); $copy['ready'] = '14'; $copy['bank0_state'] = $state; $copy['bank0_code'] = '4'
    $copy['bank0_identity'] = '0'; $copy['bank0_family'] = '0'
    Assert-ThySensors $copy
}
foreach ($state in @('failed', 'unsupported')) {
    $copy = $sensorFields.Clone(); $copy['ready'] = '14'; $copy['failed'] = '1'
    $copy['bank0_state'] = $state; $copy['bank0_code'] = '3'; $copy['bank0_identity'] = '0'; $copy['bank0_family'] = '255'
    Assert-ThySensors $copy
}
Expect-Reject { $copy = $sensorFields.Clone(); $copy.Remove('bank3_code'); Assert-ThySensors $copy } 'truncated sensor final bank/result'
Expect-Reject { $copy = $sensorFields.Clone(); $copy.Remove('calibration'); Assert-ThySensors $copy } 'truncated sensor scope'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['failed'] = '1'; Assert-ThySensors $copy } 'overlapping readiness masks'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['ready'] = '16'; Assert-ThySensors $copy } 'nonexistent sensor bank'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['bank0_family'] = '255'; Assert-ThySensors $copy } 'identity flag with wrong family'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['bank0_device'] = '65536'; Assert-ThySensors $copy } 'sensor device overflow'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['bank0_state'] = 'failed'; Assert-ThySensors $copy } 'readiness mask/state mismatch'
Expect-Reject { $copy = $sensorFields.Clone(); $copy['bank0_code'] = '3'; Assert-ThySensors $copy } 'ready state with error'

$controlFields = @{ valid = '15'; covered = '43'; total = '43'; physical_map = 'observe' }
$knownMasks = @(0x01ca, 0x5fff, 0x1ffb, 0x1ffb); $logicalMasks = @(31, 16383, 4095, 4095)
for ($bank = 0; $bank -lt 4; $bank++) {
    $prefix = 'bank' + $bank + '_'
    $controlFields[$prefix + 'raw'] = [string]$knownMasks[$bank]
    $controlFields[$prefix + 'seen'] = [string]$knownMasks[$bank]
    $controlFields[$prefix + 'known_seen'] = [string]$knownMasks[$bank]
    $controlFields[$prefix + 'logical'] = [string]$logicalMasks[$bank]
    foreach ($name in @('rose', 'fell', 'changes', 'samples', 'changed_ms', 'sample_ms')) { $controlFields[$prefix + $name] = '0' }
}
Assert-ThyControls $controlFields
$unknownControls = $controlFields.Clone(); $unknownControls['bank0_raw'] = [string](0x01ca -bor 0x8000)
$unknownControls['bank0_seen'] = $unknownControls['bank0_raw']; $unknownControls['bank0_rose'] = '32768'
Assert-ThyControls $unknownControls
$invalidControls = $controlFields.Clone(); $invalidControls['valid'] = '14'; $invalidControls['bank0_seen'] = '0'
$invalidControls['bank0_known_seen'] = '0'; $invalidControls['covered'] = '38'
Assert-ThyControls $invalidControls # stale raw while invalid is explicitly retained
Expect-Reject { $copy = $controlFields.Clone(); $copy.Remove('bank3_sample_ms'); Assert-ThyControls $copy } 'truncated control final bank'
Expect-Reject { $copy = $controlFields.Clone(); $copy.Remove('physical_map'); Assert-ThyControls $copy } 'truncated control mapping qualifier'
Expect-Reject { $copy = $controlFields.Clone(); $copy['covered'] = '42'; Assert-ThyControls $copy } 'coverage count mismatch'
Expect-Reject { $copy = $controlFields.Clone(); $copy['total'] = '44'; Assert-ThyControls $copy } 'unknown coverage contract'
Expect-Reject { $copy = $controlFields.Clone(); $copy['bank0_logical'] = '1'; Assert-ThyControls $copy } 'logical descriptor mismatch'
Expect-Reject { $copy = $controlFields.Clone(); $copy['bank0_known_seen'] = '65535'; Assert-ThyControls $copy } 'unknown bits counted as known'
Expect-Reject { $copy = $controlFields.Clone(); $copy['bank0_raw'] = '65536'; Assert-ThyControls $copy } 'raw control overflow'
Expect-Reject { $copy = $controlFields.Clone(); $copy['bank0_rose'] = '32768'; Assert-ThyControls $copy } 'latched edge never observed pressed'

# Execute both actual opt-in blocks with a mocked transport. No automatic LED
# or pad output command is part of initial or periodic sensor observation.
$sensorBlocks = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$IncludeSensorDetails'
}, $true))
if ($sensorBlocks.Count -ne 2) { throw 'Expected initial and periodic opt-in sensor blocks' }
$script:detailCommands = @(); $serialPort = $null; $IncludeSensorDetails = $true
function Send-ThyTelemetryCommand {
    param($SerialPort, [string]$Command, [string]$Prefix)
    $script:detailCommands += $Command
    if ($Command -ceq 'sensors' -and $Prefix -ceq 'SENSORS ') { return $sensorFields }
    if ($Command -ceq 'controls' -and $Prefix -ceq 'CONTROLS ') { return $controlFields }
    throw 'Unexpected automatic detail command'
}
foreach ($block in $sensorBlocks) { Invoke-Expression $block.Extent.Text }
if (($script:detailCommands -join ',') -cne 'sensors,controls,sensors,controls') { throw 'Wrong opt-in sensor command sequence' }
$IncludeSensorDetails = $false; $script:detailCommands = @()
foreach ($block in $sensorBlocks) { Invoke-Expression $block.Extent.Text }
if ($script:detailCommands.Count -ne 0) { throw 'Sensor commands were not opt-in' }

# Execute the actual periodic loop AST with a synthetic clock and mocked
# waits/transport. The old truncated final wait produced a 56 ms extra sample
# in the captured 60-second observation, falsely failing the OLED gate.
$periodicLoops = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.WhileStatementAst] -and
    $node.Condition.Extent.Text -match 'stableClock.Elapsed.TotalSeconds'
}, $true))
if ($periodicLoops.Count -ne 1) { throw 'Expected one periodic telemetry loop' }
$script:syntheticClock = [pscustomobject]@{ Elapsed = [pscustomobject]@{ TotalSeconds = 0.001 } }
$stableClock = $script:syntheticClock; $DurationSeconds = 60; $PollSeconds = 5
$IncludeI2cDetails = $false; $IncludeSensorDetails = $false; $serialPort = $null
$ExpectedBuild = $build; $ExpectedSerial = $serial; $statusCount = 1
$previous = $status.Clone(); $previous['uptime_ms'] = '12001'; $previous['oled_frames'] = '24'
$script:syntheticBaseline = $status.Clone(); $script:syntheticCommandCount = 3
$script:waitSeconds = @(); $script:sampleTimes = @()
function Start-Sleep {
    param([double]$Seconds, [int]$Milliseconds)
    if ($PSBoundParameters.ContainsKey('Milliseconds')) { throw 'Periodic loop used a truncated final wait' }
    if ($Seconds -ne 5) { throw 'Periodic loop did not wait a complete polling interval' }
    $script:waitSeconds += $Seconds
    $script:syntheticClock.Elapsed.TotalSeconds += $Seconds
}
function Send-ThyTelemetryCommand {
    param($SerialPort, [string]$Command, [string]$Prefix)
    if ($Command -ne 'status' -or $Prefix -ne 'STATUS THYDIAG/1 ') { throw 'Unexpected synthetic command' }
    $script:syntheticClock.Elapsed.TotalSeconds += 0.04
    $script:sampleTimes += $script:syntheticClock.Elapsed.TotalSeconds
    $copy = $script:syntheticBaseline.Clone()
    $copy['uptime_ms'] = [string][int](12000 + 1000 * $script:syntheticClock.Elapsed.TotalSeconds)
    $copy['oled_frames'] = [string][int][Math]::Floor(([double]$copy['uptime_ms']) / 500)
    $script:syntheticCommandCount++
    $copy['cdc_commands'] = [string]$script:syntheticCommandCount
    return $copy
}
Invoke-Expression $periodicLoops[0].Extent.Text
if ($script:sampleTimes.Count -ne 12 -or $script:waitSeconds.Count -ne 12) { throw 'Unexpected periodic sample count' }
for ($i = 1; $i -lt $script:sampleTimes.Count; $i++) {
    if (($script:sampleTimes[$i] - $script:sampleTimes[$i - 1]) -lt 5) { throw 'Rapid duplicate final poll' }
}
if ($script:sampleTimes[-1] -lt 60 -or $script:sampleTimes[-1] -gt 65) { throw 'Synthetic observation duration outside one-poll bound' }
'PASS: collector syntax, greetings, identity/startup/status/I2C/sensor/control gates, opt-in sequence and actual periodic-loop cadence with mocked clock/waits/transport; no device API called.'
