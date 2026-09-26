param(
    [string]$SourceFont = 'work/v00/NotoSansSC-official.ttf',
    [ValidateSet('design', 'candidate', 'color')]
    [string]$Mode = 'design'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$source = (Resolve-Path $SourceFont -ErrorAction Stop).Path
$output = Join-Path $root 'work/v00/type-specimens'
$rawDir = if ($Mode -eq 'color') { 'raw-color' } elseif ($Mode -eq 'candidate') { 'raw-candidate' } else { 'raw' }
New-Item -ItemType Directory -Force (Join-Path $output $rawDir) | Out-Null

# 四个真实静态字重只作主机候选；不复制进固件或改变现有资源预算。
& py -3.13 tools/design/prepare_v00_font_candidate.py --source $source --output-dir work/v00/fonts --weights 300 400 500 600 |
    Set-Content (Join-Path $output 'candidate-fonts.json') -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw '候选字体生成失败' }

$vs = 'E:\Program Files\Microsoft Visual Studio\2022\Community'
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
& cmake --build firmware/tests/build/d07-a0/tiny-ttf-oom --config Debug --target test_tiny_ttf_oom > (Join-Path $output 'host-build.log') 2>&1
if ($LASTEXITCODE -ne 0) { throw '主机 TinyTTF 构建失败' }

$groups = @(
    @(0, 4, 400), @(5, 11, 600), @(12, 18, 400),
    @(19, 23, 500), @(24, 24, 300), @(25, 26, 400), @(27, 29, 500),
    @(30, 30, 400), @(31, 31, 500), @(32, 32, 600), @(33, 33, 400),
    @(34, 34, 500)
)
$exe = 'firmware/tests/build/d07-a0/tiny-ttf-oom/test_tiny_ttf_oom.exe'
$typography = Get-Content docs/assets/v00-typography-v1/typography.json -Raw -Encoding UTF8 | ConvertFrom-Json
$lines = @()
$sizes = @{FACE_TIME=81; FACE_WEEKDAY=30; TIMER_PRESET=49; TIMER_SECTION=25; ALARM_VALUE=44;
    ROW_TITLE=26; CONTROL_BATTERY=31; ALARM_SEPARATOR=35; ROW_DETAIL=20; ACTION_LABEL=26;
    PAGE_TITLE=26}
$env:V00_SPEC_OUTPUT = "work/v00/type-specimens/$rawDir"
foreach ($group in $groups) {
    $font = "work/v00/fonts/NotoSansSC-review-$($group[2]).ttf"
    for ($index = $group[0]; $index -le $group[1]; $index++) {
        if ($Mode -ne 'design') {
            $role = if ($index -eq 34) { 'PAGE_TITLE' }
                elseif ($index -le 4 -or $index -eq 30) { 'FACE_TIME' } elseif ($index -eq 32) { 'FACE_WEEKDAY' }
                elseif ($index -le 10) { 'TIMER_PRESET' }
                elseif ($index -eq 11) { 'TIMER_SECTION' } elseif ($index -le 15) { 'ALARM_VALUE' }
                elseif ($index -le 18) { 'ROW_TITLE' } elseif ($index -le 23) { 'CONTROL_BATTERY' }
                elseif ($index -eq 31) { 'CONTROL_BATTERY' }
                elseif ($index -eq 24) { 'ALARM_SEPARATOR' } elseif ($index -le 26 -or $index -eq 33) { 'ROW_DETAIL' }
                else { 'ACTION_LABEL' }
            $env:V00_SPEC_SIZE = [string]$sizes[$role]
        } else { Remove-Item Env:V00_SPEC_SIZE -ErrorAction SilentlyContinue }
        if ($Mode -eq 'color') {
            $reference = @($typography.records | Where-Object { $_.role -eq $role })[0]
            $components = [regex]::Matches($reference.design.color, '\d+') | ForEach-Object { [int]$_.Value }
            $env:V00_SPEC_FG = ('{0:X2}{1:X2}{2:X2}' -f $components[0],$components[1],$components[2])
            $env:V00_SPEC_BG = '17181C'
        } else {
            Remove-Item Env:V00_SPEC_FG,Env:V00_SPEC_BG -ErrorAction SilentlyContinue
        }
        $line = & $exe $font component_v00_font_specimen $index 2>&1
        if ($LASTEXITCODE -ne 0) { throw "字体样例 $index 失败：$line" }
        $lines += $line
    }
}
$logName = if ($Mode -eq 'color') { 'color-size.log' } elseif ($Mode -eq 'candidate') { 'candidate-size.log' } else { 'design-size.log' }
$lines | Set-Content (Join-Path $output $logName) -Encoding UTF8
$env:V00_SPEC_MODE = $Mode
& node tools/design/build_v00_font_specimens.cjs
if ($LASTEXITCODE -ne 0) { throw '字体样张归档失败' }
Remove-Item Env:V00_SPEC_MODE,Env:V00_SPEC_SIZE,Env:V00_SPEC_OUTPUT,Env:V00_SPEC_FG,Env:V00_SPEC_BG -ErrorAction SilentlyContinue
Write-Output "V00 字体主机样张完成：$($lines.Count) 条，板测和正式映射仍待审批。"
