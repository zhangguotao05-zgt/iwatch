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
Write-Output 'Build lock exclusion/release tests passed'
