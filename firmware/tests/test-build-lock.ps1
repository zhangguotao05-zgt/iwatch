$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-common.ps1')
$testDir = Join-Path $PSScriptRoot 'build/lock-test'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$first = Enter-IwatchBuildLock -ProjectDir $testDir
$blocked = $false
try {
    try {
        $second = Enter-IwatchBuildLock -ProjectDir $testDir
        $second.Dispose()
    }
    catch { $blocked = $true }
    if (-not $blocked) { throw '共享输出锁未能拒绝第二个构建。' }
}
finally { $first.Dispose() }
$after = Enter-IwatchBuildLock -ProjectDir $testDir
$after.Dispose()

$approvedBuildDir = Join-Path $testDir 'build_iwatch_sf32lb58_a128_qspi_hcpu'
New-Item -ItemType Directory -Path $approvedBuildDir -Force | Out-Null
Set-Content -LiteralPath (Join-Path $approvedBuildDir 'stale.map') -Value 'stale'
Reset-IwatchBuildDirectory -ProjectDir $testDir -BuildDir $approvedBuildDir
if (Test-Path -LiteralPath $approvedBuildDir) { throw '已批准的共享输出目录没有被完整清理。' }
if (-not (Test-Path -LiteralPath $testDir)) { throw '构建目录清理越过了项目边界。' }

$outside = Join-Path (Split-Path -Parent $testDir) 'not-approved'
New-Item -ItemType Directory -Path $outside -Force | Out-Null
Set-Content -LiteralPath (Join-Path $outside 'sentinel.txt') -Value 'keep'
$rejected = $false
try { Reset-IwatchBuildDirectory -ProjectDir $testDir -BuildDir $outside }
catch { $rejected = $true }
if (-not $rejected) { throw '构建目录清理没有拒绝项目边界外路径。' }
if (-not (Test-Path -LiteralPath (Join-Path $outside 'sentinel.txt'))) {
    throw '被拒绝的目录发生了修改。'
}
Write-Output 'Build lock exclusion/release and safe reset tests passed'

$relativeBase = Join-Path $testDir 'base\project'
$relativeTarget = Join-Path $testDir 'sdk root'
New-Item -ItemType Directory -Path $relativeBase, $relativeTarget -Force | Out-Null
$relativeActual = Get-IwatchRelativePath -BasePath $relativeBase -TargetPath $relativeTarget
if ($relativeActual -ne '..\..\sdk root') {
    throw "兼容相对路径计算错误：$relativeActual"
}
Write-Output 'PowerShell 5.1 relative path compatibility test passed'
