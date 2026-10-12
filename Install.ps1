param(
    [switch]$Uninstall
)

If ([IntPtr]::Size -ne 8) {
    Write-Host "Script is not running as 64bit!!"
    Exit
}

# Verify that user running script is an administrator, otherwise relaunch elevated
$IsAdmin = [Security.Principal.WindowsIdentity]::GetCurrent()
If ((New-Object Security.Principal.WindowsPrincipal $IsAdmin).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator) -eq $FALSE) {
    $newProcess = New-Object System.Diagnostics.ProcessStartInfo "PowerShell"
    $newProcess.Arguments = "-ExecutionPolicy Bypass -NoExit -File `"$($myInvocation.MyCommand.Definition)`""
    if ($Uninstall) { $newProcess.Arguments += " -Uninstall" }
    $newProcess.Verb = "runas"
    [System.Diagnostics.Process]::Start($newProcess) | Out-Null
    exit
}

Set-Location $PSScriptRoot

Write-Host "Saitek Cyborg force feedback driver for Windows 10/11 (https://github.com/WallyCZ/saitek-cyborg-ff)"

$devices = @(
    @{ Name = "Saitek Cyborg Evo Force"; HardwareId = "VID_06A3&PID_FFB5"; RenamedDevice = "Saitek Cyborg Evo FF" },
    @{ Name = "Saitek Cyborg 3D Rumble Force"; HardwareId = "VID_06A3&PID_FF52"; RenamedDevice = "Saitek Cyborg 3D Rumble FF" },
    @{ Name = "Saitek Cyborg 3D Force Stick"; HardwareId = "VID_06A3&PID_FF12"; RenamedDevice = "Saitek Cyborg 3D FS" }
)
$installDir = Join-Path $env:ProgramFiles "SaitekFF"
$regsvr64 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
$regsvr32 = Join-Path $env:SystemRoot "SysWOW64\regsvr32.exe"

function Find-Dll($arch) {
    foreach ($path in "$PSScriptRoot\$arch\SaitekFF.dll", "$PSScriptRoot\bin\$arch\SaitekFF.dll") {
        if (Test-Path $path) { return $path }
    }
    return $null
}

function Register-Dll($regsvr, $dll, [switch]$Unregister) {
    $arguments = @("/s")
    if ($Unregister) { $arguments += "/u" }
    $process = Start-Process $regsvr -ArgumentList ($arguments + "`"$dll`"") -Wait -PassThru
    return $process.ExitCode -eq 0
}

# The original Saitek driver replaces the stick's USB driver with Immersion's kernel driver, which talks a
# different protocol. It has to go, so the stick uses the Windows HID driver this driver is built for.
function Remove-SaitekDriver {
    $packages = @()
    $published = $null
    foreach ($line in (pnputil /enum-drivers)) {
        if ($line -match ':\s*(oem\d+\.inf)\s*$') { $published = $Matches[1] }
        elseif ($line -match ':\s*(sai\w*\.inf)\s*$' -and $published) { $packages += @{ Published = $published; Original = $Matches[1] } }
    }
    $inUse = @()
    foreach ($device in $devices) {
        Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like "USB\$($device.HardwareId)*" } | ForEach-Object {
            $service = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service).Data
            if ($service -and $service -ne "HidUsb") { $inUse += "$($device.Name) uses driver service '$service'" }
        }
    }
    if (-not $packages -and -not $inUse) { return $true }

    Write-Host ""
    Write-Host "The original Saitek driver is installed:" -ForegroundColor Yellow
    $inUse | ForEach-Object { Write-Host "  $_" }
    $packages | ForEach-Object { Write-Host "  driver package $($_.Original) ($($_.Published))" }
    Write-Host "It must be removed, the stick then uses the standard Windows HID driver."
    $confirmation = Read-Host "Remove the Saitek driver now? [Y/n]"
    if ($confirmation -ne "" -and $confirmation -ne "y" -and $confirmation -ne "Y") { return $false }
    foreach ($package in $packages) {
        pnputil /delete-driver $package.Published /uninstall /force | Out-Null
        Write-Host "Removed $($package.Original)"
    }
    pnputil /scan-devices | Out-Null
    Write-Host "Unplug the stick and plug it back in if force feedback does not work right away."
    return $true
}

if ($Uninstall) {
    foreach ($arch in @(@{ Name = "x64"; Regsvr = $regsvr64 }, @{ Name = "x86"; Regsvr = $regsvr32 })) {
        $dll = Join-Path $installDir "$($arch.Name)\SaitekFF.dll"
        if (Test-Path $dll) { Register-Dll $arch.Regsvr $dll -Unregister | Out-Null }
    }
    Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Driver removed."
    exit
}

$dll64 = Find-Dll "x64"
$dll32 = Find-Dll "x86"
if (-not $dll64 -or -not $dll32) {
    Write-Host "SaitekFF.dll not found next to this script (x64 and x86 folders). Download a release or run build.cmd first." -ForegroundColor Red
    exit
}

if (-not (Remove-SaitekDriver)) {
    Write-Host "Installation cancelled, the driver cannot work while the Saitek driver is installed." -ForegroundColor Red
    exit
}

# Copy both builds: 64-bit games load x64, 32-bit games load x86
try {
    foreach ($arch in "x64", "x86") {
        New-Item -ItemType Directory -Force (Join-Path $installDir $arch) | Out-Null
    }
    Copy-Item $dll64 (Join-Path $installDir "x64\SaitekFF.dll") -Force -ErrorAction Stop
    Copy-Item $dll32 (Join-Path $installDir "x86\SaitekFF.dll") -Force -ErrorAction Stop
}
catch {
    Write-Host "Cannot copy the driver to $installDir. Close all games and controller tools and run the script again." -ForegroundColor Red
    exit
}

if (-not (Register-Dll $regsvr64 (Join-Path $installDir "x64\SaitekFF.dll")) -or -not (Register-Dll $regsvr32 (Join-Path $installDir "x86\SaitekFF.dll"))) {
    Write-Host "Registration failed." -ForegroundColor Red
    exit
}
Write-Host "Driver installed to $installDir" -ForegroundColor Green

$confirmation = Read-Host "Do you want to change device name? (needed for some games like DCS) [Y/n]"
if (($confirmation -eq 'y') -or ($confirmation -eq 'Y') -or ($confirmation -eq '')) {
    foreach ($device in $devices) {
        $registryPath = "HKCU:\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\$($device.HardwareId)"
        if (Test-Path $registryPath) {
            New-ItemProperty -Path $registryPath -Name 'OEMName' -Value $device.RenamedDevice -PropertyType String -Force | Out-Null
            Write-Host "Renamed $($device.Name) to $($device.RenamedDevice)"
        }
    }
}
Write-Host "Done. Test force feedback in the Game Controllers panel (joy.cpl) or in a game."
