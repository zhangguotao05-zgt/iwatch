[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$firmwareRoot = Join-Path $repoRoot 'firmware'
$flashScript = Join-Path $firmwareRoot 'flash-serial.ps1'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

if (-not (Test-Path -LiteralPath $windowsPowerShell -PathType Leaf)) {
    throw "找不到 Windows PowerShell 5.1：$windowsPowerShell"
}

# 固件脚本包含中文提示，必须全部由目标环境按正确编码解析。
$scripts = @(Get-ChildItem -LiteralPath $firmwareRoot -Recurse -Filter '*.ps1' -File)
foreach ($script in $scripts) {
    $escapedPath = $script.FullName.Replace("'", "''")
    $parseCommand = @"
`$tokens = `$null
`$errors = `$null
[void][System.Management.Automation.Language.Parser]::ParseFile('$escapedPath', [ref]`$tokens, [ref]`$errors)
if (`$errors.Count -ne 0) {
    `$errors | ForEach-Object { [Console]::Error.WriteLine(`$_.Message) }
    exit 1
}
"@

    & $windowsPowerShell -NoProfile -NonInteractive -Command $parseCommand
    if ($LASTEXITCODE -ne 0) {
        throw "$($script.FullName) 未通过 Windows PowerShell 5.1 语法检查。"
    }
}

$flashText = [IO.File]::ReadAllText($flashScript, [Text.Encoding]::UTF8)
$utf8ReadCount = ([regex]::Matches($flashText, 'Get-Content[^\r\n]+-Encoding UTF8')).Count
if ($utf8ReadCount -lt 2) {
    throw '构建身份与烧录清单必须显式按 UTF-8 读取。'
}

Write-Host "Windows PowerShell 5.1 脚本语法检查通过：$($scripts.Count) 个文件。"
