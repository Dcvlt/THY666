# Offline only. Extract and execute the pure cached-data classifier, never the inventory script.
$ErrorActionPreference = 'Stop'
$inventoryPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\capture_usb_inventory.ps1'))
$inventoryTokens = $null; $inventoryErrors = $null
$inventoryAst = [Management.Automation.Language.Parser]::ParseFile($inventoryPath, [ref]$inventoryTokens, [ref]$inventoryErrors)
if ($inventoryErrors.Count) { throw ($inventoryErrors.Message -join '; ') }
$classifiers = @($inventoryAst.FindAll({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-ThyUsbObservations'
}, $true))
if ($classifiers.Count -ne 1) { throw 'Expected one pure cached-data classifier.' }
Invoke-Expression $classifiers[0].Extent.Text

function Test-Device([string]$Identity, [string]$Class, [string[]]$CompatibleIds = @()) {
    [pscustomobject]@{ instance_id = $Identity; class = $Class
        properties = @{ 'DEVPKEY_Device_CompatibleIds' = $CompatibleIds } }
}
function Assert-Roles($Actual, [bool]$Midi, [bool]$Hid, [bool]$Dfu, [bool]$Cdc) {
    $expected = @{ midi_interface_present = $Midi; updater_hid_interface_present = $Hid
        st_rom_dfu_vid_pid_present = $Dfu; diagnostic_cdc_port_present = $Cdc }
    foreach ($role in $expected.Keys) {
        if ($Actual[$role] -ne $expected[$role]) { throw ('Incorrect role: ' + $role) }
    }
}

Assert-Roles (Get-ThyUsbObservations @()) $false $false $false $false
foreach ($identity in @('USB\VID_0333&PID_0333&MI_00\a', 'USB\VID_115C&PID_572B\a')) {
    Assert-Roles (Get-ThyUsbObservations @((Test-Device $identity 'MEDIA'))) $true $false $false $false
}
Assert-Roles (Get-ThyUsbObservations @(
    (Test-Device 'USB\VID_CAFE&PID_4009&MI_00\a' 'Ports'),
    (Test-Device 'USB\VID_CAFE&PID_4009&MI_03\a' 'MEDIA')
)) $true $false $false $true
Assert-Roles (Get-ThyUsbObservations @((Test-Device 'USB\VID_CAFE&PID_4009&MI_03\a' 'MidiEndpoint'))) $true $false $false $false
Assert-Roles (Get-ThyUsbObservations @((Test-Device 'HID\VID_0333&PID_0333\a' 'HIDClass'))) $false $true $false $false
Assert-Roles (Get-ThyUsbObservations @((Test-Device 'USB\VID_0333&PID_0333\a' 'USB' @('USB\Class_03')))) $false $true $false $false
Assert-Roles (Get-ThyUsbObservations @((Test-Device 'USB\VID_0483&PID_DF11\a' 'USB'))) $false $false $true $false
# An unrelated endpoint or a partial PID match must not claim success for our board.
Assert-Roles (Get-ThyUsbObservations @(
    (Test-Device 'USB\VID_1234&PID_5678\a' 'MidiEndpoint'),
    (Test-Device 'USB\VID_CAFE&PID_40090\a' 'Ports'),
    (Test-Device 'USB\VID_0483&PID_DF110\a' 'USB'),
    (Test-Device 'USB\VID_0333&PID_03330\a' 'HIDClass')
)) $false $false $false $false
'PASS: inventory syntax and cached identity/class attribution; no PnP or device API called.'
