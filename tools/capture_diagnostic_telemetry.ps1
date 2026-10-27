# Bounded first-test telemetry only. This script opens only an explicitly bound
# CAFE:4009 CDC port. It never starts an updater or requests device-memory reads.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(10, 120)][int]$DurationSeconds = 60,
    [ValidateRange(1, 10)][int]$PollSeconds = 5,
    [switch]$IncludeI2cDetails,
    [switch]$IncludeSensorDetails
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{16}$'


function ConvertFrom-ThyTelemetryLine {
    param([string]$Line, [string]$Prefix)
    if (-not $Line.StartsWith($Prefix, [StringComparison]::Ordinal)) { throw "Unexpected reply; expected $Prefix" }
    $fields = @{}
    foreach ($match in [regex]::Matches($Line, '(?:^| )([a-zA-Z0-9_]+)=([^ ]+)')) {
        if ($fields.ContainsKey($match.Groups[1].Value)) { throw 'Duplicate telemetry field' }
        $fields[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    return $fields
}

function Get-ThyTelemetryInteger {
    param([hashtable]$Fields, [string]$Name)
    if (-not $Fields.ContainsKey($Name)) { throw "Missing telemetry field $Name" }
    $value = $Fields[$Name]
    if ($value -match '^0x[0-9A-Fa-f]{1,8}$') { return [Convert]::ToUInt32($value.Substring(2), 16) }
    if ($value -match '^[0-9]{1,10}$') { return [Convert]::ToUInt32($value, 10) }
    throw "Invalid integer field $Name"
}

function Assert-ThyGreeting {
    param([string]$Line)
    if ($Line -cnotin @('HELLO THYDIAG/1 diagnostic; type help', 'HELLO THYDIAG/1 read-only candidate; type help')) {
        throw 'Unexpected greeting; no commands sent'
    }
}

function Assert-ThyTelemetryIdentity {
    param([object[]]$Chain, [string]$Serial, [string]$HubParent)
    if ($Chain.Count -lt 2 -or $Chain.Count -gt 8) { throw 'Invalid CDC parent chain length' }
    $seen = @{}
    for ($i = 0; $i -lt $Chain.Count; $i++) {
        $node = $Chain[$i]
        if ($node.status -ne 'OK' -or $seen.ContainsKey($node.instance_id)) { throw 'Unavailable or cyclic CDC parent chain' }
        $seen[$node.instance_id] = $true
        if ($i -lt $Chain.Count - 1 -and $node.parent -ne $Chain[$i + 1].instance_id) { throw 'Broken CDC parent chain' }
    }
    if ($Chain[0].class -ne 'Ports' -or $Chain[0].instance_id -notmatch '^USB\\VID_CAFE&PID_4009&MI_00\\') {
        throw 'Selected COM port is not the diagnostic CDC interface'
    }
    $physical = $Chain[-1]
    if ($physical.instance_id -ne ('USB\VID_CAFE&PID_4009\' + $Serial) -or $physical.parent -ne $HubParent) {
        throw 'Diagnostic physical serial or hub parent does not match the explicit binding'
    }
}

function Assert-ThyStartup {
    param([hashtable]$Fields)
    # Preserve the complete snapshot, including fields whose inherited values
    # are evidence rather than assumptions. Reject a silently truncated reply.
    foreach ($name in @('valid', 'control', 'ipsr', 'primask', 'basepri', 'faultmask', 'msp', 'psp', 'vtor', 'mpu_ctrl',
        'cpacr', 'fpccr', 'aircr', 'ccr', 'icsr', 'shcsr', 'cfsr', 'hfsr', 'rcc_cr', 'rcc_cfgr', 'rcc_csr', 'rcc_apb1enr',
        'iwdg_sr', 'iwdg_pr', 'iwdg_rlr', 'wwdg_cr', 'wwdg_cfr')) { $null = Get-ThyTelemetryInteger $Fields $name }
    if ((Get-ThyTelemetryInteger $Fields valid) -ne 1 -or (Get-ThyTelemetryInteger $Fields ipsr) -ne 0 -or
        ((Get-ThyTelemetryInteger $Fields control) -band 1) -ne 0 -or
        ((Get-ThyTelemetryInteger $Fields fpccr) -band 1) -ne 0) { throw 'Unsupported or invalid incoming startup state; stop probing' }
}

function Assert-ThyStatus {
    param([hashtable]$Fields, [string]$Build, [string]$Serial, [hashtable]$Previous)
    if ($Fields['build'] -ne $Build -or $Fields['uid'] -ne $Serial) { throw 'Build or UID mismatch; stop probing' }
    if ((Get-ThyTelemetryInteger $Fields stage) -ne 5 -or
        (Get-ThyTelemetryInteger $Fields clock_source) -ne 1 -or
        (Get-ThyTelemetryInteger $Fields usb_clock_ready) -ne 1 -or
        (Get-ThyTelemetryInteger $Fields hclk) -ne 168000000 -or
        (Get-ThyTelemetryInteger $Fields pclk1) -ne 42000000 -or
        (Get-ThyTelemetryInteger $Fields pclk2) -ne 84000000 -or
        ((Get-ThyTelemetryInteger $Fields device_id) -band 0xFFF) -ne 0x413 -or
        (Get-ThyTelemetryInteger $Fields flash_kib) -ne 1024 -or
        (Get-ThyTelemetryInteger $Fields usb_initialized) -ne 1 -or
        (Get-ThyTelemetryInteger $Fields usb_mounted) -ne 1 -or
        ((Get-ThyTelemetryInteger $Fields errors) -band 0x1C3) -ne 0) { throw 'Unexpected startup, clock, chip or USB status; stop probing' }
    foreach ($name in @('core_selftest', 'fault_exception', 'fault_cfsr', 'fault_hfsr')) {
        if ((Get-ThyTelemetryInteger $Fields $name) -ne 0) { throw "Fault/self-test field $name is nonzero; stop probing" }
    }
    if ($null -ne $Previous) {
        foreach ($name in @('uptime_ms', 'oled_frames', 'cdc_commands')) {
            if ((Get-ThyTelemetryInteger $Fields $name) -le (Get-ThyTelemetryInteger $Previous $name)) {
                throw "Counter $name did not advance; possible reset or stalled loop"
            }
        }
    }
}

function Assert-ThyI2c {
    param([hashtable]$Fields)
    $attempted = Get-ThyTelemetryInteger $Fields attempted
    $ok = Get-ThyTelemetryInteger $Fields ok
    $history = Get-ThyTelemetryInteger $Fields history
    if ($attempted -gt 15 -or $ok -gt 15 -or $history -gt 15 -or
        ($ok -band $attempted) -ne $ok -or ($history -band $attempted) -ne $history) {
        throw 'Invalid I2C bank masks'
    }
    $hasReadiness = $Fields.ContainsKey('initialized')
    if ($hasReadiness) {
        $initialized = Get-ThyTelemetryInteger $Fields initialized
        if ($initialized -gt 15 -or ($initialized -band $ok) -ne $initialized) {
            throw 'Invalid I2C initialized bank mask'
        }
    }
    $addresses = @(0x36, 0x35, 0x39, 0x38)
    for ($bank = 0; $bank -lt 4; $bank++) {
        $prefix = 'bank' + $bank + '_'
        foreach ($name in @('addr', 'attempts', 'successes', 'code', 'errors', 'last_code', 'sr1', 'sr2', 'at_ms')) {
            $null = Get-ThyTelemetryInteger $Fields ($prefix + $name)
        }
        if ((Get-ThyTelemetryInteger $Fields ($prefix + 'addr')) -ne $addresses[$bank] -or
            -not $Fields.ContainsKey($prefix + 'phase') -or -not $Fields.ContainsKey($prefix + 'flags')) {
            throw 'Incomplete or mismatched I2C bank observation'
        }
        # A historical failure is retained after a successful latest attempt.
        # Do not turn a bus failure into an unsafe retry or abort telemetry.
        $bit = 1 -shl $bank
        $code = Get-ThyTelemetryInteger $Fields ($prefix + 'code')
        # Version 3 probes identity before a successful AA status sample.
        # A successful raw identity transaction does not make that bank ready.
        # Older reports lack initialized and retain the original two-way gate.
        if (($attempted -band $bit) -ne 0 -and
            ((($ok -band $bit) -ne 0 -and $code -ne 0) -or
             (-not $hasReadiness -and ($ok -band $bit) -eq 0 -and $code -eq 0))) {
            throw 'I2C latest result disagrees with its health mask'
        }
    }
}

function Assert-ThySensors {
    param([hashtable]$Fields)
    $ready = Get-ThyTelemetryInteger $Fields ready
    $failed = Get-ThyTelemetryInteger $Fields failed
    if ($ready -gt 15 -or $failed -gt 15 -or ($ready -band $failed) -ne 0 -or
        $Fields['readiness'] -cne 'bus-and-status' -or $Fields['calibration'] -cne 'unverified') {
        throw 'Invalid sensor readiness summary'
    }
    for ($bank = 0; $bank -lt 4; $bank++) {
        $prefix = 'bank' + $bank + '_'
        foreach ($name in @('identity', 'family', 'device', 'revision', 'ready_ms', 'wake_nacks', 'retries',
            'init_failures', 'last_reg', 'resets', 'status_reads', 'status_ms', 'code')) {
            $null = Get-ThyTelemetryInteger $Fields ($prefix + $name)
        }
        $state = $Fields[$prefix + 'state']
        $identity = Get-ThyTelemetryInteger $Fields ($prefix + 'identity')
        $family = Get-ThyTelemetryInteger $Fields ($prefix + 'family')
        $code = Get-ThyTelemetryInteger $Fields ($prefix + 'code')
        $bit = 1 -shl $bank
        if ($state -cnotin @('settle', 'family', 'identity', 'status', 'ready', 'failed', 'unsupported') -or
            $identity -gt 1 -or $family -gt 255 -or ($identity -eq 1 -and $family -ne 0x9A) -or
            (Get-ThyTelemetryInteger $Fields ($prefix + 'device')) -gt 65535 -or
            (Get-ThyTelemetryInteger $Fields ($prefix + 'revision')) -gt 255 -or
            (Get-ThyTelemetryInteger $Fields ($prefix + 'last_reg')) -gt 255 -or $code -gt 4) {
            throw 'Incomplete or invalid sensor bank observation'
        }
        $isReady = $state -ceq 'ready'
        $isFailed = $state -cin @('failed', 'unsupported')
        if ((($ready -band $bit) -ne 0) -ne $isReady -or
            ((($failed -band $bit) -ne 0) -ne $isFailed) -or
            ($isReady -and ($identity -ne 1 -or $code -ne 0)) -or
            ($isFailed -and $code -eq 0) -or
            (-not $isReady -and -not $isFailed -and $code -ne 4)) {
            throw 'Sensor state disagrees with readiness masks or result'
        }
        # Failed, unsupported and still-probing banks are useful observations.
        # Do not turn a peripheral observation into a reset or stop collection.
    }
}

function Assert-ThyControls {
    param([hashtable]$Fields)
    $valid = Get-ThyTelemetryInteger $Fields valid
    $covered = Get-ThyTelemetryInteger $Fields covered
    if ($valid -gt 15 -or (Get-ThyTelemetryInteger $Fields total) -ne 43 -or $covered -gt 43 -or
        $Fields['physical_map'] -cne 'observe') { throw 'Invalid control coverage summary' }
    $descriptors = @(
        @(0x100, 0x80, 0x40, 2, 8),
        @(0x40, 0x20, 1, 2, 0x80, 0x10, 8, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x4000, 4),
        @(8, 2, 1, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000),
        @(8, 2, 1, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000)
    )
    $coverageCount = 0
    for ($bank = 0; $bank -lt 4; $bank++) {
        $prefix = 'bank' + $bank + '_'
        foreach ($name in @('raw', 'seen', 'rose', 'fell', 'logical', 'known_seen')) {
            if ((Get-ThyTelemetryInteger $Fields ($prefix + $name)) -gt 65535) { throw 'Invalid raw control mask' }
        }
        foreach ($name in @('changes', 'samples', 'changed_ms', 'sample_ms')) {
            $null = Get-ThyTelemetryInteger $Fields ($prefix + $name)
        }
        $raw = Get-ThyTelemetryInteger $Fields ($prefix + 'raw')
        $seen = Get-ThyTelemetryInteger $Fields ($prefix + 'seen')
        $knownSeen = Get-ThyTelemetryInteger $Fields ($prefix + 'known_seen')
        $knownMask = 0; $logical = 0
        for ($index = 0; $index -lt $descriptors[$bank].Count; $index++) {
            $descriptor = $descriptors[$bank][$index]
            $knownMask = $knownMask -bor $descriptor
            if (($raw -band $descriptor) -ne 0) { $logical = $logical -bor (1 -shl $index) }
            if (($seen -band $descriptor) -ne 0) { $coverageCount++ }
        }
        if ($knownSeen -ne ($seen -band $knownMask) -or
            (Get-ThyTelemetryInteger $Fields ($prefix + 'logical')) -ne $logical -or
            (($valid -band (1 -shl $bank)) -ne 0 -and ($seen -band $raw) -ne $raw) -or
            ((Get-ThyTelemetryInteger $Fields ($prefix + 'rose')) -band $seen) -ne (Get-ThyTelemetryInteger $Fields ($prefix + 'rose')) -or
            ((Get-ThyTelemetryInteger $Fields ($prefix + 'fell')) -band $seen) -ne (Get-ThyTelemetryInteger $Fields ($prefix + 'fell'))) {
            throw 'Control raw masks, logical mapping or retained history disagree'
        }
    }
    if ($covered -ne $coverageCount) { throw 'Control coverage count disagrees with bank observations' }
}

function Get-ThyLiveCdcBinding {
    param([string]$PortName, [string]$Serial, [string]$HubParent)
    $present = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
    $portNodes = @($present | Where-Object { $_.Class -eq 'Ports' -and $_.FriendlyName -match ('\(' + [regex]::Escape($PortName) + '\)$') })
    if ($portNodes.Count -ne 1) { throw 'Explicit COM port does not identify exactly one present Ports node' }
    $chain = @(); $current = $portNodes[0]
    for ($depth = 0; $depth -lt 8; $depth++) {
        $parent = (Get-PnpDeviceProperty -InstanceId $current.InstanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop).Data
        $chain += [pscustomobject]@{ instance_id = $current.InstanceId; parent = $parent; class = $current.Class; status = $current.Status; friendly_name = $current.FriendlyName }
        if ($current.InstanceId -match '^USB\\VID_CAFE&PID_4009\\') { break }
        $next = @($present | Where-Object { $_.InstanceId -eq $parent })
        if ($next.Count -ne 1) { throw 'CDC physical parent is missing or ambiguous' }
        $current = $next[0]
    }
    Assert-ThyTelemetryIdentity $chain $Serial $HubParent
    return ,$chain
}

function Read-ThyConsoleLine {
    param([IO.Ports.SerialPort]$SerialPort)
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $bytes = [Collections.Generic.List[byte]]::new()
    try {
        while ($deadline.Elapsed.TotalSeconds -lt 5) {
            try { $value = $SerialPort.ReadByte() } catch [TimeoutException] { continue }
            if ($value -lt 0) { throw 'CDC stream closed' }
            $bytes.Add([byte]$value)
            if ($bytes.Count -gt 4096) { throw 'CDC line exceeds the bounded reply size' }
            if ($value -eq 10) {
                if ($bytes.Count -lt 2 -or $bytes[$bytes.Count - 2] -ne 13) { throw 'Expected complete CRLF framing' }
                foreach ($character in $bytes.GetRange(0, $bytes.Count - 2)) {
                    if ($character -lt 32 -or $character -gt 126) { throw 'Non-ASCII diagnostic reply' }
                }
                $line = [Text.Encoding]::ASCII.GetString($bytes.ToArray(), 0, $bytes.Count - 2)
                Write-ThyTelemetryEvent 'rx' @{ line = $line; hex = [BitConverter]::ToString($bytes.ToArray()).Replace('-', '').ToLowerInvariant() }
                return $line
            }
        }
        throw 'Timed out waiting for a complete CDC reply'
    } catch {
        if ($bytes.Count) { Write-ThyTelemetryEvent 'partial-rx' @{ hex = [BitConverter]::ToString($bytes.ToArray()).Replace('-', '').ToLowerInvariant() } }
        throw
    }
}

function Send-ThyTelemetryCommand {
    param([IO.Ports.SerialPort]$SerialPort, [string]$Command, [string]$Prefix)
    if ($Command -cnotin @('help', 'startup', 'status', 'core-test', 'touch', 'i2c', 'sensors', 'controls', 'led status', 'adc', 'midi')) { throw 'Command is outside the telemetry-only allowlist' }
    Write-ThyTelemetryEvent 'tx-intent' @{ line = $Command }
    $SerialPort.Write($Command + "`n")
    Write-ThyTelemetryEvent 'tx-complete' @{ line = $Command }
    $line = Read-ThyConsoleLine $SerialPort
    return (ConvertFrom-ThyTelemetryLine $line $Prefix)
}

function Write-ThyTelemetryEvent {
    param([string]$Kind, [object]$Data)
    $event = [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); elapsed_ms = $script:observationClock.ElapsedMilliseconds; kind = $Kind; data = $Data }
    $script:eventWriter.WriteLine(($event | ConvertTo-Json -Depth 16 -Compress))
    $script:eventWriter.Flush()
}

$ErrorActionPreference = 'Stop'
$workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$allowedPath = [IO.Path]::GetFullPath((Join-Path $workspacePath 'analysis/local')) + [IO.Path]::DirectorySeparatorChar
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $outputPath.StartsWith($allowedPath, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output directory must be a new directory within analysis/local' }
if (Test-Path -LiteralPath $outputPath) { throw 'Observation directory already exists; never overwrite evidence' }
$binding = Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop | Out-Null
$eventsPath = Join-Path $outputPath 'events.jsonl'
$eventStream = [IO.File]::Open($eventsPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$script:eventWriter = [IO.StreamWriter]::new($eventStream, [Text.UTF8Encoding]::new($false))
$script:observationClock = [Diagnostics.Stopwatch]::StartNew()
$serialPort = $null; $failure = $null; $completed = $false; $statusCount = 0; $postBinding = $null
$report = [ordered]@{
    format = 'thy666.diagnostic-telemetry'; version = 1; started_utc = [DateTime]::UtcNow.ToString('o')
    port = $Port; expected_serial = $ExpectedSerial; expected_hub_parent = $ExpectedHubParent; expected_build = $ExpectedBuild
    requested_duration_seconds = $DurationSeconds; poll_seconds = $PollSeconds; identity_before_open = $binding
    helper_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    allowed_commands = @('help', 'startup', 'status', 'core-test', 'touch', 'adc', 'midi') + @('i2c' | Where-Object { $IncludeI2cDetails }) + @('sensors', 'controls' | Where-Object { $IncludeSensorDetails })
    include_i2c_details = [bool]$IncludeI2cDetails
    include_sensor_details = [bool]$IncludeSensorDetails
    scope = 'CDC line coding and DTR control; bounded telemetry and RAM-only core self-test. No memory read, program, erase, reset or MIDI transmit command.'
    installation_full_image_verified = $false; backup_verified = $false; independent_recovery_verified = $false
}
try {
    Write-ThyTelemetryEvent 'binding' $binding
    $serialPort = [IO.Ports.SerialPort]::new($Port, 115200, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
    $serialPort.Handshake = [IO.Ports.Handshake]::None
    $serialPort.DtrEnable = $true; $serialPort.RtsEnable = $false
    $serialPort.ReadTimeout = 200; $serialPort.WriteTimeout = 2000
    # Recheck the explicit binding immediately before opening. No port auto-selection.
    $null = Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open()
    Write-ThyTelemetryEvent 'opened' @{ dtr = $true; baud = 115200 }
    $greeting = Read-ThyConsoleLine $serialPort
    Assert-ThyGreeting $greeting
    $null = Send-ThyTelemetryCommand $serialPort 'help' 'OK THYDIAG/1 '
    $startup = Send-ThyTelemetryCommand $serialPort 'startup' 'STARTUP THYDIAG/1 '
    Assert-ThyStartup $startup
    $previous = Send-ThyTelemetryCommand $serialPort 'status' 'STATUS THYDIAG/1 '
    Assert-ThyStatus $previous $ExpectedBuild $ExpectedSerial $null
    $statusCount++
    $core = Send-ThyTelemetryCommand $serialPort 'core-test' 'CORE PASS '
    if ((Get-ThyTelemetryInteger $core failures) -ne 0 -or $core['scope'] -ne 'ram-only') { throw 'RAM core self-test failed' }
    $null = Send-ThyTelemetryCommand $serialPort 'touch' 'TOUCH '
    if ($IncludeI2cDetails) {
        $i2c = Send-ThyTelemetryCommand $serialPort 'i2c' 'I2C '
        Assert-ThyI2c $i2c
    }
    if ($IncludeSensorDetails) {
        $sensors = Send-ThyTelemetryCommand $serialPort 'sensors' 'SENSORS '
        Assert-ThySensors $sensors
        $controls = Send-ThyTelemetryCommand $serialPort 'controls' 'CONTROLS '
        Assert-ThyControls $controls
    }
    $null = Send-ThyTelemetryCommand $serialPort 'adc' 'ADC '
    $null = Send-ThyTelemetryCommand $serialPort 'midi' 'MIDI '
    $stableClock = [Diagnostics.Stopwatch]::StartNew()
    while ($stableClock.Elapsed.TotalSeconds -lt $DurationSeconds) {
        # Keep every sample at least one full polling interval apart. A
        # truncated final wait can become zero and falsely require another
        # OLED frame within its 500 ms refresh period. The final sample may
        # extend observation by at most one poll interval plus command time.
        Start-Sleep -Seconds $PollSeconds
        $currentStatus = Send-ThyTelemetryCommand $serialPort 'status' 'STATUS THYDIAG/1 '
        Assert-ThyStatus $currentStatus $ExpectedBuild $ExpectedSerial $previous
        $previous = $currentStatus; $statusCount++
        if ($IncludeI2cDetails) {
            $i2c = Send-ThyTelemetryCommand $serialPort 'i2c' 'I2C '
            Assert-ThyI2c $i2c
        }
        if ($IncludeSensorDetails) {
            $sensors = Send-ThyTelemetryCommand $serialPort 'sensors' 'SENSORS '
            Assert-ThySensors $sensors
            $controls = Send-ThyTelemetryCommand $serialPort 'controls' 'CONTROLS '
            Assert-ThyControls $controls
        }
    }
    $postBinding = Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThyTelemetryEvent 'binding-after' $postBinding
    $completed = $true
} catch {
    $failure = $_.Exception.Message
    Write-ThyTelemetryEvent 'failure' @{ message = $failure }
} finally {
    if ($null -ne $serialPort) {
        try { if ($serialPort.IsOpen) { $serialPort.Close() }; $serialPort.Dispose() } catch {
            Write-ThyTelemetryEvent 'close-failure' @{ message = $_.Exception.Message }
            $completed = $false
            if ($null -eq $failure) { $failure = 'Serial close/disposal failed: ' + $_.Exception.Message }
        }
    }
    $report['completed'] = $completed; $report['failure'] = $failure; $report['status_samples'] = $statusCount
    $report['identity_after_observation'] = $postBinding
    $report['ended_utc'] = [DateTime]::UtcNow.ToString('o'); $report['elapsed_ms'] = $script:observationClock.ElapsedMilliseconds
    $script:eventWriter.Dispose(); $eventStream.Dispose()
    $report['events_sha256'] = (Get-FileHash -LiteralPath $eventsPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $summaryPath = Join-Path $outputPath 'observation.json'
    $summaryStream = [IO.File]::Open($summaryPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $summaryWriter = [IO.StreamWriter]::new($summaryStream, [Text.UTF8Encoding]::new($false))
    try { $summaryWriter.WriteLine(($report | ConvertTo-Json -Depth 18)) } finally { $summaryWriter.Dispose(); $summaryStream.Dispose() }
}
[pscustomobject]@{ path = $outputPath; completed = $completed; status_samples = $statusCount; failure = $failure } | ConvertTo-Json
if (-not $completed) { throw "Observation stopped: $failure. Evidence preserved at $outputPath" }
