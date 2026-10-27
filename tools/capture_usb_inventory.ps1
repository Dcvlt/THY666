# Windows PnP inventory only. Does not open the updater, HID, MIDI or DFU devices.
[CmdletBinding()]
param(
    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,39}$')]
    [string]$Label = 'snapshot'
)

function Get-ThyUsbObservations {
    # Pure cached-data classification, also exercised by the offline test.
    param([object[]]$Devices)
    $midiDevices = @($Devices | Where-Object {
        $_.instance_id -match 'VID_(0333&PID_0333|115C&PID_572B|CAFE&PID_4009)(?:&|\\|$)' -and
        $_.class -in @('MEDIA', 'MidiEndpoint')
    })
    $hidDevices = @($Devices | Where-Object {
        $_.instance_id -match 'VID_0333&PID_0333(?:&|\\|$)' -and (
            $_.class -eq 'HIDClass' -or
            ($_.properties['DEVPKEY_Device_CompatibleIds'] -join ' ') -match 'USB\\Class_03'
        )
    })
    $dfuDevices = @($Devices | Where-Object { $_.instance_id -match 'VID_0483&PID_DF11(?:&|\\|$)' })
    $cdcDevices = @($Devices | Where-Object {
        $_.instance_id -match 'VID_CAFE&PID_4009(?:&|\\|$)' -and $_.class -eq 'Ports'
    })
    [ordered]@{
        midi_interface_present = ($midiDevices.Count -gt 0)
        updater_hid_interface_present = ($hidDevices.Count -gt 0)
        st_rom_dfu_vid_pid_present = ($dfuDevices.Count -gt 0)
        diagnostic_cdc_port_present = ($cdcDevices.Count -gt 0)
    }
}

$ErrorActionPreference = 'Stop'
$workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$capturePath = Join-Path $workspacePath 'analysis/local/usb'
$capturedAt = [DateTime]::UtcNow
$knownUsbId = 'VID_(0333&PID_0333|115C&PID_572B|0483&PID_DF11|CAFE&PID_4009)'
$keys = @(
    'DEVPKEY_Device_HardwareIds',
    'DEVPKEY_Device_CompatibleIds',
    'DEVPKEY_Device_Service',
    'DEVPKEY_Device_Parent',
    'DEVPKEY_Device_BusReportedDeviceDesc',
    'DEVPKEY_Device_LocationPaths',
    'DEVPKEY_Device_ContainerId',
    'DEVPKEY_Device_DriverProvider',
    'DEVPKEY_Device_DriverVersion'
)

# Query all present devices once; retain only the known device identities/names.
# A failed CIM inventory aborts, rather than claiming that no device is connected.
$present = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
$selected = @($present | Where-Object {
    $_.InstanceId -match $knownUsbId -or
    $_.FriendlyName -match 'TheoryBoard|THY666|STM32.*Bootloader'
} | Sort-Object InstanceId)
$devices = @($selected | ForEach-Object {
    $device = $_
    $properties = [ordered]@{}
    $propertyErrors = @()
    foreach ($key in $keys) {
        try {
            $value = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName $key -ErrorAction Stop
            $properties[$key] = $value.Data
        } catch {
            $properties[$key] = $null
            $propertyErrors += [pscustomobject]@{ key = $key; error = $_.Exception.Message }
        }
    }
    [pscustomobject]@{
        status = $device.Status
        class = $device.Class
        friendly_name = $device.FriendlyName
        instance_id = $device.InstanceId
        properties = $properties
        property_errors = $propertyErrors
    }
})

$roles = Get-ThyUsbObservations -Devices $devices
$report = [ordered]@{
    format = 'thy666.windows-usb-inventory'
    version = 1
    label = $Label
    captured_utc = $capturedAt.ToString('o')
    computer_name = [Environment]::MachineName
    scope = 'Present Windows PnP devices and cached properties; no application USB reports or device-memory commands'
    limitations = @(
        'Cached IDs and driver classes are not a raw USB descriptor capture.',
        'VID/PID and device names do not prove firmware identity or application-independent boot entry.',
        'No interface match does not prove that the hardware lacks that interface; inspect query errors and driver state.'
    )
    observations = [ordered]@{
        midi_interface_present = $roles.midi_interface_present
        updater_hid_interface_present = $roles.updater_hid_interface_present
        st_rom_dfu_vid_pid_present = $roles.st_rom_dfu_vid_pid_present
        diagnostic_cdc_port_present = $roles.diagnostic_cdc_port_present
        raw_usb_descriptors_captured = $false
        device_memory_read = $false
        backup_verified = $false
        application_independent_recovery_verified = $false
        flash_ready = $false
    }
    devices = $devices
}
New-Item -ItemType Directory -Path $capturePath -Force | Out-Null
$filename = '{0}.{1}.json' -f $Label, $capturedAt.ToString('yyyyMMddTHHmmssfffffffZ')
$outputPath = Join-Path $capturePath $filename
$stream = [IO.File]::Open($outputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
    try { $writer.WriteLine(($report | ConvertTo-Json -Depth 12)) } finally { $writer.Dispose() }
} finally { $stream.Dispose() }

[pscustomobject]@{
    path = $outputPath
    captured_utc = $report.captured_utc
    selected_devices = $devices.Count
    midi_interface_present = $report.observations.midi_interface_present
    updater_hid_interface_present = $report.observations.updater_hid_interface_present
    st_rom_dfu_vid_pid_present = $report.observations.st_rom_dfu_vid_pid_present
    diagnostic_cdc_port_present = $report.observations.diagnostic_cdc_port_present
    device_memory_read = $false
    flash_ready = $false
} | ConvertTo-Json
