#Requires -RunAsAdministrator
<#
  Installs the VirtualPtp virtual Precision Touchpad from the downloaded release folder.
  Needs nothing else installed (no Visual Studio, no WDK).

  What it does:
    1. creates a self-signed code-signing certificate (once) and trusts it on THIS PC only
    2. signs driver\VirtualPtp.dll and driver\VirtualPtp.cat with it
    3. creates the Root\VirtualPtp device and installs the driver (vptp-setup.exe)

  No test mode: this is a user-mode (UMDF) driver.

  Usage, from the extracted folder in an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File .\install.ps1
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -ForgetSigningKey   # see README
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -AutoStartElevated  # start MiddlePtp elevated at every login
#>
param(
    [switch]$ForgetSigningKey,
    # Copy MiddlePtp.exe to Program Files and register a logon task that runs it elevated
    # (no UAC prompt), so middle-drag also works over admin windows.
    [switch]$AutoStartElevated
)
$ErrorActionPreference = 'Stop'
$CertSubject = 'CN=VirtualPtp Local Driver Signing'

$root   = $PSScriptRoot
$driver = Join-Path $root 'driver'
$inf    = Join-Path $driver 'VirtualPtp.inf'
$dll    = Join-Path $driver 'VirtualPtp.dll'
$cat    = Join-Path $driver 'VirtualPtp.cat'
$setup  = Join-Path $root 'vptp-setup.exe'
foreach ($f in $inf, $dll, $cat, $setup) { if (-not (Test-Path $f)) { throw "Missing $f - run this from the extracted release folder." } }

if ([Environment]::OSVersion.Version.Build -lt 22000) { throw 'VirtualPtp needs Windows 11 (build 22000 or newer).' }

# Files downloaded from GitHub carry the "from the internet" mark; clear it for our own files.
Get-ChildItem $root -Recurse -File | Unblock-File

# --- certificate -------------------------------------------------------------
$cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $CertSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
        Select-Object -First 1
if (-not $cert) {
    Write-Host 'Creating self-signed code signing certificate...'
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject `
        -CertStoreLocation Cert:\LocalMachine\My -KeyAlgorithm RSA -KeyLength 3072 `
        -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)
}
$cerFile = Join-Path $env:TEMP 'VirtualPtp.cer'
Export-Certificate -Cert $cert -FilePath $cerFile | Out-Null
foreach ($store in 'Root', 'TrustedPublisher') {
    if (-not (Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Thumbprint -eq $cert.Thumbprint)) {
        Import-Certificate -FilePath $cerFile -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
        Write-Host "Trusted certificate in LocalMachine\$store"
    }
}
Remove-Item $cerFile -ErrorAction SilentlyContinue

# --- sign ---------------------------------------------------------------------------
foreach ($file in $dll, $cat) {
    $sig = Set-AuthenticodeSignature -FilePath $file -Certificate $cert -HashAlgorithm SHA256
    if ($sig.Status -ne 'Valid') { throw "Signing $file failed: $($sig.Status) $($sig.StatusMessage)" }
    Write-Host "Signed $(Split-Path $file -Leaf)"
}

if ($ForgetSigningKey) {
    Remove-Item "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey
    Write-Host 'Private signing key deleted. A new certificate will be created on the next install.'
}

# --- install ------------------------------------------------------------------------
& $setup install $inf
switch ($LASTEXITCODE) {
    0       { Write-Host 'Done. "Virtual Precision Touchpad" is now under Human Interface Devices in Device Manager.' -ForegroundColor Green }
    1       { Write-Host 'Installed, but Windows wants a reboot to finish.' -ForegroundColor Yellow }
    default { throw "vptp-setup failed with exit code $LASTEXITCODE" }
}

# --- optional: elevated autostart -------------------------------------------------------
if ($AutoStartElevated) {
    $TaskName   = 'MiddlePtp'
    $installDir = Join-Path $env:ProgramFiles 'VirtualPtp'
    $exe        = Join-Path $installDir 'MiddlePtp.exe'

    # An elevated autostart must point into an admin-only folder: if it ran from e.g. Downloads,
    # any non-admin program could swap the exe and get admin rights at your next login.
    Get-Process MiddlePtp -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    New-Item -ItemType Directory -Force $installDir | Out-Null
    Copy-Item (Join-Path $root 'MiddlePtp.exe') $exe -Force
    $oldIni = Join-Path $root 'MiddlePtp.ini'
    if ((Test-Path $oldIni) -and -not (Test-Path (Join-Path $installDir 'MiddlePtp.ini'))) {
        Copy-Item $oldIni $installDir   # keep settings from a previous run
    }

    # The interactive user, not the admin account that might have approved the UAC prompt.
    $user = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $user) { $user = "$env:USERDOMAIN\$env:USERNAME" }

    $action    = New-ScheduledTaskAction -Execute $exe -WorkingDirectory $installDir
    $trigger   = New-ScheduledTaskTrigger -AtLogOn -User $user
    $trigger.Delay = 'PT5S'   # let Explorer's tray come up first
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
    $settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                     -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -StartWhenAvailable
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal `
        -Settings $settings -Description 'MiddlePtp: middle-drag to touchpad scrolling (elevated)' -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "MiddlePtp installed to $installDir and started elevated; it will start automatically at every login for $user." -ForegroundColor Green
    Write-Host "Settings file: $installDir\MiddlePtp.ini (or use the tray menu)."
} else {
    Write-Host 'Next: start MiddlePtp.exe (or re-run with -AutoStartElevated) and check the touchpad settings in README.md.'
}