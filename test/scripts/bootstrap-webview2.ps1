$ErrorActionPreference = 'Stop'

# The SDK's loader library does not install the WebView2 browser runtime.
# https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution
function Get-WebView2Version {
  $client = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
  $keys = @(
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$client",
    "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$client",
    "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$client"
  )
  foreach ($key in $keys) {
    $version = Get-ItemPropertyValue -Path $key -Name pv -ErrorAction SilentlyContinue
    if ($version -and [version]$version -gt [version]'0.0.0.0') {
      return $version
    }
  }
  return $null
}

$installedVersion = Get-WebView2Version
if ($installedVersion) {
  Write-Host "WebView2 Runtime $installedVersion is installed."
  exit 0
}

$installer = Join-Path $env:RUNNER_TEMP 'MicrosoftEdgeWebview2Setup.exe'
Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile $installer
$signature = Get-AuthenticodeSignature -FilePath $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
  throw 'The WebView2 installer must have a valid Microsoft signature.'
}

$process = Start-Process -FilePath $installer -ArgumentList '/silent', '/install' -PassThru -Wait
$installedVersion = Get-WebView2Version
if (-not $installedVersion) {
  throw "WebView2 Runtime installation failed (exit code $($process.ExitCode))."
}
Write-Host "WebView2 Runtime $installedVersion is installed."
