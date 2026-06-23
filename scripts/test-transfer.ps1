param(
    [ValidateSet("gbn", "sr", "all")]
    [string]$Mode = "all",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$ClientDir = Join-Path $Root "GBN_client"
$ServerDir = Join-Path $Root "GBN_sever"
$ClientExe = Join-Path $ClientDir "x64\Debug\GBN_client.exe"
$ServerExe = Join-Path $ServerDir "x64\Debug\GBN_sever.exe"
$ClientInput = Join-Path $ClientDir "test.txt"
$ServerOutput = Join-Path $ServerDir "data.txt"
$TempDir = Join-Path $Root "scripts\.tmp"

if (!(Test-Path $ClientExe)) { throw "Client exe not found: $ClientExe" }
if (!(Test-Path $ServerExe)) { throw "Server exe not found: $ServerExe" }
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

$payload = @"
GBN/SR transfer verification payload.
This text intentionally crosses packet boundaries by repeating deterministic content.
0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ
"@
$payload = ($payload * 60)
$payload = $payload.TrimEnd()

$originalInput = $null
$hadOriginalInput = Test-Path $ClientInput
if ($hadOriginalInput) {
    $originalInput = [IO.File]::ReadAllBytes($ClientInput)
}

$originalOutput = $null
$hadOriginalOutput = Test-Path $ServerOutput
if ($hadOriginalOutput) {
    $originalOutput = [IO.File]::ReadAllBytes($ServerOutput)
}

function Stop-IfRunning($Process) {
    if ($null -ne $Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        $Process.WaitForExit()
    }
}

function Start-TestProcess($Exe, $WorkingDir, $InputText = $null) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.WorkingDirectory = $WorkingDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = ($null -ne $InputText)
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false
    if ($null -ne $psi.EnvironmentVariables) {
        $psi.EnvironmentVariables.Clear()
        $psi.EnvironmentVariables["SystemRoot"] = $env:SystemRoot
        $psi.EnvironmentVariables["WINDIR"] = $env:WINDIR
        $psi.EnvironmentVariables["PATH"] = "$env:SystemRoot\System32;$env:SystemRoot"
    }
    else {
        $psi.Environment.Clear()
        $psi.Environment["SystemRoot"] = $env:SystemRoot
        $psi.Environment["WINDIR"] = $env:WINDIR
        $psi.Environment["PATH"] = "$env:SystemRoot\System32;$env:SystemRoot"
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    if ($null -ne $InputText) {
        $process.StandardInput.Write($InputText)
        $process.StandardInput.Close()
    }
    return $process
}

function Run-One($Protocol) {
    Remove-Item -Force -ErrorAction SilentlyContinue $ServerOutput
    [IO.File]::WriteAllText($ClientInput, $payload, [Text.Encoding]::ASCII)
    $stdin = "-test$Protocol`r`n-quit`r`n"

    $server = $null
    $client = $null
    try {
        $server = Start-TestProcess $ServerExe $ServerDir
        Start-Sleep -Milliseconds 700

        $client = Start-TestProcess $ClientExe $ClientDir $stdin

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            if ($client.HasExited) { break }
            Start-Sleep -Milliseconds 250
        }

        if (!$client.HasExited) {
            throw "$Protocol client did not finish within $TimeoutSeconds seconds"
        }

        if (!(Test-Path $ServerOutput)) {
            throw "$Protocol did not create server output file"
        }

        $expected = [IO.File]::ReadAllText($ClientInput, [Text.Encoding]::ASCII)
        $actual = [IO.File]::ReadAllText($ServerOutput, [Text.Encoding]::ASCII)
        if ($expected -ne $actual) {
            throw "$Protocol output mismatch: expected $($expected.Length) chars, got $($actual.Length) chars"
        }

        Write-Host "PASS $Protocol"
    }
    finally {
        Stop-IfRunning $client
        Stop-IfRunning $server
    }
}

try {
    if ($Mode -eq "all" -or $Mode -eq "gbn") { Run-One "gbn" }
    if ($Mode -eq "all" -or $Mode -eq "sr") { Run-One "sr" }
}
finally {
    if ($hadOriginalInput) {
        [IO.File]::WriteAllBytes($ClientInput, $originalInput)
    }
    if ($hadOriginalOutput) {
        [IO.File]::WriteAllBytes($ServerOutput, $originalOutput)
    }
}
