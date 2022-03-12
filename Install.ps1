If([IntPtr]::Size -ne 8)
{
    Write-Host "Script is not running as 64bit!!"
    Exit
}


$ver = $host | select version
if ($ver.Version.Major -gt 1)  {$Host.Runspace.ThreadOptions = "ReuseThread"}

# Verify that user running script is an administrator
$IsAdmin=[Security.Principal.WindowsIdentity]::GetCurrent()
If ((New-Object Security.Principal.WindowsPrincipal $IsAdmin).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator) -eq $FALSE)
{
  "`nERROR: You are NOT a local administrator.  Run this script after logging on with a local administrator account."
	# We are not running "as Administrator" - so relaunch as administrator

	# Create a new process object that starts PowerShell
	$newProcess = new-object System.Diagnostics.ProcessStartInfo "PowerShell";

	# Specify the current script path and name as a parameter
	$newProcess.Arguments = $myInvocation.MyCommand.Definition;

	# Indicate that the process should be elevated
	$newProcess.Verb = "runas";

	# Start the new process
	[System.Diagnostics.Process]::Start($newProcess);

	# Exit from the current, unelevated, process
	exit
}

Write-Host "Saitek Force Feedback driver fix (https://github.com/WallyCZ/saitek-cyborg-ff)"

$targets = @(
	@{ DllName="SaiQFFB5.dll"; DeviceName="Saitek Cyborg Evo Force"; HardwareId="VID_06A3&PID_FFB5"; RenamedDevice="Saitek Cyborg Evo FF" },
	@{ DllName="SaiQFF12.dll"; DeviceName="Saitek Cyborg 3D Rumble Force"; HardwareId="VID_06A3&PID_FF52"; RenamedDevice="Saitek Cyborg 3D Rumble FF" }
)

$found = $false

foreach ($target in $targets)
{
  $targetpath = ([System.Environment]::SystemDirectory + "\\" + $target.DllName)
  if (Test-Path -Path $targetpath -PathType Leaf)
  {
	Write-Host "Replacing $($target.DeviceName) driver"
	Try
	{
		Copy-Item -Path "SaiQFFB5.dll" -Destination $targetpath -force
	}
	Catch
	{
		Rename-Item -Path $targetpath -NewName ($target.DllName + ".bak")
		Copy-Item -Path "SaiQFFB5.dll" -Destination $targetpath -force
	}
	
	$confirmation = Read-Host "Do you want to change device name (needed for same games like DCS) [Y/n]"
	if (($confirmation -eq 'y') -or ($confirmation -eq 'Y') -or ($confirmation -eq '')) {
		$RegistryPath = "HKCU:\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\$($target.HardwareId)"
		$Name         = 'OEMName'
		New-ItemProperty -Path $RegistryPath -Name $Name -Value $target.RenamedDevice -PropertyType String -Force
	}
	
	$found = $true
	break
  }
}


if(-not $found)
{
	Write-Host "No Saitek FF driver found, please install it before running this script"
}
