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
#>
param([switch]$ForgetSigningKey)
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
Write-Host 'Next: start MiddlePtp.exe and check the touchpad settings listed in README.md.'
