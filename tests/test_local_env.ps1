# Offline only: local-file parsing, precedence, validation and script syntax.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/local_env.ps1')
$script:assertions = 0
function Check([bool]$Condition, [string]$Message) { $script:assertions++; if (-not $Condition) { throw $Message } }
function Reject([scriptblock]$Action, [string]$Message) { $failed=$false; try { & $Action | Out-Null } catch { $failed=$true }; Check $failed $Message }
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/test-local-env'))
[void][IO.Directory]::CreateDirectory($taskRoot)
$taskFolder = Join-Path $taskRoot ([Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($taskFolder)
$fixture = Join-Path $taskFolder '.env'
$testKey = 'THY666_TEST_LOCAL_CONFIGURATION'
$originalProcessValue = [Environment]::GetEnvironmentVariable($testKey, 'Process')
try {
    [IO.File]::WriteAllText($fixture, @'
# Literal values, including Windows backslashes and shell-looking text.
export THY666_TEST_LOCAL_CONFIGURATION = file value # comment
THY666_TEST_PATH='C:\Example\MIDI tools\USBPcapCMD.exe'
THY666_TEST_LITERAL="$(throw 'must not execute') # literal"
THY666_TEST_EMPTY=
'@, [Text.UTF8Encoding]::new($false))
    $configuration = Read-ThyLocalEnvironment -Path $fixture
    Check ($configuration.Count -eq 4) 'Wrong parsed key count'
    Check ($configuration[$testKey] -ceq 'file value') 'Whitespace or inline comment parsing failed'
    Check ($configuration['THY666_TEST_PATH'] -ceq 'C:\Example\MIDI tools\USBPcapCMD.exe') 'Quoted Windows path changed'
    Check ($configuration['THY666_TEST_LITERAL'] -ceq '$(throw ''must not execute'') # literal') 'Quoted text was interpolated or altered'
    Check ($configuration['THY666_TEST_EMPTY'] -ceq '') 'Blank file value changed'
    [Environment]::SetEnvironmentVariable($testKey, $null, 'Process')
    Check ((Resolve-ThyLocalValue -Name $testKey -Configuration $configuration -Default 'fallback') -ceq 'file value') 'File fallback failed'
    [Environment]::SetEnvironmentVariable($testKey, 'process value', 'Process')
    Check ((Resolve-ThyLocalValue -Name $testKey -Configuration $configuration -Default 'fallback') -ceq 'process value') 'Process environment did not override file'
    Check ((Resolve-ThyLocalValue -Name $testKey -Value 'command line' -Explicit -Configuration $configuration) -ceq 'command line') 'Explicit argument lost precedence'
    Reject { Resolve-ThyLocalValue -Name $testKey -Value '' -Explicit -Configuration $configuration -Required } 'Explicit blank required argument fell back to a different device'
    [Environment]::SetEnvironmentVariable($testKey, ' ', 'Process')
    Check ((Resolve-ThyLocalValue -Name $testKey -Configuration $configuration -Default 'fallback') -ceq 'file value') 'Blank process environment masked file value'
    Check ((Resolve-ThyLocalValue -Name 'THY666_TEST_EMPTY' -Configuration $configuration -Default 'fallback') -ceq 'fallback') 'Blank file value masked default'
    Reject { Resolve-ThyLocalValue -Name 'THY666_TEST_MISSING' -Configuration $configuration -Required } 'Missing required configuration accepted'
    Reject { Resolve-ThyLocalValue -Name $testKey -Value 'bad port' -Explicit -Configuration $configuration -Pattern '^COM[1-9][0-9]*$' } 'Invalid binding accepted'
    $relative = Resolve-ThyLocalValue -Name $testKey -Value 'build/example-input' -Explicit -Configuration $configuration -AsPath
    Check ($relative -eq [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/example-input'))) 'Relative configuration path did not resolve at workspace root'
    foreach ($bad in @('INVALID LINE', ('A=one' + "`n" + 'A=two'), 'A="unterminated', 'A="ok" extra', '1BAD=value')) {
        [IO.File]::WriteAllText($fixture, $bad, [Text.UTF8Encoding]::new($false))
        Reject { Read-ThyLocalEnvironment -Path $fixture } 'Malformed/duplicate environment content accepted'
    }
    $configuration = Read-ThyLocalEnvironment -Path (Join-Path $taskFolder 'missing.env')
    Check ($configuration.Count -eq 0) 'Missing optional file did not return empty configuration'
    # Run only each script's real parameter/configuration prefix in an isolated
    # fixture directory. No functions or hardware/session body are copied.
    $probeTools = Join-Path $taskFolder 'tools'
    [void][IO.Directory]::CreateDirectory($probeTools)
    [IO.File]::Copy((Join-Path $PSScriptRoot '../tools/local_env.ps1'), (Join-Path $probeTools 'local_env.ps1'))
    [IO.File]::WriteAllText((Join-Path $taskFolder '.env'), @'
THY666_SERIAL_PORT=COM91
THY666_DEVICE_SERIAL=0123456789ABCDEF01234567
THY666_USB_HUB_PARENT='USB\ROOT_HUB30\SYNTHETIC'
THY666_EXPECTED_BUILD=0123456789abcdef
THY666_MIDI_INPUT_NAME='Synthetic MIDI'
THY666_MIDI_INTERFACE_PATH='synthetic-interface'
THY666_USBPCAP_DEVICE='\\.\USBPcap9'
THY666_USBPCAP_PATH='test/USBPcapCMD.exe'
THY666_USBPCAP_ADDRESSES=
'@, [Text.UTF8Encoding]::new($false))
    $deviceKeys=@('THY666_SERIAL_PORT','THY666_DEVICE_SERIAL','THY666_USB_HUB_PARENT','THY666_EXPECTED_BUILD','THY666_MIDI_INPUT_NAME','THY666_MIDI_INTERFACE_PATH','THY666_USBPCAP_DEVICE','THY666_USBPCAP_PATH','THY666_USBPCAP_ADDRESSES')
    $savedDeviceEnvironment=@{}
    foreach ($key in $deviceKeys) {
        $savedDeviceEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
        [Environment]::SetEnvironmentVariable($key,$null,'Process')
    }
    try {
    foreach ($scriptPath in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../tools') -Filter '*.ps1') {
        $tokens=$null; $errors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile($scriptPath.FullName, [ref]$tokens, [ref]$errors)
        Check ($errors.Count -eq 0) ('PowerShell syntax error: ' + $scriptPath.Name + ': ' + ($errors.Message -join '; '))
        if ($scriptPath.Name -in @('capture_diagnostic_telemetry.ps1','capture_instrument_telemetry.ps1','capture_instrument_midi.ps1','capture_sequencer_song.ps1','instrument_panic.ps1','observe_ui_feedback.ps1','read_instrument_polish_snapshot.ps1','read_touch_configuration.ps1','test_diagnostic_hardware.ps1')) {
            foreach ($name in @('Port','ExpectedSerial','ExpectedHubParent','ExpectedBuild')) {
                $parameter=@($ast.ParamBlock.Parameters | Where-Object {$_.Name.VariablePath.UserPath -eq $name})
                Check ($parameter.Count -eq 1) ('Missing local configuration parameter: ' + $name)
                Check ($parameter[0].Extent.Text -notmatch 'Mandatory\s*=\s*\$true') ('Configuration parameter still prompts before .env: ' + $name)
            }
            $source=[IO.File]::ReadAllText($scriptPath.FullName)
            foreach ($name in @('THY666_SERIAL_PORT','THY666_DEVICE_SERIAL','THY666_USB_HUB_PARENT','THY666_EXPECTED_BUILD')) {
                Check ($source.Contains("-Name '"+$name+"'")) ('Missing local identity binding: '+$name)
            }
            $prefix=[regex]::Match($source,"(?s)\. \(Join-Path \`$PSScriptRoot 'local_env.ps1'\).*?(?=\r?\n\r?\n)").Value
            Check (-not [string]::IsNullOrEmpty($prefix)) ('Missing configuration prefix: '+$scriptPath.Name)
            $probe=Join-Path $probeTools ('probe-'+$scriptPath.Name)
            [IO.File]::WriteAllText($probe, ($ast.ParamBlock.Extent.Text+"`n"+$prefix+"`n[pscustomobject]@{Port=`$Port;Serial=`$ExpectedSerial;Hub=`$ExpectedHubParent;Build=`$ExpectedBuild}"), [Text.UTF8Encoding]::new($false))
            $probeArgs=@{OutputDirectory='unused'}
            if($scriptPath.Name -eq 'test_diagnostic_hardware.ps1') {$probeArgs.Test='Controls'}
            $resolved=& $probe @probeArgs
            Check ($resolved.Port -ceq 'COM91' -and $resolved.Serial -ceq '0123456789ABCDEF01234567' -and $resolved.Hub -ceq 'USB\ROOT_HUB30\SYNTHETIC' -and $resolved.Build -ceq '0123456789abcdef') ('Configuration prefix did not bind fixture identity: '+$scriptPath.Name)
            $probeArgs.Port='COM92'
            $resolved=& $probe @probeArgs
            Check ($resolved.Port -ceq 'COM92') ('CLI did not override configuration in '+$scriptPath.Name)
        }
    }
    $capturePath=Join-Path $PSScriptRoot '../tools/run_usbpcap_session.ps1'
    $tokens=$null;$errors=$null
    $captureAst=[Management.Automation.Language.Parser]::ParseFile($capturePath,[ref]$tokens,[ref]$errors)
    $captureSource=[IO.File]::ReadAllText($capturePath)
    $capturePrefix=[regex]::Match($captureSource,"(?s)\. \(Join-Path \`$PSScriptRoot 'local_env.ps1'\).*?(?=\r?\n\r?\n)").Value
    $probe=Join-Path $probeTools 'probe-capture.ps1'
    [IO.File]::WriteAllText($probe, ($captureAst.ParamBlock.Extent.Text+"`n"+$capturePrefix+"`n[pscustomobject]@{Device=`$CaptureDevice;Addresses=`$Addresses;Tool=`$ToolPath}"), [Text.UTF8Encoding]::new($false))
    $capture=& $probe -SessionName fixture -CaptureAll
    Check ($capture.Device -ceq '\\.\USBPcap9') 'Capture device did not come from isolated configuration'
    Check ($capture.Addresses -ceq '') 'CaptureAll inherited an unsafe device address'
    Check ($capture.Tool -eq [IO.Path]::GetFullPath((Join-Path $taskFolder 'test/USBPcapCMD.exe'))) 'Capture path did not resolve relative to configuration root'
    Reject { & $probe -SessionName fixture } 'Selective capture accepted a missing address list'
    $capture=& $probe -SessionName fixture -Addresses 42
    Check ($capture.Addresses -ceq '42') 'Explicit selective capture address was lost'
    } finally {
        foreach($key in $deviceKeys) {[Environment]::SetEnvironmentVariable($key,$savedDeviceEnvironment[$key],'Process')}
    }
    'PASS: ' + $script:assertions + ' local environment/syntax checks; no device API called.'
} finally {
    [Environment]::SetEnvironmentVariable($testKey, $originalProcessValue, 'Process')
    # The target was created above, is absolute, and remains under our test root.
    if ($taskFolder.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        [IO.Directory]::Delete($taskFolder, $true)
    }
}
