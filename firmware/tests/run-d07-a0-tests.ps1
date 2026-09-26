[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PythonPath,
    [Parameter(Mandatory = $true)]
    [string]$PatchedSdkPath,
    [string]$BaseSdkPath = 'C:\OpenSiFli\SiFli-SDK-v2.5.1-iwatch-locked',
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$firmwareDir = Split-Path -Parent $testsDir
$repositoryRoot = Split-Path -Parent $firmwareDir
$outputRoot = Join-Path $testsDir 'build\d07-a0'
$hostBuild = Join-Path $outputRoot 'tiny-ttf-oom'
$fontPath = Join-Path $outputRoot 'DroidSansFallback.subset.ttf'
$fontManifest = Join-Path $outputRoot 'font_manifest.json'

foreach ($path in @($PythonPath, $PatchedSdkPath, $BaseSdkPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "路径不存在: $path" }
}

# Python 子测试使用同一对 SDK 路径，不回退到机器上的默认目录。
$env:IWATCH_BASE_SDK = [IO.Path]::GetFullPath($BaseSdkPath)
$env:IWATCH_PATCHED_SDK = [IO.Path]::GetFullPath($PatchedSdkPath)

# 先验证基础 SDK 与派生 SDK，避免主机测试落在手工修改的源码上。
& $PythonPath (Join-Path $firmwareDir 'sdk_patch.py') verify-base --sdk $BaseSdkPath
if ($LASTEXITCODE -ne 0) { throw '固定 SDK 校验失败。' }
& $PythonPath (Join-Path $firmwareDir 'sdk_patch.py') verify-derived --sdk $PatchedSdkPath
if ($LASTEXITCODE -ne 0) { throw '派生 SDK 校验失败。' }

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
# 先验证仓库正式产物，临时生成成功不能替代正式清单的输入一致性。
$committedFonts = Join-Path $firmwareDir 'iwatch\src\resource\fonts'
& $PythonPath (Join-Path $firmwareDir 'generate_font_subset.py') `
    --output (Join-Path $committedFonts 'DroidSansFallback.ttf') `
    --manifest (Join-Path $committedFonts 'DroidSansFallback.subset.json') --check
if ($LASTEXITCODE -ne 0) { throw '正式字体或清单过期，拒绝继续回归。' }
& $PythonPath (Join-Path $firmwareDir 'generate_font_subset.py') --output $fontPath --manifest $fontManifest
if ($LASTEXITCODE -ne 0) { throw '字体子集生成失败。' }
& $PythonPath (Join-Path $firmwareDir 'generate_font_subset.py') --output $fontPath --manifest $fontManifest --check
if ($LASTEXITCODE -ne 0) { throw '字体子集重复生成校验失败。' }

if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator)) { throw '未找到 Visual Studio 定位工具。' }
    $VisualStudioPath = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) { throw '未找到 Visual Studio C/C++ 工具。' }
Import-Module (Join-Path $VisualStudioPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$cmake = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw '未找到 Visual Studio 附带的 CMake。' }

# 只允许清理测试专用目录，防止旧依赖信息掩盖 SDK 头文件变化。
$expectedParent = [IO.Path]::GetFullPath($outputRoot).TrimEnd('\') + '\'
$resolvedBuild = [IO.Path]::GetFullPath($hostBuild)
if (-not $resolvedBuild.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw "拒绝清理测试目录之外的路径: $resolvedBuild"
}
if (Test-Path -LiteralPath $resolvedBuild) {
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$configureLog = Join-Path $outputRoot 'cmake-configure.log'
$buildLog = Join-Path $outputRoot 'cmake-build.log'
& $cmake -S (Join-Path $testsDir 'd07_tiny_ttf_oom') -B $resolvedBuild -G Ninja "-DSDK_ROOT=$PatchedSdkPath" *> $configureLog
if ($LASTEXITCODE -ne 0) { Get-Content $configureLog -Tail 80; throw 'tiny_ttf 主机测试配置失败。' }
# MSVC 并发写入同一 PDB 会偶发 C1090；主机门禁使用串行编译保证可复现。
& $cmake --build $resolvedBuild --parallel 1 *> $buildLog
if ($LASTEXITCODE -ne 0) { Get-Content $buildLog -Tail 80; throw 'tiny_ttf 主机测试编译失败。' }

& $PythonPath (Join-Path $testsDir 'd07_tiny_ttf_oom\run_oom_matrix.py') `
    --executable (Join-Path $resolvedBuild 'test_tiny_ttf_oom.exe') --font $fontPath --repeat 1000
if ($LASTEXITCODE -ne 0) { throw 'tiny_ttf 内存故障矩阵失败。' }
& $PythonPath (Join-Path $testsDir 'd07_tiny_ttf_oom\run_product_matrix.py') `
    --executable (Join-Path $resolvedBuild 'test_tiny_ttf_oom.exe') --font $fontPath
if ($LASTEXITCODE -ne 0) { throw 'D10 页面与全局结果客户端回归失败。' }

Push-Location $repositoryRoot
try {
    & $PythonPath -m unittest firmware.tests.test_build_identity firmware.tests.test_font_subset `
        firmware.tests.test_resource_budget firmware.tests.test_sdk_patch -v
    if ($LASTEXITCODE -ne 0) { throw 'D07-A0 Python 回归失败。' }

    $archiveRoot = Join-Path $firmwareDir 'iwatch\project\artifacts\DEV_A128_NAND'
    foreach ($toolchain in @('gcc', 'keil')) {
        $archive = Join-Path $archiveRoot $toolchain
        if (Test-Path -LiteralPath (Join-Path $archive 'build_identity.json')) {
            & $PythonPath (Join-Path $firmwareDir 'resource_budget.py') --build-dir $archive `
                --toolchain $toolchain --output (Join-Path $outputRoot "$toolchain-resource-budget.json") --report-only
            if ($LASTEXITCODE -ne 0) { throw "$toolchain 资源预算基线解析失败。" }
        }
    }
}
finally {
    Pop-Location
}

Write-Host 'D07-A0 HOST TESTS OK'
