$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$ClientDir = Join-Path $Root "GBN_client"
$ClientExe = Join-Path $ClientDir "x64\Debug\GBN_client.exe"

if (!(Test-Path $ClientExe)) {
    throw "Client exe not found. Run scripts\build-cl.ps1 -Target client first."
}

chcp 65001 | Out-Null
Set-Location $ClientDir
& $ClientExe
