[CmdletBinding()]
param(
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [ValidateSet("DEV_A128_NAND", "PRODUCT_N16_NOR")]
    [string]$BuildProfile = "DEV_A128_NAND",
    [string]$KeilPath,
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

if ([string]::IsNullOrWhiteSpace($KeilPath)) {
    $mdk = Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Keil\Products\MDK' -ErrorAction SilentlyContinue
    if ($mdk -and $mdk.Path) {
        $KeilPath = Split-Path -Parent $mdk.Path
    }
}
if ([string]::IsNullOrWhiteSpace($KeilPath)) {
    throw 'Keil MDK was not found. Pass its installation root with -KeilPath.'
}
$KeilPath = [System.IO.Path]::GetFullPath($KeilPath)
if (-not (Test-Path -LiteralPath (Join-Path $KeilPath 'ARM\ARMCLANG\bin\armclang.exe'))) {
    throw "Arm Compiler 6 was not found under: $KeilPath"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-common.ps1')
$buildRoot = Get-IwatchBuildRoot -RepositoryRoot $repositoryRoot -SdkPath $SdkPath
$firmwareRoot = Join-Path $buildRoot 'firmware'
$projectDir = Join-Path $firmwareRoot 'iwatch\project'
$boardDir = Join-Path $firmwareRoot 'boards'
$repositoryProjectDir = Join-Path $PSScriptRoot 'iwatch\project'

# 激活 SDK 环境后选择已安装的 Keil 工具链。
. $exportScript -t gcc
if ($LASTEXITCODE -ne 0) {
    throw "SiFli SDK environment activation failed with exit code $LASTEXITCODE"
}
$env:RTT_CC = 'keil'
$env:RTT_EXEC_PATH = $KeilPath
# SDK 的部分增量转换动作直接调用 fromelf，不能只依赖编译环境的局部 PATH。
$env:PATH = (Join-Path $KeilPath 'ARM\ARMCLANG\bin') + [IO.Path]::PathSeparator + $env:PATH

$compiler = Join-Path $KeilPath 'ARM\ARMCLANG\bin\armclang.exe'
$buildDir = Join-Path $projectDir 'build_iwatch_sf32lb58_a128_qspi_hcpu'
$identityScript = Join-Path $firmwareRoot 'build_identity.py'
$identityArgs = @('--profile', $BuildProfile, '--sdk', $SdkPath, '--toolchain', 'keil',
    '--compiler', $compiler, '--build-dir', $buildDir, '--state', (Join-Path $projectDir '.iwatch-build-state.json'))
$buildLock = Enter-IwatchBuildLock -ProjectDir $projectDir
Push-Location $projectDir
try {
    & python $identityScript 'begin' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建来源校验失败。' }
    & python (Join-Path $firmwareRoot 'check_layout.py')
    if ($LASTEXITCODE -ne 0) { throw 'Memory layout validation failed.' }
    & scons '--board=iwatch_sf32lb58_a128_qspi' "--board_search_path=$boardDir" '--target=mdk5' "-j$Jobs"
    if ($LASTEXITCODE -ne 0) {
        throw "Keil project generation failed with exit code $LASTEXITCODE"
    }
    $keilProject = Join-Path $projectDir 'project.uvprojx'
    $projectText = [System.IO.File]::ReadAllText($keilProject)
    $projectText = $projectText.Replace('<TargetName>rt-thread</TargetName>', '<TargetName>iwatch</TargetName>')

    # 为 SF32LB58 补齐生成器遗漏的 CDE 编译参数，保证重新生成后仍可编译。
    $defaultCompilerOptions = '<MiscControls>-Wno-builtin-macro-redefined </MiscControls>'
    $sf32lb58CompilerOptions = '<MiscControls>-Wno-builtin-macro-redefined -march=armv8-m.main+cdecp1</MiscControls>'
    if (-not $projectText.Contains($defaultCompilerOptions)) {
        throw 'Generated Keil project does not contain the expected compiler options block.'
    }
    $projectText = $projectText.Replace($defaultCompilerOptions, $sf32lb58CompilerOptions)

    # 把 SDK 引用改为有效绝对路径，保证从实际工程目录打开时能够解析。
    $relativeSdkPath = [System.IO.Path]::GetRelativePath($projectDir, $SdkPath)
    $escapedSdkPath = [System.Security.SecurityElement]::Escape($SdkPath)
    $projectText = $projectText.Replace($relativeSdkPath, $escapedSdkPath)
    [System.IO.File]::WriteAllText($keilProject, $projectText, [System.Text.UTF8Encoding]::new($false))

    [xml]$projectXml = [System.IO.File]::ReadAllText((Join-Path $repositoryProjectDir 'project.uvprojx'))
    $missingFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($fileNode in $projectXml.SelectNodes('//FilePath')) {
        $filePath = [Environment]::ExpandEnvironmentVariables($fileNode.InnerText)
        if ([System.IO.Path]::IsPathRooted($filePath)) {
            $absoluteFilePath = $filePath
        }
        else {
            $absoluteFilePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryProjectDir $filePath))
        }
        if (-not (Test-Path -LiteralPath $absoluteFilePath)) {
            $missingFiles.Add($fileNode.InnerText)
        }
    }
    if ($missingFiles.Count -ne 0) {
        throw "Generated Keil project has $($missingFiles.Count) missing source references. First missing file: $($missingFiles[0])"
    }

    Write-Output "Keil project: $keilProject"
    Write-Output "Repository file: $(Join-Path $repositoryProjectDir 'project.uvprojx')"
    Write-Output "Validated source references: $($projectXml.SelectNodes('//FilePath').Count)"

    # --target=mdk5 会启用 SDK 的 no_exec，仅输出工程和模拟构建日志。
    # 必须另起一次不带 --target 的构建，才能获得真正的 Arm Compiler 镜像。
    & scons '--board=iwatch_sf32lb58_a128_qspi' "--board_search_path=$boardDir" "-j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw 'Keil 实际编译或链接失败。' }

    & python $identityScript 'finish' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建后配置、镜像或来源校验失败，产物不可发布。' }
    & python $identityScript 'verify' @identityArgs
    if ($LASTEXITCODE -ne 0) { throw '构建产物身份复核失败。' }
}
finally {
    Pop-Location
    $buildLock.Dispose()
}
