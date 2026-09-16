[CmdletBinding()]
param(
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [ValidateSet("DEV_A128_NAND", "PRODUCT_N16_NOR")]
    [string]$BuildProfile = "DEV_A128_NAND",
    [ValidateRange(1, 64)]
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkPath)) {
    $SdkPath = 'C:\OpenSiFli\SiFli-SDK-v2.5.1-iwatch-locked'
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

$compiler = (Get-Command arm-none-eabi-gcc.exe).Source
$buildDir = Join-Path $projectDir 'build_iwatch_sf32lb58_a128_qspi_hcpu'
$identityScript = Join-Path $firmwareRoot 'build_identity.py'
$identityArgs = @('--profile', $BuildProfile, '--sdk', $SdkPath, '--toolchain', 'gcc',
    '--compiler', $compiler, '--build-dir', $buildDir, '--state', (Join-Path $projectDir '.iwatch-build-state.json'))
$buildLock = Enter-IwatchBuildLock -ProjectDir $projectDir
Push-Location $projectDir
try {
    Reset-IwatchBuildDirectory -ProjectDir $projectDir -BuildDir $buildDir
    & python $identityScript 'begin' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建来源校验失败。' }
    & python (Join-Path $firmwareRoot 'check_layout.py')
    if ($LASTEXITCODE -ne 0) { throw 'Memory layout validation failed.' }
    & scons '--board=iwatch_sf32lb58_a128_qspi' "--board_search_path=$boardDir" "-j$Jobs"
    if ($LASTEXITCODE -ne 0) {
        throw "Firmware build failed with exit code $LASTEXITCODE"
    }
    & python (Join-Path $firmwareRoot 'check_layout.py') '--build-dir' (Join-Path $projectDir 'build_iwatch_sf32lb58_a128_qspi_hcpu')
    if ($LASTEXITCODE -ne 0) { throw 'Built images failed layout validation. Do not flash these artifacts.' }

    & python $identityScript 'finish' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建后配置、镜像或来源校验失败，产物不可发布。' }
    & python $identityScript 'verify' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建产物身份复核失败。' }
}
finally {
    Pop-Location
    $buildLock.Dispose()
}
