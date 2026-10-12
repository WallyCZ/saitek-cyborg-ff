# Builds the driver and creates dist\SaitekFF-<version>.zip for a GitHub release:
#   Install.ps1, README.md, x64\SaitekFF.dll, x86\SaitekFF.dll
param(
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

cmd /c "`"$PSScriptRoot\build.cmd`""
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$stage = Join-Path $PSScriptRoot "obj\package"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
foreach ($arch in "x64", "x86") {
    New-Item -ItemType Directory -Force (Join-Path $stage $arch) | Out-Null
    Copy-Item "bin\$arch\SaitekFF.dll" (Join-Path $stage $arch)
}
Copy-Item Install.ps1, README.md $stage

New-Item -ItemType Directory -Force dist | Out-Null
$zip = Join-Path $PSScriptRoot "dist\SaitekFF-$Version.zip"
Compress-Archive -Path "$stage\*" -DestinationPath $zip -Force
Write-Host "Created $zip"
