[CmdletBinding()]
param(
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [ValidateRange(1, 64)]
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkPath)) {
    $SdkPath = 'C:\OpenSiFli\SiFli-SDK-v2.5.1'
}
$SdkPath = [System.IO.Path]::GetFullPath($SdkPath)
$exportScript = Join-Path $SdkPath 'export.ps1'
if (-not (Test-Path -LiteralPath $exportScript)) {
    throw "SiFli SDK export script was not found: $exportScript"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-common.ps1')
$buildRoot = Get-IwatchBuildRoot -RepositoryRoot $repositoryRoot -SdkPath $SdkPath
$firmwareRoot = Join-Path $buildRoot 'firmware'
$projectDir = Join-Path $firmwareRoot 'iwatch\project'
$boardDir = Join-Path $firmwareRoot 'boards'

. $exportScript -t gcc
if ($LASTEXITCODE -ne 0) {
    throw "SiFli SDK environment activation failed with exit code $LASTEXITCODE"
}

Push-Location $projectDir
try {
    & scons '--board=iwatch_sf32lb58_a128_qspi' "--board_search_path=$boardDir" "-j$Jobs"
    if ($LASTEXITCODE -ne 0) {
        throw "Firmware build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
