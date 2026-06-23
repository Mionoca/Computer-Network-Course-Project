$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$ServerDir = Join-Path $Root "GBN_sever"
$ServerExe = Join-Path $ServerDir "x64\Debug\GBN_sever.exe"

if (!(Test-Path $ServerExe)) {
    throw "Server exe not found. Run scripts\build-cl.ps1 -Target server first."
}

chcp 65001 | Out-Null
Set-Location $ServerDir
& $ServerExe
