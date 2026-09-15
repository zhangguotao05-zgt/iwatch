[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,
    [ValidateSet('gcc', 'keil')]
    [string]$Toolchain = 'gcc',
    [ValidateSet('DEV_A128_NAND')]
    [string]$BuildProfile = 'DEV_A128_NAND',
    [ValidateRange(115200, 3000000)]
    [int]$Baud = 1000000,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$archiveDir = Join-Path $PSScriptRoot "iwatch\project\artifacts\$BuildProfile\$Toolchain"
$identityPath = Join-Path $archiveDir 'build_identity.json'
$manifestPath = Join-Path $archiveDir 'sftool_param.json'

if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) {
    throw "缺少构建身份记录：$identityPath"
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "缺少烧录清单：$manifestPath"
}

$identity = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($identity.profile -ne $BuildProfile) {
    throw "构建身份中的 profile 为 $($identity.profile)，与请求的 $BuildProfile 不一致。"
}
if ($manifest.chip -ne 'SF32LB58' -or $manifest.memory -ne 'NAND') {
    throw "当前脚本只允许 SF32LB58 / NAND 开发板包，实际为 $($manifest.chip) / $($manifest.memory)。"
}
if (-not $manifest.write_flash.verify) {
    throw '烧录清单没有启用写后校验。'
}

# 三个镜像必须来自同一个受检查归档；禁止单独替换 main 或 FTab。
$required = [ordered]@{
    'bootloader/bootloader.bin' = '0x1C020000'
    'main.bin'                  = '0x69000000'
    'ftab/ftab.bin'             = '0x1C000000'
}
$manifestFiles = @($manifest.write_flash.files)
if ($manifestFiles.Count -ne $required.Count) {
    throw "烧录清单应包含 $($required.Count) 个镜像，实际为 $($manifestFiles.Count) 个。"
}

$archiveFull = [System.IO.Path]::GetFullPath($archiveDir)
$archivePrefix = $archiveFull.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
$flashSpecs = @()
$requiredOrder = @($required.Keys)
for ($index = 0; $index -lt $manifestFiles.Count; $index++) {
    $item = $manifestFiles[$index]
    $relative = ([string]$item.path).Replace('\', '/')
    if (-not $required.Contains($relative)) {
        throw "烧录清单包含未批准的文件：$relative"
    }
    if ($relative -ne $requiredOrder[$index]) {
        throw "烧录清单顺序错误：第 $($index + 1) 项应为 $($requiredOrder[$index])，实际为 $relative。FTab 必须最后写入。"
    }
    $expectedAddress = $required[$relative]
    if ([string]$item.address -ne $expectedAddress) {
        throw "$relative 的地址应为 $expectedAddress，实际为 $($item.address)。"
    }

    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $archiveFull $relative))
    if (-not $fullPath.StartsWith($archivePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "烧录文件越出归档目录：$relative"
    }
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "缺少烧录文件：$fullPath"
    }

    $expectedHash = [string]$identity.artifacts.$relative
    if ([string]::IsNullOrWhiteSpace($expectedHash)) {
        throw "构建身份没有记录 $relative 的 SHA-256。"
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fullPath).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash.ToLowerInvariant()) {
        throw "$relative 的 SHA-256 与构建身份不一致，拒绝烧录。"
    }

    $flashSpecs += "$fullPath@$expectedAddress"
    Write-Host ("已校验 {0}  SHA-256 {1}" -f $relative, $actualHash)
}

if ($DryRun) {
    Write-Host "只读检查通过：$BuildProfile / $Toolchain，未访问 $Port。"
    Write-Host '正式烧录前请安装 Mode 跳帽并复位开发板。'
    exit 0
}

$sftoolCommand = Get-Command 'sftool.exe' -ErrorAction SilentlyContinue
if (-not $sftoolCommand) {
    $userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
    $toolRoot = Join-Path $userProfile '.sifli\tools\sftool'
    $toolCandidate = Get-ChildItem -LiteralPath $toolRoot -Recurse -Filter 'sftool.exe' -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $toolCandidate) {
        throw '找不到 sftool.exe，请先安装 SiFli SDK 工具环境。'
    }
    $sftoolPath = $toolCandidate.FullName
}
else {
    $sftoolPath = $sftoolCommand.Source
}

Write-Host "准备通过 $Port 烧录 $BuildProfile / $Toolchain。"
Write-Host '请确认 Mode 跳帽已安装且开发板已经复位；本流程不会访问 J-Link。'
& $sftoolPath -p $Port -b $Baud -c $manifest.chip -m $manifest.memory.ToLowerInvariant() `
    --before no_reset --after soft_reset write_flash --verify @flashSpecs
if ($LASTEXITCODE -ne 0) {
    throw "sftool 烧录失败，退出码：$LASTEXITCODE。请保持 Mode 跳帽，排查后重新烧录完整三件套。"
}

Write-Host '三件套写入并校验完成。取下 Mode 跳帽，再复位进入正常启动。'
