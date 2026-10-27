# Offline only: parses the supervisor and compiles its native declarations without calling them.
param([string]$CaptureScript = (Join-Path $PSScriptRoot '..\tools\run_usbpcap_session.ps1'))
$ErrorActionPreference = 'Stop'
$captureScript = [IO.Path]::GetFullPath($CaptureScript)
$captureTokens = $null; $captureErrors = $null
$captureAst = [Management.Automation.Language.Parser]::ParseFile($captureScript, [ref]$captureTokens, [ref]$captureErrors)
if ($captureErrors.Count) { throw ($captureErrors.Message -join '; ') }
$captureStarts = @($captureAst.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Start-Process' }, $true))
if ($captureStarts.Count -ne 1) { throw 'Expected exactly one capture child launch.' }
$captureStartParameters = @($captureStarts[0].CommandElements | Where-Object { $_ -is [Management.Automation.Language.CommandParameterAst] } | ForEach-Object { $_.ParameterName })
if ($captureStartParameters -contains 'RedirectStandardOutput' -or $captureStartParameters -contains 'RedirectStandardInput' -or $captureStartParameters -notcontains 'NoNewWindow') { throw 'Console attachment regression: child must inherit console input/output.' }
$captureSource = [IO.File]::ReadAllText($captureScript)
$captureNativeSource = [regex]::Match($captureSource, "(?s)Add-Type -TypeDefinition @'\r?\n(.*?)\r?\n'@").Groups[1].Value
if (-not $captureNativeSource) { throw 'Native declarations were not found.' }
Add-Type -TypeDefinition $captureNativeSource
$captureRecords = [ThyCaptureConsole]::QRecords()
if ($captureRecords.Length -ne 2) { throw 'Expected q key-down and key-up records.' }
foreach ($captureRecord in $captureRecords) {
    if ([Runtime.InteropServices.Marshal]::SizeOf($captureRecord) -ne 20) { throw 'Incorrect Windows INPUT_RECORD size.' }
    $captureBuffer = [Runtime.InteropServices.Marshal]::AllocHGlobal(20)
    try {
        # Zero padding to make the entire ABI byte buffer deterministic for this check.
        for ($captureOffset=0; $captureOffset -lt 20; $captureOffset++) { [Runtime.InteropServices.Marshal]::WriteByte($captureBuffer, $captureOffset, 0) }
        [Runtime.InteropServices.Marshal]::StructureToPtr($captureRecord, $captureBuffer, $false)
        if ([Runtime.InteropServices.Marshal]::ReadInt16($captureBuffer, 0) -ne 1) { throw 'Not a KEY_EVENT.' }
        if ([Runtime.InteropServices.Marshal]::ReadInt32($captureBuffer, 4) -ne $captureRecord.KeyDown) { throw 'Incorrect BOOL bKeyDown position.' }
        if ([Runtime.InteropServices.Marshal]::ReadInt16($captureBuffer, 8) -ne 1) { throw 'Expected one repeat.' }
        if ([Runtime.InteropServices.Marshal]::ReadInt16($captureBuffer, 10) -ne 0x51) { throw 'Incorrect VK_Q.' }
        if ([Runtime.InteropServices.Marshal]::ReadByte($captureBuffer, 14) -ne 0x71) { throw 'USBPcap ReadConsoleInputA would not see lowercase q.' }
        if ([Runtime.InteropServices.Marshal]::ReadInt32($captureBuffer, 16) -ne 0) { throw 'Unexpected control modifiers.' }
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($captureBuffer) }
}
if ($captureRecords[0].KeyDown -ne 1 -or $captureRecords[1].KeyDown -ne 0) { throw 'Incorrect key-down/up order.' }
foreach ($captureField in @(@('EventType',0),@('KeyDown',4),@('Repeat',8),@('VirtualKey',10),@('Scan',12),@('Unicode',14),@('Control',16))) {
    if ([Runtime.InteropServices.Marshal]::OffsetOf([ThyCaptureConsole+InputRecord], $captureField[0]).ToInt32() -ne $captureField[1]) { throw ('Incorrect ABI offset: '+$captureField[0]) }
}
'PASS: supervisor syntax, compiled native declarations and q console-record ABI; no capture/native API called.'
