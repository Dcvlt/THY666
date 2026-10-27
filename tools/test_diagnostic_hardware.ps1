# Explicit opt-in volatile hardware tests for the fourth diagnostic only.
# The existing telemetry collector remains read-only and is never executed.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='',
    [ValidatePattern('^[0-9A-Fa-f]{24}$')][string]$ExpectedSerial='',
    [ValidateNotNullOrEmpty()][string]$ExpectedHubParent='',
    [ValidatePattern('^[0-9A-Fa-f]{16}$')][string]$ExpectedBuild='',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][ValidateSet('Controls','Led','Pads')][string]$Test,
    [ValidateRange(60,120)][int]$DurationSeconds=60,
    [switch]$ClearControls,
    [ValidateSet('All','Walk','Pixel','Off')][string]$LedMode='All',
    [ValidateRange(0,2)][int]$Component=0,
    [ValidateRange(0,95)][int]$Pixel=0
)

# Explicit arguments override process configuration and the ignored root .env.
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$Port = Resolve-ThyLocalValue -Name 'THY666_SERIAL_PORT' -Value $Port -Explicit:($PSBoundParameters.ContainsKey('Port')) -Configuration $localConfiguration -Required -Pattern '^COM[1-9][0-9]*$'
$ExpectedSerial = Resolve-ThyLocalValue -Name 'THY666_DEVICE_SERIAL' -Value $ExpectedSerial -Explicit:($PSBoundParameters.ContainsKey('ExpectedSerial')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{24}$'
$ExpectedHubParent = Resolve-ThyLocalValue -Name 'THY666_USB_HUB_PARENT' -Value $ExpectedHubParent -Explicit:($PSBoundParameters.ContainsKey('ExpectedHubParent')) -Configuration $localConfiguration -Required
$ExpectedBuild = Resolve-ThyLocalValue -Name 'THY666_EXPECTED_BUILD' -Value $ExpectedBuild -Explicit:($PSBoundParameters.ContainsKey('ExpectedBuild')) -Configuration $localConfiguration -Required -Pattern '^[0-9A-Fa-f]{16}$'


function Get-ThyHardwareMilliseconds { return $script:observationClock.ElapsedMilliseconds }
function Wait-ThyHardwareMilliseconds([int]$Milliseconds) { Start-Sleep -Milliseconds $Milliseconds }

function Assert-ThyCenterStatus {
    param([hashtable]$Fields)
    foreach($name in @('state','mode','channel','brightness','enabled_mask','frames','errors','updated_ms','code')) {
        $null=Get-ThyTelemetryInteger $Fields $name
    }
    if((Get-ThyTelemetryInteger $Fields state) -gt 3 -or (Get-ThyTelemetryInteger $Fields mode) -gt 2 -or
        (Get-ThyTelemetryInteger $Fields channel) -gt 23 -or (Get-ThyTelemetryInteger $Fields brightness) -gt 4095 -or
        ((Get-ThyTelemetryInteger $Fields enabled_mask) -band 0xff00001f) -ne 0 -or
        (Get-ThyTelemetryInteger $Fields code) -gt 4 -or $Fields['driver'] -cne 'cpu' -or
        $Fields['lighting_dma'] -cne 'none' -or $Fields['nvm_writes'] -cne 'none') {
        throw 'Malformed or unexpected fourth-diagnostic center observation'
    }
}

function Send-ThyHardwareCommand {
    param([object]$SerialPort,[string]$Command,[string]$Prefix)
    $fixed=@('startup','status','controls','controls clear','led status','led off','pads status','pads start','pads stop','adc','center status')
    if ($Command -cnotin $fixed -and
        $Command -cnotmatch '^led (all|walk) [0-2]$' -and
        $Command -cnotmatch '^led pixel ([0-9]|[1-8][0-9]|9[0-5]) [0-2]$' -and
        $Command -cnotmatch '^pads bank [0-5]$') { throw 'Command is outside the bounded hardware-test allowlist' }
    if ($Command -cnotin @('startup','status') -and -not $script:hardwareBound) {
        throw 'Fresh greeting, startup and exact build/UID binding are required before hardware commands'
    }
    $bytes=[Text.Encoding]::ASCII.GetBytes($Command+"`n")
    $hex=[BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant()
    Write-ThyTelemetryEvent 'tx-intent' @{line=$Command;hex=$hex}
    $SerialPort.Write($Command+"`n")
    Write-ThyTelemetryEvent 'tx-complete' @{line=$Command;hex=$hex}
    $line=Read-ThyConsoleLine $SerialPort
    if ($line.StartsWith('ERR ',[StringComparison]::Ordinal)) {
        Write-ThyTelemetryEvent 'device-error' @{command=$Command;line=$line}
        throw ('Device rejected '+$Command+': '+$line)
    }
    return (ConvertFrom-ThyTelemetryLine $line $Prefix)
}

function Invoke-ThyHardwareSession {
    param([object]$SerialPort,[string]$Test,[string]$Build,[string]$Serial,
          [int]$DurationSeconds=60,[bool]$ClearControls=$false,
          [string]$LedMode='All',[int]$Component=0,[int]$Pixel=0)
    $script:hardwareBound=$false
    if ($Build -cne '0d99dc2374abf123') { throw 'This hardware helper is bound to fourth diagnostic build 0d99dc2374abf123' }
    if ($Test -cnotin @('Controls','Led','Pads') -or $DurationSeconds -lt 60 -or $DurationSeconds -gt 120 -or
        $LedMode -cnotin @('All','Walk','Pixel','Off') -or $Component -lt 0 -or $Component -gt 2 -or $Pixel -lt 0 -or $Pixel -gt 95) {
        throw 'Invalid bounded hardware test arguments'
    }
    $result=[ordered]@{completed=$false;failure=$null;cleanup_completed=$false;firmware_checks_passed=$false;
        optical_verification='owner-required';test=$Test;observations=0;final=$null;final_adc=$null;
        center_before=$null;center_after=$null;device_actions=@()}
    try {
        $greeting=Read-ThyConsoleLine $SerialPort
        Assert-ThyGreeting $greeting
        $startup=Send-ThyHardwareCommand $SerialPort startup 'STARTUP THYDIAG/1 '
        Assert-ThyStartup $startup
        $previous=Send-ThyHardwareCommand $SerialPort status 'STATUS THYDIAG/1 '
        Assert-ThyStatus $previous $Build $Serial $null
        $script:hardwareBound=$true
        Write-ThyTelemetryEvent 'bound' @{build=$Build;uid=$Serial;greeting=$greeting}
        $result.center_before=Send-ThyHardwareCommand $SerialPort 'center status' 'CENTER '
        Assert-ThyCenterStatus $result.center_before
        if ($Test -ceq 'Controls') {
            if ($ClearControls) {
                $controls=Send-ThyHardwareCommand $SerialPort 'controls clear' 'CONTROLS '
                Assert-ThyControls $controls
                $result.device_actions+= 'controls clear'
            }
            $window=$DurationSeconds*1000
        } elseif ($Test -ceq 'Led') {
            $command=switch($LedMode) {
                'Off' {'led off'}
                'All' {'led all '+$Component}
                'Walk' {'led walk '+$Component}
                'Pixel' {'led pixel '+$Pixel+' '+$Component}
            }
            $result.initial=Send-ThyHardwareCommand $SerialPort $command 'LED '
            $result.device_actions+=$command
            $window=if($LedMode -ceq 'Off') {1000} else {10000}
        } else {
            $result.pad_profile_before=Send-ThyHardwareCommand $SerialPort 'pads status' 'PADS '
            if((Get-ThyTelemetryInteger $result.pad_profile_before lighting_decoupled) -ne 1) {
                throw 'Fourth-diagnostic pad acquisition is not decoupled from lighting'
            }
            $result.initial=Send-ThyHardwareCommand $SerialPort 'pads start' 'PADS '
            if((Get-ThyTelemetryInteger $result.initial lighting_decoupled) -ne 1) {
                throw 'Pad start did not retain the lighting-decoupled profile'
            }
            $result.device_actions+='pads start'
            $window=10000
        }
        $start=Get-ThyHardwareMilliseconds
        $statusAt=$start+5000
        do {
            Wait-ThyHardwareMilliseconds 500
            if ($Test -ceq 'Controls') {
                $fields=Send-ThyHardwareCommand $SerialPort controls 'CONTROLS '
                Assert-ThyControls $fields
            } elseif ($Test -ceq 'Led') {
                $fields=Send-ThyHardwareCommand $SerialPort 'led status' 'LED '
            } else {
                $fields=Send-ThyHardwareCommand $SerialPort 'pads status' 'PADS '
                if((Get-ThyTelemetryInteger $fields lighting_decoupled) -ne 1) {
                    throw 'Pad observation changed the lighting-decoupled profile'
                }
                for($bank=0;$bank -lt 6;$bank++) {
                    $null=Send-ThyHardwareCommand $SerialPort ('pads bank '+$bank) 'PADS '
                }
            }
            $result.final=$fields
            $result.observations++
            $at=Get-ThyHardwareMilliseconds
            if ($at -ge $statusAt) {
                $status=Send-ThyHardwareCommand $SerialPort status 'STATUS THYDIAG/1 '
                Assert-ThyStatus $status $Build $Serial $previous
                $previous=$status
                $statusAt=$at+5000
            }
        } while ((Get-ThyHardwareMilliseconds)-$start -lt $window)
        $result.completed=$true
    } catch {
        $result.failure=$_.Exception.Message
        Write-ThyTelemetryEvent 'failure' @{message=$result.failure}
    } finally {
        # These are the only cleanup mutations. No reopening, retries of the
        # test, reset or output restart occurs after any transport/device error.
        if ($script:hardwareBound -and $Test -cin @('Led','Pads')) {
            try {
                $stop=if($Test -ceq 'Led') {'led off'} else {'pads stop'}
                $prefix=if($Test -ceq 'Led') {'LED '} else {'PADS '}
                $null=Send-ThyHardwareCommand $SerialPort $stop $prefix
                $result.device_actions+=$stop
                $cleanupStart=Get-ThyHardwareMilliseconds
                do {
                    Wait-ThyHardwareMilliseconds 100
                    $command=if($Test -ceq 'Led') {'led status'} else {'pads status'}
                    $final=Send-ThyHardwareCommand $SerialPort $command $prefix
                    $result.final=$final
                    if($Test -ceq 'Led') {
                        $result.cleanup_completed=$final['state'] -ceq 'idle' -and $final['mode'] -ceq 'off' -and
                            (Get-ThyTelemetryInteger $final off_frame_sent) -eq 1
                    } else {
                        $result.cleanup_completed=(Get-ThyTelemetryInteger $final active) -eq 0 -and
                            (Get-ThyTelemetryInteger $final busy) -eq 0 -and
                            (Get-ThyTelemetryInteger $final state) -in @(0,5)
                    }
                } while(-not $result.cleanup_completed -and
                    (Get-ThyHardwareMilliseconds)-$cleanupStart -lt 2000)
                if($Test -ceq 'Pads') {
                    # Save all six banks after stop, before any later test can
                    # clear their extrema; valid masks remain observations.
                    for($bank=0;$bank -lt 6;$bank++) {
                        $null=Send-ThyHardwareCommand $SerialPort ('pads bank '+$bank) 'PADS '
                    }
                    $result.final_adc=Send-ThyHardwareCommand $SerialPort adc 'ADC '
                    if($result.final_adc['ownership'] -cne 'raw-adc' -or $result.final_adc['snapshots'] -cne 'active' -or
                        (Get-ThyTelemetryInteger $result.final_adc ok) -ne 0x3f) {
                        $result.cleanup_completed=$false
                    }
                }
                if(-not $result.cleanup_completed) { throw 'Device did not confirm bounded hardware shutdown; inspect retained observations' }
            } catch {
                $result.completed=$false;$result.cleanup_completed=$false
                $result.cleanup_failure=$_.Exception.Message
                Write-ThyTelemetryEvent 'cleanup-failure' @{message=$_.Exception.Message}
                if($null -eq $result.failure) {$result.failure=$_.Exception.Message}
            }
        } elseif($script:hardwareBound) {$result.cleanup_completed=$true}
        if($script:hardwareBound) {
            try {
                $result.center_after=Send-ThyHardwareCommand $SerialPort 'center status' 'CENTER '
                Assert-ThyCenterStatus $result.center_after
            } catch {
                $result.completed=$false;$result.center_after_failure=$_.Exception.Message
                Write-ThyTelemetryEvent 'center-observation-failure' @{message=$_.Exception.Message}
                if($null -eq $result.failure) {$result.failure=$_.Exception.Message}
            }
        }
    }
    # completed means collection and quiescence, never blanket hardware or
    # optical acceptance. Retained acquisition faults remain observations.
    if($result.completed -and $result.cleanup_completed) {
        if($Test -ceq 'Led') {
            $result.firmware_checks=[ordered]@{
                no_new_errors=(Get-ThyTelemetryInteger $result.final errors) -eq (Get-ThyTelemetryInteger $result.initial errors)
                frame_progress=(Get-ThyTelemetryInteger $result.final frames) -gt (Get-ThyTelemetryInteger $result.initial frames)
                off_frame_confirmed=(Get-ThyTelemetryInteger $result.final off_frame_sent) -eq 1
                latest_result_ok=(Get-ThyTelemetryInteger $result.final code) -eq 0
            }
        } elseif($Test -ceq 'Pads') {
            $result.acquisition_fault_retained=(Get-ThyTelemetryInteger $result.final state) -eq 5
            $result.firmware_checks=[ordered]@{
                lighting_decoupled=(Get-ThyTelemetryInteger $result.final lighting_decoupled) -eq 1
                no_new_errors=(Get-ThyTelemetryInteger $result.final errors) -eq (Get-ThyTelemetryInteger $result.initial errors)
                frame_progress=(Get-ThyTelemetryInteger $result.final frames) -gt 0
                scan_progress=(Get-ThyTelemetryInteger $result.final scans) -gt (Get-ThyTelemetryInteger $result.initial scans)
                latest_result_ok=(Get-ThyTelemetryInteger $result.final code) -eq 0
                fault_retained=$result.acquisition_fault_retained
            }
        } else {
            $result.firmware_checks=[ordered]@{
                coverage_complete=(Get-ThyTelemetryInteger $result.final covered) -eq 43
                banks_valid=(Get-ThyTelemetryInteger $result.final valid) -eq 15
            }
        }
        $result.firmware_checks_passed=$true
        foreach($check in $result.firmware_checks.Keys) {
            if($check -eq 'fault_retained') {if($result.firmware_checks[$check]) {$result.firmware_checks_passed=$false}}
            elseif(-not $result.firmware_checks[$check]) {$result.firmware_checks_passed=$false}
        }
    }
    return $result
}

function Write-ThyTelemetryEvent {
    param([string]$Kind,[object]$Data)
    $event=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=(Get-ThyHardwareMilliseconds);kind=$Kind;data=$Data}
    $script:eventWriter.WriteLine(($event|ConvertTo-Json -Depth 16 -Compress));$script:eventWriter.Flush()
    if($Kind -cin @('tx-intent','tx-complete','rx','partial-rx')) {
        $script:wireWriter.WriteLine(('{0} {1} {2} {3}' -f $event.elapsed_ms,$Kind,$Data.hex,$Data.line))
        $script:wireWriter.Flush()
    }
}

$ErrorActionPreference='Stop'
if($ExpectedBuild -cne '0d99dc2374abf123') {throw 'ExpectedBuild must be 0d99dc2374abf123 for this hardware-test helper'}
# Import only reviewed helper definitions. Dot-sourcing would execute the
# collector, so parse its AST and select exact function definitions instead.
$collectorPath=Join-Path $PSScriptRoot 'capture_diagnostic_telemetry.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($collectorPath,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
foreach($name in @('ConvertFrom-ThyTelemetryLine','Get-ThyTelemetryInteger','Assert-ThyGreeting',
    'Assert-ThyTelemetryIdentity','Assert-ThyStartup','Assert-ThyStatus','Assert-ThyControls',
    'Get-ThyLiveCdcBinding','Read-ThyConsoleLine')) {
    $found=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
    if($found.Count -ne 1) {throw ('Expected exactly one shared helper '+$name)}
    Invoke-Expression $found[0].Extent.Text
}
$workspacePath=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$allowedPath=[IO.Path]::GetFullPath((Join-Path $workspacePath 'analysis/local'))+[IO.Path]::DirectorySeparatorChar
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $outputPath.StartsWith($allowedPath,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $outputPath)) {throw 'Output must be a fresh directory inside analysis/local'}
$ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($outputPath))
while($null -ne $ancestor -and ($ancestor.FullName+[IO.Path]::DirectorySeparatorChar).StartsWith($allowedPath,[StringComparison]::OrdinalIgnoreCase)) {
    if($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Output ancestors must not be junctions or symbolic links'}
    $ancestor=$ancestor.Parent
}
$binding=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop|Out-Null
$streams=@();$serialPort=$null;$result=@{completed=$false;failure=$null}
$script:observationClock=[Diagnostics.Stopwatch]::StartNew()
try {
    foreach($file in @('events.jsonl','wire.txt')) {
        $stream=[IO.File]::Open((Join-Path $outputPath $file),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        $streams+=,$stream
        $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
        if($file -eq 'events.jsonl') {$script:eventWriter=$writer} else {$script:wireWriter=$writer}
    }
    Write-ThyTelemetryEvent 'binding' $binding
    $serialPort=[IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serialPort.Encoding=[Text.Encoding]::ASCII
    $serialPort.Handshake=[IO.Ports.Handshake]::None
    $serialPort.DtrEnable=$true;$serialPort.RtsEnable=$false
    $serialPort.ReadTimeout=200;$serialPort.WriteTimeout=2000
    $null=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    $serialPort.Open()
    Write-ThyTelemetryEvent 'opened' @{dtr=$true;baud=115200}
    $result=Invoke-ThyHardwareSession $serialPort $Test $ExpectedBuild $ExpectedSerial $DurationSeconds ([bool]$ClearControls) $LedMode $Component $Pixel
    $bindingAfter=Get-ThyLiveCdcBinding $Port $ExpectedSerial $ExpectedHubParent
    Write-ThyTelemetryEvent 'binding-after' $bindingAfter
} catch {
    $result.completed=$false;$result.failure=$_.Exception.Message
    if($null -ne $script:eventWriter) {Write-ThyTelemetryEvent 'failure' @{message=$result.failure}}
} finally {
    if($null -ne $serialPort) {
        try {if($serialPort.IsOpen) {$serialPort.Close()};$serialPort.Dispose()} catch {
            $result.completed=$false;$result.close_failure=$_.Exception.Message
        }
    }
    if($null -ne $script:eventWriter) {$script:eventWriter.Dispose()}
    if($null -ne $script:wireWriter) {$script:wireWriter.Dispose()}
    foreach($stream in $streams) {$stream.Dispose()}
    $summary=[ordered]@{format='thy666.diagnostic-hardware-test';version=1;port=$Port;expected_build=$ExpectedBuild;
        expected_serial=$ExpectedSerial;expected_hub_parent=$ExpectedHubParent;identity_before=$binding;
        identity_after=$bindingAfter;test=$Test;result=$result;elapsed_ms=(Get-ThyHardwareMilliseconds);
        helper_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
        shared_collector_sha256=(Get-FileHash -LiteralPath $collectorPath -Algorithm SHA256).Hash.ToLowerInvariant();
        scope='Explicit bounded volatile LED/pad/control tests. No flash, erase, reset, memory read or MIDI transmit. Optical and physical mapping require owner observations.'}
    foreach($file in @('events.jsonl','wire.txt')) {
        $path=Join-Path $outputPath $file
        if(Test-Path -LiteralPath $path) {$summary[$file+'_sha256']=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $summaryStream=[IO.File]::Open((Join-Path $outputPath 'observation.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $summaryWriter=[IO.StreamWriter]::new($summaryStream,[Text.UTF8Encoding]::new($false))
    try {$summaryWriter.WriteLine(($summary|ConvertTo-Json -Depth 18))} finally {$summaryWriter.Dispose();$summaryStream.Dispose()}
}
[pscustomobject]@{path=$outputPath;completed=$result.completed;failure=$result.failure}|ConvertTo-Json
if(-not $result.completed) {throw ('Hardware observation incomplete; evidence preserved at '+$outputPath)}
