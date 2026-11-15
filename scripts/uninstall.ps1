#Requires -RunAsAdministrator
<#
  Removes the virtual touchpad device and its driver package.
  -RemoveCertificate also removes the self-signed certificate install.ps1 trusted.

    powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 [-RemoveCertificate]
#>
param([switch]$RemoveCertificate)
$ErrorActionPreference = 'Continue'
$CertSubject = 'CN=VirtualPtp Local Driver Signing'

Get-Process MiddlePtp -ErrorAction SilentlyContinue | Stop-Process -Force

# Elevated autostart (install.ps1 -AutoStartElevated)
if (Get-ScheduledTask -TaskName 'MiddlePtp' -ErrorAction SilentlyContinue) {
    Unregister-ScheduledTask -TaskName 'MiddlePtp' -Confirm:$false
    Write-Host 'Removed MiddlePtp logon task'
}
$installDir = Join-Path $env:ProgramFiles 'VirtualPtp'
if (Test-Path $installDir) {
    Remove-Item $installDir -Recurse -Force
    Write-Host "Removed $installDir"
}

$setup = Join-Path $PSScriptRoot 'vptp-setup.exe'
if (Test-Path $setup) {
    & $setup remove
} else {
    Write-Warning 'vptp-setup.exe not found; remove "Virtual Precision Touchpad" in Device Manager manually.'
}

# Driver store packages (locale-independent, unlike parsing pnputil's text output)
Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -like '*\virtualptp.inf' } | ForEach-Object {
    Write-Host "Deleting driver package $($_.Driver)"
    pnputil /delete-driver $_.Driver /uninstall /force | Out-Null
}

if ($RemoveCertificate) {
    foreach ($store in 'My', 'Root', 'TrustedPublisher') {
        Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $CertSubject | ForEach-Object {
            if ($_.HasPrivateKey) { Remove-Item $_.PSPath -DeleteKey } else { Remove-Item $_.PSPath }
            Write-Host "Removed certificate from LocalMachine\$store"
        }
    }
}
Write-Host 'Uninstall finished.'