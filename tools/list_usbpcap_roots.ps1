# Enumerate USBPcap roots/configuration only. No --capture, --fifo or updater.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,79}$')][string]$SessionName, [string]$ToolPath='')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'local_env.ps1')
$localConfiguration = Read-ThyLocalEnvironment -Path (Join-Path $PSScriptRoot '../.env')
$captureToolPath = Resolve-ThyLocalValue -Name 'THY666_USBPCAP_PATH' -Value $ToolPath -Explicit:($PSBoundParameters.ContainsKey('ToolPath')) -Configuration $localConfiguration -Default (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'USBPcap/USBPcapCMD.exe') -AsPath -Required
if ((Get-FileHash -LiteralPath $captureToolPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne '8b13173e9453fda9914d707c732880de44081e20e1113b3a2c827abd7a28b15d') { throw 'USBPcap tool identity changed.' }
$workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$listingPath = Join-Path $workspacePath ('analysis\local\usb-update-capture\' + $SessionName)
if (Test-Path -LiteralPath $listingPath) { throw 'Listing output exists; choose a new session name.' }
[void](New-Item -ItemType Directory -Path $listingPath)
function Invoke-Listing([string]$QueryArguments, [string]$Label) {
    $stdoutPath = Join-Path $listingPath ($Label + '.stdout.txt')
    $stderrPath = Join-Path $listingPath ($Label + '.stderr.txt')
    $queryProcess = Start-Process -FilePath $captureToolPath -ArgumentList $QueryArguments -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
    if (-not $queryProcess.WaitForExit(10000)) {
        if ($queryProcess.MainModule.FileName -ine $captureToolPath) { throw 'Listing child identity changed.' }
        $queryProcess.Kill()
        [void]$queryProcess.WaitForExit(5000)
        throw 'Enumeration timed out; own listing child stopped, no capture started.'
    }
    $queryProcess.WaitForExit()
    # Start-Process may lose the exit code of this short-lived GUI-subsystem
    # executable. Do not turn null into nonzero or claim null means success.
    if ($null -ne $queryProcess.ExitCode -and $queryProcess.ExitCode -ne 0) { throw ('Enumeration failed: ' + $stderrPath) }
    $queryText = [IO.File]::ReadAllText($stdoutPath)
    if ([string]::IsNullOrWhiteSpace($queryText)) { throw ('Enumeration produced no data: ' + $stderrPath) }
    return $queryText
}
$interfaceListing = Invoke-Listing '--extcap-interfaces' 'interfaces'
$captureRoots = @([regex]::Matches($interfaceListing, 'interface\s+\{value=(\\\\\.\\USBPcap[0-9]+)\}') | ForEach-Object { $_.Groups[1].Value })
if (-not $captureRoots.Count) { throw 'No USBPcap interfaces returned; inspect retained listing.' }
$rootRecords = @()
foreach ($captureRoot in $captureRoots) {
    $label = $captureRoot.Substring($captureRoot.LastIndexOf('\') + 1).ToLowerInvariant()
    $configuration = Invoke-Listing ('--extcap-interface ' + $captureRoot + ' --extcap-config') $label
    $targetLines = @($configuration -split '\r?\n' | Where-Object { $_ -match 'TheoryBoard|THY333|Irijule' })
    $rootRecords += [ordered]@{ CaptureDevice=$captureRoot; TargetLines=$targetLines; ConfigLog=(Join-Path $listingPath ($label + '.stdout.txt')) }
}
$report = [ordered]@{ Format='thy333.usbpcap-root-listing'; Version=1; CapturedUtc=[DateTime]::UtcNow.ToString('o'); CaptureStarted=$false; UpdaterStarted=$false; Roots=$rootRecords }
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $listingPath 'listing.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 5
