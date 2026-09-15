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
