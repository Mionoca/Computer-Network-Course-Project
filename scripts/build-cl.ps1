param(
    [ValidateSet("client", "server", "all")]
    [string]$Target = "all"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$VsRoot = "D:\vsIDE"
$VcTools = Join-Path $VsRoot "VC\Tools\MSVC\14.38.33130"
$WinSdk = "D:\Windows Kits\10"
$WinSdkVersion = "10.0.22621.0"
$Cl = Join-Path $VcTools "bin\HostX86\x64\cl.exe"

if (!(Test-Path $Cl)) { throw "cl.exe not found: $Cl" }

$IncludePath = @(
    (Join-Path $VcTools "include"),
    (Join-Path $WinSdk "Include\$WinSdkVersion\ucrt"),
    (Join-Path $WinSdk "Include\$WinSdkVersion\um"),
    (Join-Path $WinSdk "Include\$WinSdkVersion\shared")
) -join ";"

$LibPath = @(
    (Join-Path $VcTools "lib\x64"),
    (Join-Path $WinSdk "Lib\$WinSdkVersion\ucrt\x64"),
    (Join-Path $WinSdk "Lib\$WinSdkVersion\um\x64")
) -join ";"

$ToolPath = @(
    (Join-Path $VcTools "bin\HostX86\x64"),
    (Join-Path $WinSdk "bin\$WinSdkVersion\x64"),
    (Join-Path $WinSdk "bin\x64"),
    "$env:SystemRoot\System32",
    $env:SystemRoot
) -join ";"

function Invoke-CleanTool($Exe, $Arguments, $WorkingDir) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.Arguments = $Arguments
    $psi.WorkingDirectory = $WorkingDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $false
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false

    if ($null -ne $psi.EnvironmentVariables) {
        $envBlock = $psi.EnvironmentVariables
    }
    else {
        $envBlock = $psi.Environment
    }

    $envBlock.Clear()
    $envBlock["SystemRoot"] = $env:SystemRoot
    $envBlock["WINDIR"] = $env:WINDIR
    $envBlock["TEMP"] = $env:TEMP
    $envBlock["TMP"] = $env:TMP
    $envBlock["PATH"] = $ToolPath
    $envBlock["INCLUDE"] = $IncludePath
    $envBlock["LIB"] = $LibPath
    $envBlock["LIBPATH"] = $LibPath

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $Exe $Arguments"
    }
}

function Build-One($Name, $SourceRelative, $ExeRelative, $ObjName) {
    $source = Join-Path $Root $SourceRelative
    $exe = Join-Path $Root $ExeRelative
    $outDir = Split-Path -Parent $exe
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $obj = Join-Path $outDir $ObjName
    $pdb = Join-Path $outDir "vc143.pdb"
    $args = @(
        "/nologo",
        "/utf-8",
        "/EHsc",
        "/W3",
        "/Od",
        "/Zi",
        "/MDd",
        "/D_DEBUG",
        "/D_CONSOLE",
        "/D_CRT_SECURE_NO_WARNINGS",
        "/D_WINSOCK_DEPRECATED_NO_WARNINGS",
        "/Fo`"$obj`"",
        "/Fd`"$pdb`"",
        "/Fe`"$exe`"",
        "`"$source`"",
        "/link",
        "/DEBUG",
        "/SUBSYSTEM:CONSOLE",
        "ws2_32.lib"
    ) -join " "

    Write-Host "Building $Name..."
    Invoke-CleanTool $Cl $args $Root
}

if ($Target -eq "all" -or $Target -eq "client") {
    Build-One "client" "GBN_client\client.cpp" "GBN_client\x64\Debug\GBN_client.exe" "client.obj"
}

if ($Target -eq "all" -or $Target -eq "server") {
    Build-One "server" "GBN_sever\sever.cpp" "GBN_sever\x64\Debug\GBN_sever.exe" "sever.obj"
}
