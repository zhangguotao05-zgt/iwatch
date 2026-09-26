$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$directory = 'work/v00/type-specimens'
New-Item -ItemType Directory -Force "$directory/raw-page" | Out-Null
$spec = Get-Content docs/assets/v00-typography-v1/typography.json -Raw -Encoding UTF8 | ConvertFrom-Json
$cases = @(
    @{Id='Faces.modular:e004'; Index=0; Weight=400; Size=81},
    @{Id='Faces.modular:e004'; Index=1; Weight=400; Size=81},
    @{Id='Faces.modular:e004'; Index=2; Weight=400; Size=81},
    @{Id='Faces.modular:e004'; Index=3; Weight=400; Size=81},
    @{Id='Faces.modular:e004'; Index=4; Weight=400; Size=81},
    @{Id='Faces.modular:e004'; Index=30; Weight=400; Size=81},
    @{Id='Timers.home:e010'; Index=5; Weight=600; Size=49},
    @{Id='Timers.home:e007'; Index=11; Weight=600; Size=25},
    @{Id='Timers.home:e006'; Index=34; Weight=500; Size=26},
    @{Id='Alarms.edit:e082'; Index=13; Weight=400; Size=44},
    @{Id='Settings.display:e020'; Index=16; Weight=400; Size=26},
    @{Id='System.control:e012'; Index=19; Weight=500; Size=31},
    @{Id='System.control:e012'; Index=20; Weight=500; Size=31},
    @{Id='System.control:e012'; Index=21; Weight=500; Size=31},
    @{Id='System.control:e012'; Index=22; Weight=500; Size=31},
    @{Id='System.control:e012'; Index=23; Weight=500; Size=31},
    @{Id='System.control:e012'; Index=31; Weight=500; Size=31}
)
$vs = 'E:\Program Files\Microsoft Visual Studio\2022\Community'
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
& cmake --build firmware/tests/build/d07-a0/tiny-ttf-oom --config Debug --target test_tiny_ttf_oom > "$directory/page-build.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw '页面基线主机构建失败' }
& py -3.13 tools/design/compare_v00_page_pixels.py --self-test
if ($LASTEXITCODE -ne 0) { throw '暗色字边反例未被逐像素比较检出' }
$env:V00_SPEC_OUTPUT = "$directory/raw-page"
$env:V00_SPEC_BG = '17181C'
$results = @()
foreach ($case in $cases) {
    $record = @($spec.records | Where-Object { $_.id -eq $case.Id })
    if ($record.Count -ne 1) { throw "设计记录不唯一：$($case.Id)" }
    $design = $record[0].design
    $bounds = $design.text_bounds
    $env:V00_PAGE_X = [string][math]::Round($bounds.x)
    $env:V00_PAGE_W = [string][math]::Ceiling($bounds.width)
    if ($case.Id -eq 'Faces.modular:e004') {
        # 内部字体对象多留侧边抗锯齿空间；视觉锚点和触摸容器仍取批准稿。
        $env:V00_PAGE_X = '185'
        $env:V00_PAGE_W = '190'
    }
    if ($case.Id -eq 'System.control:e012') {
        # 电量文本框可在原 167px 胶囊内部延展，卡片命中区域不变。
        $env:V00_PAGE_X = '37'
        $env:V00_PAGE_W = '140'
    }
    if ($case.Id -eq 'Timers.home:e007') {
        # 内部字体对象右侧多留两个像素，保留最暗的抗锯齿边缘。
        $env:V00_PAGE_W = '126'
    }
    if ($case.Id -eq 'Settings.display:e020') {
        # 文本仍从原锚点绘制；仅扩展对象宽度，不改变所在行的命中区。
        $env:V00_PAGE_W = '105'
    }
    if ($case.Id -eq 'Timers.home:e006') {
        # 标题样张与正文分开验证，保留字形最右侧的暗色边缘。
        $env:V00_PAGE_W = '80'
    }
    $env:V00_PAGE_H = [string][math]::Ceiling($bounds.height)
    $env:V00_PAGE_BASELINE = [string][math]::Round($design.baseline_y)
    $env:V00_PAGE_ALIGN = [string]$design.alignment
    $env:V00_SPEC_SIZE = [string]$case.Size
    $components = [regex]::Matches($design.color, '\d+') | ForEach-Object { [int]$_.Value }
    $env:V00_SPEC_FG = ('{0:X2}{1:X2}{2:X2}' -f $components[0],$components[1],$components[2])
    $font = "work/v00/fonts/NotoSansSC-review-$($case.Weight).ttf"
    $line = & firmware/tests/build/d07-a0/tiny-ttf-oom/test_tiny_ttf_oom.exe $font component_v00_font_specimen $case.Index 2>&1
    if ($LASTEXITCODE -ne 0) { throw "页面基线样例失败：$line" }
    $comparison = & py -3.13 tools/design/compare_v00_page_pixels.py $case.Index 2>&1
    if ($LASTEXITCODE -gt 1 -or $comparison -notmatch '^RGB565_COMPARE ') {
        throw "页面逐像素比较失败：$comparison"
    }
    $results += "$($case.Id) $line $comparison $(if ($LASTEXITCODE -eq 0) { 'NO_CLIP' } else { 'CLIPPED' })"
}
$results | Set-Content "$directory/page-baselines.log" -Encoding UTF8
Remove-Item Env:V00_SPEC_OUTPUT,Env:V00_SPEC_BG,Env:V00_SPEC_FG,Env:V00_SPEC_SIZE,Env:V00_PAGE_X,Env:V00_PAGE_W,Env:V00_PAGE_H,Env:V00_PAGE_BASELINE,Env:V00_PAGE_ALIGN -ErrorAction SilentlyContinue
if (@($results | Where-Object { $_ -match ' CLIPPED$' }).Count -ne 0) {
    throw '至少一个页面容器裁切字形；详见 page-baselines.log'
}
Write-Output "V00 PAGE BASELINE OK: $($results.Count) examples, no glyph clipping"
