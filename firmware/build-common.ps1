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

    # SiFli SDK v2.5.1 writes Kconfig include paths using the Windows ANSI
    # code page and reads them back as UTF-8. Use a stable ASCII junction so
    # projects stored below a Chinese path can still be built and opened by Keil.
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
