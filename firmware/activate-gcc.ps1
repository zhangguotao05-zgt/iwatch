[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SdkPath,
    [string]$BaseSdkPath = $env:IWATCH_BASE_SDK,
    [string]$BootstrapPath
)

$ErrorActionPreference = 'Stop'
$exportScript = Join-Path $SdkPath 'export.ps1'
foreach ($required in @($exportScript)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "GCC environment prerequisite is missing: $required"
    }
}

# 只影响当前 PowerShell 会话；使用点调用后再运行现有构建入口。
if ($BootstrapPath -and ($env:PATH -split ';') -notcontains $BootstrapPath) {
    if (-not (Test-Path -LiteralPath (Join-Path $BootstrapPath 'uv.exe') -PathType Leaf)) {
        throw 'BootstrapPath must contain the already installed uv.exe.'
    }
    $env:PATH = $BootstrapPath + ';' + $env:PATH
}
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) { throw 'Install/configure uv before activating the SDK.' }
if ($BaseSdkPath) { $env:IWATCH_BASE_SDK = [IO.Path]::GetFullPath($BaseSdkPath) }
$env:IWATCH_PATCHED_SDK = [IO.Path]::GetFullPath($SdkPath)
. $exportScript -t gcc
if ($LASTEXITCODE -ne 0) { throw 'SiFli GCC environment activation failed.' }

& arm-none-eabi-gcc --version
if ($LASTEXITCODE -ne 0) { throw 'GCC version check failed.' }
& python --version
if ($LASTEXITCODE -ne 0) { throw 'Python version check failed.' }
