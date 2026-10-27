# Offline only; no top-level helper code, COM or PnP APIs are executed.
$ErrorActionPreference='Stop'
foreach($source in @(@{path=(Join-Path $PSScriptRoot '../tools/capture_instrument_telemetry.ps1');names=@('Assert-ThyInstrumentGreeting','ConvertFrom-ThyInstrumentReply','Assert-ThyInstrumentStatus','Send-ThyInstrumentObservation')},@{path=(Join-Path $PSScriptRoot '../tools/instrument_panic.ps1');names=@('Invoke-ThyInstrumentPanicSession')})) {
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source.path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count) {throw ($parseErrors.Message -join '; ')}
    foreach($name in $source.names) {
        $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($found.Count -ne 1) {throw 'Expected one offline panic helper'}
        Invoke-Expression $found[0].Extent.Text
    }
}
$script:checks=0
function Check([bool]$Value,[string]$Message) {$script:checks++;if(-not $Value) {throw $Message}}
function Reject([scriptblock]$Action,[string]$Message) {$bad=$false;try {& $Action|Out-Null} catch {$bad=$true};Check $bad $Message}
function Write-ThyTelemetryEvent([string]$Kind,[object]$Data) {}
function Read-ThyConsoleLine([object]$SerialPort) {if(-not $SerialPort.Replies.Count) {throw 'Synthetic read timeout'};return $SerialPort.Replies.Dequeue()}
$build='332f075994d2e8ed'
function New-PanicPort([string]$Greeting='HELLO THYINSTRUMENT/1 alpha; type help',[string]$Ack='OK panic',[string]$Build='332f075994d2e8ed',[int]$AfterVoices=0) {
    $p=[pscustomobject]@{Writes=[Collections.Generic.List[string]]::new();Replies=[Collections.Generic.Queue[string]]::new();FailPanicWrite=$false}
    $p|Add-Member ScriptMethod Write {param([string]$Text) $this.Writes.Add($Text);if($Text -ceq "panic`n" -and $this.FailPanicWrite) {throw 'Synthetic uncertain panic write'}}
    $p.Replies.Enqueue($Greeting)
    $p.Replies.Enqueue('STATUS THYINSTRUMENT/1 build='+$Build+' ready=1 uptime_ms=1000 errors=0 presses=42 releases=40 voices=6 layout=MAJOR C')
    $p.Replies.Enqueue($Ack)
    $p.Replies.Enqueue('STATUS THYINSTRUMENT/1 build='+$Build+' ready=1 uptime_ms=1001 errors=0 presses=42 releases=40 voices='+$AfterVoices+' layout=MAJOR C')
    return $p
}
$port=New-PanicPort
$result=Invoke-ThyInstrumentPanicSession $port $build
Check ($result.before.voices -eq 6 -and $result.after.voices -eq 0 -and $result.voices_zero_confirmed) 'Panic verification failed'
Check (($port.Writes -join ',') -ceq "status`n,panic`n,status`n") 'Unexpected command/order/framing'
Check ($script:panicAttempted -and $script:panicAcknowledged) 'Panic receipt flags missing'
Check ($result.after.presses -eq 42 -and $result.after.releases -eq 40) 'Panic observation changed detector counter evidence'
$port=New-PanicPort -Greeting 'HELLO OTHER'
Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Foreign greeting accepted'
Check ($port.Writes.Count -eq 0 -and -not $script:panicAttempted) 'Command sent before fresh greeting'
$port=New-PanicPort -Build 'ffffffffffffffff'
Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Foreign build accepted'
Check ($port.Writes.Count -eq 1 -and -not $script:panicAttempted) 'Panic sent before exact build'
foreach($ack in @('OK panic extra','ok panic','OK','ERR unknown-command','')) {
    $port=New-PanicPort -Ack $ack
    Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Malformed panic acknowledgement accepted'
    Check ($port.Writes.Count -eq 2 -and $script:panicAttempted -and -not $script:panicAcknowledged) 'Retried panic or sent post-ack status after failed acknowledgement'
}
$port=New-PanicPort -AfterVoices 3
Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Nonzero post-panic voices accepted'
Check (($port.Writes|Where-Object {$_ -ceq "panic`n"}).Count -eq 1) 'Retried after nonzero voices'
$port=New-PanicPort;$port.FailPanicWrite=$true
Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Uncertain write exception swallowed'
Check ($port.Writes.Count -eq 2 -and $script:panicAttempted -and -not $script:panicAcknowledged) 'Uncertain panic write was retried'
$port=New-PanicPort;$port.Replies.Clear();$port.Replies.Enqueue('HELLO THYINSTRUMENT/1 alpha; type help');$port.Replies.Enqueue('STATUS THYINSTRUMENT/1 build='+$build+' ready=1 uptime_ms=1000 errors=0 presses=42 releases=40 voices=6 layout=MAJOR C')
Reject {Invoke-ThyInstrumentPanicSession $port $build} 'Missing acknowledgement accepted'
Check ($port.Writes.Count -eq 2) 'Timed-out panic was retried'
Write-Output ('PASS instrument one-shot panic offline assertions='+$script:checks)
