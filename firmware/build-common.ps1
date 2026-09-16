function Get-IwatchBuildRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string]$SdkPath
    )

    $RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)
    if ($RepositoryRoot -notmatch '[^\x00-\x7F]') {
        return $RepositoryRoot
    }

    # SDK 混用 ANSI 与 UTF-8 路径，中文工作区通过固定的 ASCII 联接构建。
    $sdkParent = Split-Path -Parent $SdkPath
    $aliasParent = Join-Path $sdkParent '_workspaces'
    $aliasRoot = Join-Path $aliasParent 'iwatch'
    New-Item -ItemType Directory -Path $aliasParent -Force | Out-Null

    if (Test-Path -LiteralPath $aliasRoot) {
        $aliasItem = Get-Item -LiteralPath $aliasRoot -Force
        $existingTarget = [System.IO.Path]::GetFullPath([string]$aliasItem.Target)
        if ($aliasItem.LinkType -ne 'Junction' -or $existingTarget -ne $RepositoryRoot) {
            throw "Build alias already exists and points elsewhere: $aliasRoot"
        }
    }
    else {
        New-Item -ItemType Junction -Path $aliasRoot -Target $RepositoryRoot | Out-Null
    }

    return $aliasRoot
}

function Enter-IwatchBuildLock {
    param([string]$ProjectDir)
    $lockPath = Join-Path $ProjectDir '.iwatch-build.lock'
    try {
        # GCC 和 Keil 使用同一 SCons 输出，进程退出或异常都会释放句柄锁。
        return [System.IO.File]::Open($lockPath, [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    }
    catch { throw '另一个 iwatch 构建正在使用输出目录，请待其结束再构建。' }
}

function Reset-IwatchBuildDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectDir,
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $projectFull = [System.IO.Path]::GetFullPath($ProjectDir).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $buildFull = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $expected = [System.IO.Path]::GetFullPath(
        (Join-Path $projectFull 'build_iwatch_sf32lb58_a128_qspi_hcpu')
    ).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $comparison = [System.StringComparison]::OrdinalIgnoreCase
    $insideProject = $buildFull.StartsWith(
        $projectFull + [System.IO.Path]::DirectorySeparatorChar,
        $comparison
    )

    if (-not $insideProject -or -not $buildFull.Equals($expected, $comparison)) {
        throw "拒绝清理未经批准的构建目录: $buildFull"
    }
    if (Test-Path -LiteralPath $buildFull) {
        # GCC 与 Keil 共用 SDK 的固定输出名；完整清理可阻止另一工具链的 map/对象残留。
        Remove-Item -LiteralPath $buildFull -Recurse -Force
    }
}
